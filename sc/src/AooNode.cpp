#include "Aoo.hpp"
#include "AooClient.hpp"

#include "common/net_utils.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"

#include <vector>
#include <list>
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

#if USE_PEER_LIST
struct AooPeer {
    std::string group;
    std::string user;
    aoo::ip_address address;
    aoo_id id;
};
#endif

struct AooNodeClient {
    INodeClient *obj;
    aoo_type type;
    aoo_id id;
};

class AooNode final : public INode {
    friend class INode;
public:
    AooNode(World *world, int socket, int port);
    ~AooNode();

    void release(INodeClient& client) override;

    aoo::ip_address::ip_type type() const { return type_; }

    int socket() const override { return socket_; }

    int port() const override { return port_; }

    int sendto(const char *buf, int32_t size,
               const ip_address& addr) override
    {
        return socket_sendto(socket_, buf, size, addr);
    }

#if USE_PEER_LIST
    bool findPeer(const std::string& group, const std::string& user,
                  aoo::ip_address& addr);

    void addPeer(const std::string& group, const std::string& user,
                 const ip_address& addr, aoo_id id) override;

    void removePeer(const std::string& group,
                    const std::string& user) override;

    void removeAllPeers() override;

    void removeGroup(const std::string& group) override;
#endif

    void notify() override;
private:
    World *world_;
    int socket_ = -1;
    int port_ = 0;
    aoo::ip_address::ip_type type_;
    // dependants
    std::vector<AooNodeClient> clients_;
    aoo::shared_mutex clientMutex_;
#if USE_PEER_LIST
    // peer list
    std::vector<AooPeer> peers_;
#endif
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
    bool addClient(INodeClient& client, aoo_type type, aoo_id id);

    void doSend();

    void doReceive();

    void handleClientMessage(const char *data, int32_t size,
                             aoo::time_tag time);

    void handleClientBundle(const osc::ReceivedBundle& bundle);
};

// public methods

AooNode::AooNode(World *world, int socket, int port)
    : world_(world), socket_(socket), port_(port)
{
    type_ = socket_family(socket);
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
    int didit = socket_signal(socket_, port_);
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

    LOG_VERBOSE("aoo: released node on port " << port_);
}

using NodeMap = std::unordered_map<int, std::weak_ptr<AooNode>>;

shared_mutex gNodeMapMutex;
static std::unordered_map<World *, NodeMap> gNodeMap;

static NodeMap& getNodeMap(World *world){
    scoped_lock lock(gNodeMapMutex);
    return gNodeMap[world];
}

INode::ptr INode::get(World *world, INodeClient& client,
                      aoo_type type, int port, aoo_id id)
{
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

    if (!node->addClient(client, type, id)){
        // never happens for new node
        return nullptr;
    }

    return node;
}

void AooNode::release(INodeClient& client){
    // remove receiver from list
    aoo::scoped_lock l(clientMutex_);
    for (auto it = clients_.begin(); it != clients_.end(); ++it){
        if (&client == it->obj){
            clients_.erase(it);
            return;
        }
    }
    LOG_ERROR("AooNode::release: client not found!");
}

#if USE_PEER_LIST
bool AooNode::findPeer(const std::string& group, const std::string& user,
                       aoo::ip_address& addr)
{
    for (auto& peer : peers_){
        if (peer.group == group && peer.user == user){
            addr = peer.address;
            return true;
        }
    }
    return false;
}

void AooNode::addPeer(const std::string& group,
                      const std::string& user,
                      const ip_address& addr, aoo_id id)
{
    aoo::ip_address dummy;
    if (findPeer(group, user, dummy)){
        LOG_ERROR("AooNode::add_peer: peer already added");
        return;
    }

    peers_.push_back({ group, user, addr, id });
}

void AooNode::removePeer(const std::string& group,
                         const std::string& user)
{
    for (auto it = peers_.begin(); it != peers_.end(); ++it){
        if (it->group == group && it->user == user){
            peers_.erase(it);
            return;
        }
    }
    LOG_ERROR("AooNode::remove_peer: couldn't find peer");
}

void AooNode::removeAllPeers(){
    peers_.clear();
}

