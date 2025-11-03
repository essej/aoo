#include "AooNode.hpp"

// public methods

AooNode::AooNode(int port) {
    LOG_DEBUG("create AooClient on port " << port);

    auto client = AooClient::create();

    AooClientSettings settings;
    settings.portNumber = port;
    settings.userData = this;
    settings.messageHandler = [](void *user, const AooByte *data, AooInt32 size,
                                 const void *address, AooAddrSize addrlen) -> AooError {
        static_cast<AooNode*>(user)->handleMessage(data, size);
        return kAooOk;
    };

    if (auto err = client->setup(settings); err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        throw std::runtime_error(msg);
    }

    // socketType now contains the actual socket type
    if (settings.socketType & kAooSocketIPv6) {
        type_ = aoo::ip_address::IPv6;
    } else if (settings.socketType & kAooSocketIPv4) {
        type_ = aoo::ip_address::IPv4;
    } else {
        throw std::runtime_error("bad socket type");
    }
    ipv4mapped_ = settings.socketType & kAooSocketIPv4Mapped;

    port_ = settings.portNumber; // might have been updated!
    client_ = std::move(client);

    // set event handler *before* starting network threads
    client_->setEventHandler([](void *user, const AooEvent *event, AooThreadLevel level) {
        static_cast<AooNode *>(user)->handleEvent(event);
    }, this, kAooEventModeCallback);

#if NETWORK_THREAD_POLL
    // start network I/O thread
    LOG_DEBUG("start network thread");
    iothread_ = std::thread([this](){
        aoo::sync::set_low_realtime_priority();
        performNetworkIO();
    });
#else
    // start send thread
    LOG_DEBUG("start network send thread");
    sendThread_ = std::thread([this](){
        aoo::sync::set_low_realtime_priority();
        send();
    });
    // start receive thread
    LOG_DEBUG("start network receive thread");
    receiveThread_ = std::thread([this](){
        aoo::sync::set_low_realtime_priority();
        receive();
    });
#endif

    LOG_INFO("new node on port " << port);
}

AooNode::~AooNode() {
    // notify and quit client thread
    client_->stop();
    if (clientThread_.joinable()){
        clientThread_.join();
    }

#if NETWORK_THREAD_POLL
    LOG_DEBUG("join network thread");
    // notify I/O thread
    quit_.store(true);
    if (iothread_.joinable()) {
        iothread_.join();
    }
#else
    LOG_DEBUG("join network threads");
    // join threads
    if (sendThread_.joinable()) {
        sendThread_.join();
    }
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
#endif

    LOG_INFO("release node on port " << port_);
}

// NB: nodes are always unique, so we don't need to keep a per-world dictionary!
static aoo::sync::mutex gNodeMapMutex;
static std::unordered_map<int, std::weak_ptr<AooNode>> gNodeMap;

INode::ptr INode::get(int port) {
    std::shared_ptr<AooNode> node;

    // find or create node (with mutex locked!)
    std::lock_guard lock(gNodeMapMutex);
    auto it = gNodeMap.find(port);
    if (it != gNodeMap.end()) {
        LOG_DEBUG("found node on port " << port);
        node = it->second.lock(); // may be NULL!
    }

    if (!node) {
        // create new AooNode instance
        try {
            node = std::make_shared<AooNode>(port);
            gNodeMap[port] = node; // insert/assign!
        } catch (const std::exception& e) {
            LOG_ERROR("AooNode: " << e.what());
        }
    }

    return node;
}

bool AooNode::registerClient(sc::AooClient *c){
    std::unique_lock lock(clientObjectMutex_);
    if (clientObject_){
        LOG_ERROR("AooClient on port " << port()
                  << " already exists!");
        return false;
    }
    clientObject_ = c;
    lock.unlock();

    if (!clientThread_.joinable()){
        // lazily create client thread
        clientThread_ = std::thread([this]() {
            aoo::sync::set_low_realtime_priority();
            client_->run(kAooInfinite);
        });
    }

    return true;
}

void AooNode::unregisterClient(sc::AooClient *c) {
    assert(clientObject_ == c);
    aoo::sync::unique_lock<aoo::sync::mutex> lock(clientObjectMutex_);
    clientObject_ = nullptr;
}

// private methods

void AooNode::handleEvent(const AooEvent *event) {
    // forward event to client object, if registered.
    aoo::sync::unique_lock<aoo::sync::mutex> lock(clientObjectMutex_);
    if (clientObject_) {
        clientObject_->handleEvent(event);
    }
}

