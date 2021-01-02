#include "Aoo.hpp"
#include "AooClient.hpp"

#include "common/net_utils.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"

#include <unordered_map>
#include <cstring>
#include <stdio.h>
#include <errno.h>

#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>

#define AOO_POLL_INTERVAL 1000 // microseconds

using namespace aoo;

class AooNode final : public INode {
    friend class INode;
public:
    AooNode(World *world, int socket, const ip_address& addr);

    ~AooNode();

    aoo::ip_address::ip_type type() const override { return type_; }

    int port() const override { return port_; }

    aoo::net::iclient * client() override {
        return client_.get();
    }

    bool registerClient(AooClient *c) override;

    void unregisterClient(AooClient *c) override;

    void notify() override;

    void lock() override {
        clientMutex_.lock();
    }

    void unlock() override {
        clientMutex_.unlock();
    }
private:
    World *world_;
    int socket_ = -1;
    int port_ = 0;
    aoo::ip_address::ip_type type_;
    // client
    aoo::net::iclient::pointer client_;
    std::mutex clientMutex_;
    std::thread clientThread_;
    AooClient *clientObject_ = nullptr;
    // threading
    std::thread thread_;
    std::atomic<bool> event_{false};
    std::atomic<bool> quit_{false};

    // private methods
    static int32_t send(void *user, const char *msg, int32_t size,
                        const void *addr, int32_t addrlen, uint32_t flags);

    void performNetworkIO();

    void handleClientMessage(const char *data, int32_t size,
                             const ip_address& addr, aoo::time_tag time);

    void handleClientBundle(const osc::ReceivedBundle& bundle,
                            const ip_address& addr);
};

// public methods

AooNode::AooNode(World *world, int socket, const ip_address& addr)
    : world_(world), socket_(socket), port_(addr.port()), type_(addr.type())
{
    client_.reset(aoo::net::iclient::create(addr.address(), addr.length(), 0));
    // start network thread
    thread_ = std::thread([this](){
        lower_thread_priority();

        performNetworkIO();
    });

    LOG_VERBOSE("aoo: new node on port " << port_);
}

AooNode::~AooNode(){
    // tell the network thread that we're done
    quit_ = true;
    socket_signal(socket_);

    thread_.join();

    socket_close(socket_);

    // quit client thread
    if (clientThread_.joinable()){
        client_->quit();
        clientThread_.join();
    }

    LOG_VERBOSE("aoo: released node on port " << port_);
}

using NodeMap = std::unordered_map<int, std::weak_ptr<AooNode>>;

aoo::shared_mutex gNodeMapMutex;
static std::unordered_map<World *, NodeMap> gNodeMap;

static NodeMap& getNodeMap(World *world){
    scoped_lock lock(gNodeMapMutex);
    return gNodeMap[world];
}

INode::ptr INode::get(World *world, int port){
    std::shared_ptr<AooNode> node;

    auto& nodeMap = getNodeMap(world);

    // find or create node
    auto it = nodeMap.find(port);
    if (it != nodeMap.end()){
        node = it->second.lock();
    }

    if (!node){
        // first create socket
        int sock = socket_udp(port);
        if (sock < 0){
            LOG_ERROR("AooNode: couldn't bind to port " << port);
            return nullptr;
        }

        ip_address addr;
        if (socket_address(sock, addr) != 0){
            LOG_ERROR("AooNode: couldn't get socket address");
            socket_close(sock);
            return nullptr;
        }

        // increase send buffer size to 65 kB
        socket_setsendbufsize(sock, 2 << 15);
        // increase receive buffer size to 2 MB
        socket_setrecvbufsize(sock, 2 << 20);

        // finally create aoo node instance
        node = std::make_shared<AooNode>(world, sock, addr);
        nodeMap.emplace(port, node);
    }

    return node;
}

bool AooNode::registerClient(AooClient *c){
    std::lock_guard<std::mutex> lock(clientMutex_);
    if (clientObject_){
        LOG_ERROR("aoo client on port " << port_
                  << " already exists!");
        return false;
    }
    if (!clientThread_.joinable()){
        // lazily create client thread
        clientThread_ = std::thread([this](){
            client_->run();
        });
    }
    clientObject_ = c;
    client_->set_eventhandler(
        [](void *user, const aoo_event *event, int32_t) {
            static_cast<AooClient*>(user)->handleEvent(event);
        }, c, AOO_EVENT_CALLBACK);
    return true;
}

void AooNode::unregisterClient(AooClient *c){
    std::lock_guard<std::mutex> lock(clientMutex_);
    assert(clientObject_ == c);
    clientObject_ = nullptr;
    client_->set_eventhandler(nullptr, nullptr, AOO_EVENT_NONE);
}

void AooNode::notify(){
    event_.store(true);
}

// private methods

int32_t AooNode::send(void *user, const char *msg, int32_t size,
                      const void *addr, int32_t addrlen, uint32_t flags)
{
    auto x = (AooNode *)user;
    ip_address address((const sockaddr *)addr, addrlen);
    return socket_sendto(x->socket_, msg, size, address);
}

void AooNode::performNetworkIO(){
    ip_address addr;
    char buf[AOO_MAXPACKETSIZE];
    int nbytes = socket_receive(socket_, buf, AOO_MAXPACKETSIZE,
                                &addr, AOO_POLL_INTERVAL);
    if (nbytes > 0){
        try {
            osc::ReceivedPacket packet(buf, nbytes);
            if (packet.IsBundle()){
                osc::ReceivedBundle bundle(packet);
                handleClientBundle(bundle, addr);
            } else {
                handleClientMessage(buf, nbytes, addr, aoo::time_tag::immediate());
            }
        } catch (const osc::Exception &err){
            LOG_ERROR("AooNode: bad OSC message - " << err.what());
        }
    } else if (nbytes == 0){
        // timeout - update client
        std::lock_guard<std::mutex> lock(clientMutex_);
        client_->update(send, this);
    } else {
        // ignore errors when quitting
        if (!quit_){
            socket_error_print("recv");
        }
        return;
    }

    if (event_.exchange(false)){
        std::lock_guard<std::mutex> lock(clientMutex_);
        client_->update(send, this);
    }
}

void AooNode::handleClientMessage(const char *data, int32_t size,
                                  const ip_address& addr, aoo::time_tag time)
{
    if (size > 4 && !memcmp("/aoo", data, 4)){
        // AoO message
        client_->handle_message(data, size, addr.address(), addr.length(),
                                send, this);
    } else if (!strncmp("/sc/msg", data, size)){
        // OSC message coming from language client
        std::lock_guard<std::mutex> lock(clientMutex_);
        if (clientObject_){
            clientObject_->forwardMessage(data, size, time);
        }
    } else {
        LOG_WARNING("AooNode: unknown OSC message " << data);
    }
}

void AooNode::handleClientBundle(const osc::ReceivedBundle &bundle,
                                 const ip_address& addr){
    auto time = bundle.TimeTag();
    auto it = bundle.ElementsBegin();
    while (it != bundle.ElementsEnd()){
        if (it->IsBundle()){
            osc::ReceivedBundle b(*it);
            handleClientBundle(b, addr);
        } else {
            handleClientMessage(it->Contents(), it->Size(), addr, time);
        }
        ++it;
    }
}
