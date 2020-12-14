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

#ifndef AOO_NODE_POLL
 #define AOO_NODE_POLL 0
#endif

#define AOO_POLL_INTERVAL 1000 // microseconds

using namespace aoo;

class AooNode final : public INode {
    friend class INode;
public:
    AooNode(World *world, int socket, int port);
    ~AooNode();

    aoo::ip_address::ip_type type() const { return type_; }

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
    aoo::shared_mutex clientMutex_;
    std::thread clientThread_;
    AooClient *clientObject_ = nullptr;
    // threading
#if AOO_NODE_POLL
    std::thread thread_;
#else
    std::thread sendThread_;
    std::thread receiveThread_;
    std::mutex mutex_;
    std::condition_variable condition_;
#endif
    std::atomic<bool> quit_{false};

    // private methods
    void doSend();

    void doReceive();

    void handleClientMessage(const char *data, int32_t size,
                             const ip_address& addr, aoo::time_tag time);

    void handleClientBundle(const osc::ReceivedBundle& bundle,
                            const ip_address& addr);
};

// public methods

AooNode::AooNode(World *world, int socket, int port)
    : world_(world), socket_(socket), port_(port)
{
    type_ = socket_family(socket);

    auto fn = [](void *user, const char *msg, int32_t size,
                 const void *addr, int32_t addrlen, uint32_t flags)
    {
        auto x = (AooNode *)user;
        ip_address address((const sockaddr *)addr, addrlen);
        return socket_sendto(x->socket_, msg, size, address);
    };
    client_.reset(aoo::net::iclient::create(socket, fn, this, 0));
    // start threads
#if AOO_NODE_POLL
    thread_ = std::thread([this](){
        lower_thread_priority();

        while (!quit_){
            do_receive();
            do_send();
        }
    });
#else
    sendThread_ = std::thread([this](){
        lower_thread_priority();

        std::unique_lock<std::mutex> lock(mutex_);
        while (!quit_){
            condition_.wait(lock);
            doSend();
        }
    });
    receiveThread_ = std::thread([this](){
        lower_thread_priority();

        while (!quit_){
            doReceive();
        }
    });
#endif

    LOG_VERBOSE("aoo: new node on port " << port_);
}

AooNode::~AooNode(){
    // tell the threads that we're done
#if AOO_NODE_POLL
    // don't bother waking up the thread...
    // just set the flag and wait
    quit_ = true;
    thread_.join();

    socket_close(socket_);
#else
    {
        std::lock_guard<std::mutex> l(mutex_);
        quit_ = true;
    }

    // notify send thread
    condition_.notify_all();

    // try to wake up receive thread
    aoo::unique_lock lock(clientMutex_);
    int didit = socket_signal(socket_);
    if (!didit){
        // force wakeup by closing the socket.
        // this is not nice and probably undefined behavior,
        // the MSDN docs explicitly forbid it!
        socket_close(socket_);
    }
    lock.unlock();

    // wait for threads
    sendThread_.join();
    receiveThread_.join();

    if (didit){
        socket_close(socket_);
    }
#endif

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
            LOG_ERROR("aoo node: couldn't bind to port " << port);
            return nullptr;
        }

        // increase send buffer size to 65 kB
        socket_setsendbufsize(sock, 2 << 15);
        // increase receive buffer size to 2 MB
        socket_setrecvbufsize(sock, 2 << 20);

        // finally create aoo node instance
        node = std::make_shared<AooNode>(world, sock, port);
        nodeMap.emplace(port, node);
    }

    return node;
}

bool AooNode::registerClient(AooClient *c){
    aoo::scoped_lock lock(clientMutex_);
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
    return true;
}

void AooNode::unregisterClient(AooClient *c){
    aoo::scoped_lock lock(clientMutex_);
    assert(clientObject_ == c);
    clientObject_ = nullptr;
}

void AooNode::notify(){
#if !AOO_NODE_POLL
    condition_.notify_all();
#endif
}

// private methods

void AooNode::doSend()
{
    aoo::shared_scoped_lock lock(clientMutex_);
    client_->send();
}

void AooNode::doReceive()
{
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
        notify(); // !
    } else if (nbytes == 0){
        // timeout - update client
        aoo::shared_scoped_lock lock(clientMutex_);
        client_->handle_message(nullptr, 0, nullptr, 0);
        notify(); // !
    } else {
        // ignore errors when quitting
        if (!quit_){
            socket_error_print("recv");
        }
        return;
    }

    if (client_->events_available()){
        // poll events
        aoo::shared_scoped_lock lock(clientMutex_);
        if (clientObject_){
            client_->poll_events(
                [](void *user, const aoo_event *event) {
                    static_cast<AooClient*>(user)->handleEvent(event);
                }, clientObject_);
        }
    }
}

void AooNode::handleClientMessage(const char *data, int32_t size,
                                  const ip_address& addr, aoo::time_tag time)
{
    if (size > 4 && !memcmp("/aoo", data, 4)){
        // AoO message
        client_->handle_message(data, size, addr.address(), addr.length());
    } else if (!strncmp("/sc/msg", data, size)){
        // OSC message coming from language client
        aoo::shared_scoped_lock lock(clientMutex_);
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