void AooNode::removeGroup(const std::string& group){
    for (auto it = peers_.begin(); it != peers_.end(); ) {
        // remove all peers matching group
        if (it->group == group){
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }
}
#endif

void AooNode::notify(){
#if !AOO_NODE_POLL
    condition_.notify_all();
#endif
}

// private methods

bool AooNode::addClient(INodeClient& client, aoo_type type, aoo_id id)
{
    // check client and add to list
    aoo::scoped_lock lock(clientMutex_);
#if 1
    for (auto& c : clients_)
    {
        if (c.type == type && c.id == id) {
            if (c.obj == &client){
                LOG_ERROR("AooNode::add_client: client already added!");
            } else {
                if (type == AOO_TYPE_CLIENT){
                    LOG_ERROR("aoo client on port " << port_ << " already exists!");
                } else {
                    auto which = (type == AOO_TYPE_SOURCE) ? "source" : "sink";
                    LOG_ERROR("aoo " << which << " with ID " << id
                              << " on port " << port_ << " already exists!");
                }
            }
            return false;
        }
    }
#endif
    clients_.push_back({ &client, type, id });
    return true;
}

void AooNode::doSend()
{
    aoo::shared_scoped_lock lock(clientMutex_);

    for (auto& c : clients_){
        c.obj->send();
    }
}

void AooNode::doReceive()
{
    ip_address addr;
    char buf[AOO_MAXPACKETSIZE];
    int nbytes = socket_receive(socket_, buf, AOO_MAXPACKETSIZE,
                                &addr, AOO_POLL_INTERVAL);
    if (nbytes > 0){
        // get sink ID
        aoo_type type;
        aoo_id id;
        if (aoo_parse_pattern(buf, nbytes, &type, &id))
        {
            // forward OSC packet to matching clients(s)
            aoo::shared_scoped_lock l(clientMutex_);
            for (auto& c : clients_){
                switch (type) {
                case AOO_TYPE_SOURCE:
                case AOO_TYPE_SINK:
                    if ((type == c.type) &&
                        ((id == AOO_ID_WILDCARD) || (id == c.id)))
                    {
                        c.obj->handleMessage(buf, nbytes, addr);
                        if (id != AOO_ID_WILDCARD)
                            goto parse_done;
                    }
                    break;
                case AOO_TYPE_CLIENT:
                case AOO_TYPE_PEER:
                    if (c.type == AOO_TYPE_CLIENT) {
                        c.obj->handleMessage(buf, nbytes, addr);
                        goto parse_done; // there's only one client
                    }
                    break;
                default:
                    break; // ignore
                }
            }
        } else {
            try {
                osc::ReceivedPacket packet(buf, nbytes);
                if (packet.IsBundle()){
                    osc::ReceivedBundle bundle(packet);
                    handleClientBundle(bundle);
                } else {
                    handleClientMessage(buf, nbytes, aoo::time_tag::immediate());
                }
            } catch (const osc::Exception &err){
                LOG_ERROR("AooNode: bad OSC message - " << err.what());
            }
        }
    parse_done:
        notify(); // !
    } else if (nbytes == 0){
        // timeout -> update clients
        aoo::shared_scoped_lock lock(clientMutex_);
        for (auto& c : clients_){
            c.obj->update();
        }
        notify(); // !
    } else {
        // ignore errors when quitting
        if (!quit_){
            socket_error_print("recv");
        }
        return;
    }
}

void AooNode::handleClientMessage(const char *data, int32_t size,
                                  aoo::time_tag time)
{
    if (!strncmp("/sc/msg", data, size)){
        // LATER cache AooClient
        aoo::shared_scoped_lock l(clientMutex_);
        for (auto& c : clients_){
            if (c.type == AOO_TYPE_CLIENT){
                auto client = static_cast<AooClient *>(c.obj);
                client->forwardMessage(data, size, time);
                break;
            }
        }
    } else {
        LOG_WARNING("AooNode: unknown OSC message " << data);
    }
}

void AooNode::handleClientBundle(const osc::ReceivedBundle &bundle){
    auto time = bundle.TimeTag();
    auto it = bundle.ElementsBegin();
    while (it != bundle.ElementsEnd()){
        if (it->IsBundle()){
            osc::ReceivedBundle b(*it);
            handleClientBundle(b);
        } else {
            handleClientMessage(it->Contents(), it->Size(), time);
        }
        ++it;
    }
}