void AooNode::handleMessage(const AooByte *data, int32_t size) {
    try {
        osc::ReceivedPacket packet((const char *)data, size);
        if (packet.IsBundle()) {
            osc::ReceivedBundle bundle(packet);
            auto time = bundle.TimeTag();
            if (bundle.ElementCount() < 1) {
                throw osc::Exception("empty bundle");
            }
            // NB: save the iterator on the stack!
            auto it = bundle.ElementsBegin();
            osc::ReceivedMessage msg(*it);
            handleMessage(time, msg);
        } else {
            osc::ReceivedMessage msg(packet);
            handleMessage(0, msg);
        }
    } catch (const osc::Exception& e) {
        LOG_ERROR("aoo: received bad OSC message: " << e.what());
    }
}

void AooNode::handleMessage(AooNtpTime time, const osc::ReceivedMessage &msg) {
    if (!strcmp(msg.AddressPattern(), "/sc/msg")) {
        auto it = msg.ArgumentsBegin();
        AooId group = (it++)->AsInt32();
        AooId user = (it++)->AsInt32();
        if (time > 1 && it->IsFloat()) {
            auto timeOffset = (it++)->AsFloat();
            time += aoo::time_tag::from_seconds(timeOffset);
        } else {
            time = 0;
            it++;
        }
        auto flags = (it++)->AsInt32();
        auto type = (it++)->AsInt32();
        const void *blobData;
        osc::osc_bundle_element_size_t blobSize;
        (it++)->AsBlob(blobData, blobSize);

        AooData data;
        data.type = type;
        data.data = (const AooByte *)blobData;
        data.size = blobSize;

        client_->sendMessage(group, user, data, time, flags);
    }
}

// TODO: make RT-safe! Only allow numeric IPs, so we don't need to call
// ip_address::resolve(). Hostnames should be resolved client side.
bool AooNode::getEndpointArg(sc_msg_iter *args, aoo::ip_address& addr,
                             int32_t *id, const char *what) const
{
    if (args->remain() < 2){
        LOG_ERROR("too few arguments for " << what);
        return false;
    }

    auto s = args->gets("");

    // first try peer (group|user)
    if (args->nextTag() == 's') {
        auto group = s;
        auto user = args->gets();
        // we can't use length_ptr() because socklen_t != int32_t on many platforms
        AooAddrSize addrlen = aoo::ip_address::max_length;
        if (client_->findPeerByName(group, user, nullptr, nullptr,
                                    addr.address_ptr(), &addrlen) == kAooOk) {
            addr.resize(addrlen);
        } else {
            LOG_ERROR("couldn't find peer " << group << "|" << user);
            return false;
        }
    } else {
        // otherwise try host|port
        auto ipstring = s;
        int port = args->geti();
        addr = aoo::ip_address(ipstring, port, type_, ipv4mapped_);
        if (!addr.valid()) {
            LOG_ERROR("bad IP string '" << ipstring << "' for " << what);
            return false;
        }
    }

    if (id) {
        if (args->remain()) {
            AooId i = args->geti(-1);
            if (i >= 0){
                *id = i;
            } else {
                LOG_ERROR("bad ID '" << i << "' for " << what);
                return false;
            }
        } else {
            LOG_ERROR("too few arguments for " << what);
            return false;
        }
    }

    return true;
}

#if NETWORK_THREAD_POLL

void AooNode::performNetworkIO() {
    try {
        while (!quit_.load(std::memory_order_relaxed)) {
            // receive packets
            if (auto err = client_->receive(kAooInfinite); err != kAooOk) {
                throw std::runtime_error(err == kAooErrorSocket ?
                    aoo::socket_strerror(aoo::socket_errno()) : aoo_strerror(err));
            }
            // send packets
            if (auto err = client_->send(kAooInfinite); err != kAooOk) {
                throw std::runtime_error(err == kAooErrorSocket ?
                    aoo::socket_strerror(aoo::socket_errno()) : aoo_strerror(err));
            }
            auto dur = std::chrono::duration<double>(POLL_INTERVAL);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("AooNote: UDP error: " << e.what());
        // TODO: handle error
    }
}

#else

void AooNode::send(){
    auto err = client_->send(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        LOG_ERROR("AooNote: UDP send error: " << msg);
        // TODO: handle error
    }
}

void AooNode::receive() {
    auto err = client_->receive(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        LOG_ERROR("AooNote: UDP receive error: " << msg);
        // TODO: handle error
    }
}

#endif
