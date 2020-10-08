#include "AooClient.hpp"

#include <unordered_map>

namespace {

InterfaceTable *ft;

aoo::shared_mutex gClientMutex;

using ClientMap = std::unordered_map<int, std::unique_ptr<AooClient>>;
std::unordered_map<World*, ClientMap> gClientMap;

// called from NRT thread
void createClient(World* world, int port) {
    auto client = std::make_unique<AooClient>(world, port);
    aoo::scoped_lock lock(gClientMutex);
    auto& clientMap = gClientMap[world];
    clientMap[port] = std::move(client);
}

// called from NRT thread
void freeClient(World* world, int port) {
    aoo::scoped_lock lock(gClientMutex);
    auto it = gClientMap.find(world);
    if (it != gClientMap.end()) {
        auto& clientMap = it->second;
        clientMap.erase(port);
    }
}

AooClient* findClient(World* world, int port) {
    aoo::shared_scoped_lock lock(gClientMutex);
    auto it = gClientMap.find(world);
    if (it != gClientMap.end()) {
        auto& clientMap = it->second;
        auto it2 = clientMap.find(port);
        if (it2 != clientMap.end()) {
            return it2->second.get();
        }
    }
    return nullptr;
}

int32_t sendToNode(INode* node,
    const char* data, int32_t size, const aoo::ip_address* addr)
{
    return node->sendto(data, size, *addr);
}

} // namespace

// called in NRT thread
AooClient::AooClient(World *world, int32_t port)
    : world_(world), port_(port) {
    auto node = INode::get(world, *this, AOO_TYPE_CLIENT, port, 0);
    if (node) {
        auto client = aoo::net::iclient::create(
                    node.get(), (aoo_sendfn)sendToNode, port);
        client_.reset(client);
        if (client_) {
            LOG_VERBOSE("new AooClient on port " << port);
            // start thread
            thread_ = std::thread([this]() {
                client_->run();
            });
            // done
            setNode(node);
        }
    }
    nrtThread_ = std::this_thread::get_id();
}

// called in NRT thread
AooClient::~AooClient() {
    if (node()) {
    #if USE_PEER_LIST
        node()->removeAllPeers();
    #endif
        releaseNode(); // before quitting client!
    }

    if (client_) {
        client_->quit();
        // wait for thread to finish
        thread_.join();
    }

    LOG_DEBUG("~AooClient");
}

void AooClient::doSend() {
    client_->send();
}

void AooClient::doHandleMessage(const char* data, int32_t size,
                                void* endpoint, aoo_replyfn fn) {
    auto e = (aoo::endpoint*)endpoint;
    client_->handle_message(data, size, (void*)e->address().address(),
                            e->address().length());
}

void AooClient::doUpdate() {
    // handle events
    if (client_->events_available() > 0) {
        client_->handle_events([](void *user, const aoo_event **events, int32_t n) {
            for (int i = 0; i < n; ++i) {
                static_cast<AooClient*>(user)->handleEvent(events[i]);
            }
            return 1;
        }, this);
    }
}                                             \

void AooClient::connect(const char* host, int port,
                        const char* user, const char* pwd) {
    if (!client_){
        sendReply("/aoo/client/connect", false);
        return;
    }

    // LATER also send user ID
    auto cb = [](void* x, int32_t result, const void* data) {
        auto client = (AooClient*)x;
        if (result == 0) {
            client->sendReply("/aoo/client/connect", true);
        }
        else {
            auto e = (const aoo_net_error_reply*)data;
            client->sendReply("/aoo/client/connect", false, e->errormsg);
        }
    };
    client_->connect(host, port, user, pwd, cb, this);
}

void AooClient::disconnect() {
    if (!client_) {
        sendReply("/aoo/client/disconnect", false);
        return;
    }

    auto cb = [](void* x, int32_t result, const void* data) {
        auto client = (AooClient*)x;
    #if USE_PEER_LIST
        // remove peers manually
        client->node()->removeAllPeers();
    #endif
        if (result == 0) {
            client->sendReply("/aoo/client/disconnect", true);
        }
        else {
            auto e = (const aoo_net_error_reply*)data;
            client->sendReply("/aoo/client/disconnect", false, e->errormsg);
        }
    };
    client_->disconnect(cb, this);
}

void AooClient::joinGroup(const char* name, const char* pwd) {
    if (!client_) {
        sendGroupReply("/aoo/client/group/join", name, false);
        return;
    }

    auto cb = [](void* x, int32_t result, const void* data) {
        auto request = (GroupRequest *)x;
        if (result == 0) {
            request->obj->sendGroupReply("/aoo/client/group/join",
                request->group.c_str(), true);
        } else {
            auto e = (const aoo_net_error_reply*)data;
            request->obj->sendGroupReply("/aoo/client/group/join",
                request->group.c_str(), false, e->errormsg);
        }
        delete request;
    };
    client_->join_group(name, pwd, cb, new GroupRequest{ this, name });
}

void AooClient::leaveGroup(const char* name) {
    if (!client_) {
        sendGroupReply("/aoo/client/group/leave", name, false);
        return;
    }

    auto cb = [](void* x, int32_t result, const void* data) {
        auto request = (GroupRequest*)x;
        if (result == 0) {
        #if USE_PEER_LIST
            // remove peers manually
            request->obj->node()->removeGroup(request->group);
        #endif
            request->obj->sendGroupReply("/aoo/client/group/leave",
                request->group.c_str(), true);
        } else {
            auto e = (const aoo_net_error_reply*)data;
            request->obj->sendGroupReply("/aoo/client/group/leave",
                request->group.c_str(), false, e->errormsg);
        }
        delete request;
    };
    client_->leave_group(name, cb, new GroupRequest{ this, name });
}

// called from network thread
void AooClient::handleEvent(const aoo_event* event) {
    char buf[1024];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage("/aoo/client/event") << port_;

    // Lock NRT thread, so we don't have to protect the peer list with a mutex.
    // We have to do this anyway for sending events to the client
    NRTLock(world_);

    switch (event->type) {
    case AOO_NET_DISCONNECT_EVENT:
    {
        msg << "/disconnect";
    #if USE_PEER_LIST
        node()->removeAllPeers();
    #endif
        break;
    }
    case AOO_NET_PEER_JOIN_EVENT:
    {
        auto e = (const aoo_net_peer_event*)event;
        aoo::ip_address addr((const sockaddr*)e->address, e->length);
        msg << "/peer/join" << addr.name() << addr.port()
            << e->group_name << e->user_name << e->user_id;
    #if USE_PEER_LIST
        node()->addPeer(e->group_name, e->user_name, e->user_id, addr);
    #endif
        break;
    }
    case AOO_NET_PEER_LEAVE_EVENT:
    {
        auto e = (const aoo_net_peer_event*)event;
        aoo::ip_address addr((const sockaddr*)e->address, e->length);
        msg << "/peer/leave" << addr.name() << addr.port()
            << e->group_name << e->user_name << e->user_id;
    #if USE_PEER_LIST
        node()->removePeer(e->group_name, e->user_name);
    #endif
        break;
    }
    case AOO_NET_ERROR_EVENT:
    {
        auto e = (const aoo_net_error_event*)event;
        msg << "/error" << e->errorcode << e->errormsg;
        break;
    }
    default:
        LOG_ERROR("AooClient: got unknown event " << event->type);
        NRTUnlock(world_);
        return; // don't send event!
    }

    msg << osc::EndMessage;
    ::sendMsgNRT(world_, msg);
    NRTUnlock(world_);
}

void AooClient::sendReply(const char *cmd, bool success,
                          const char *errmsg){
    char buf[1024];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage(cmd) << port_ << (int)success;
    if (errmsg){
        msg << errmsg;
    }
    msg << osc::EndMessage;

    // only lock when we're not in the NRT thread!
    bool lock = std::this_thread::get_id() != nrtThread_;

    if (lock) NRTLock(world_);
    ::sendMsgNRT(world_, msg);
    if (lock) NRTUnlock(world_);
}

void AooClient::sendGroupReply(const char* cmd, const char* group,
                               bool success, const char* errmsg)
{
    char buf[1024];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage(cmd) << port_ << group << (int)success;
    if (errmsg) {
        msg << errmsg;
    }
    msg << osc::EndMessage;

    // only lock when we're not in the NRT thread!
    bool lock = std::this_thread::get_id() != nrtThread_;

    if (lock) NRTLock(world_);
    ::sendMsgNRT(world_, msg);
    if (lock) NRTUnlock(world_);
}

namespace {

template<typename T>
void doCommand(World* world, void *replyAddr, T* cmd, AsyncStageFn fn) {
    DoAsynchronousCommand(world, replyAddr, 0, cmd,
        fn, 0, 0, CmdData::free<T>, 0, 0);
}

void aoo_client_new(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();

    auto cmdData = CmdData::create<AooClientCmd>(world);
    if (cmdData) {
        cmdData->port = port;

        auto fn = [](World* world, void* data) {
            auto port = static_cast<AooClientCmd*>(data)->port;
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/new") << port;

            if (findClient(world, port)) {
                char errbuf[256];
                snprintf(errbuf, sizeof(errbuf),
                    "AooClient on port %d already exists.", port);
                msg << 0 << errbuf;
            } else {
                createClient(world, port);
                msg << 1;
            }

            msg << osc::EndMessage;
            ::sendMsgNRT(world, msg);

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_free(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();

    auto cmdData = CmdData::create<AooClientCmd>(world);
    if (cmdData) {
        cmdData->port = port;

        auto fn = [](World * world, void* data) {
            auto port = static_cast<AooClientCmd*>(data)->port;

            freeClient(world, port);

            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/free")
                << port << osc::EndMessage;
            ::sendMsgNRT(world, msg);

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

// called from the NRT
AooClient* aoo_client_get(World *world, int port, const char *cmd)
{
    auto client = findClient(world, port);
    if (!client){
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf),
            "couldn't find AooClient on port %d", port);

        char buf[1024];
        osc::OutboundPacketStream msg(buf, sizeof(buf));

        msg << osc::BeginMessage(cmd) << port << 0 << errbuf
            << osc::EndMessage;

        ::sendMsgNRT(world, msg);
    }
    return client;
}

void aoo_client_connect(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto serverName = args->gets();
    auto serverPort = args->geti();
    auto userName = args->gets();
    auto userPwd = args->gets();

    auto cmdData = CmdData::create<ConnectCmd>(world);
    if (cmdData) {
        cmdData->port = port;
        snprintf(cmdData->serverName, sizeof(cmdData->serverName),
            "%s", serverName);
        cmdData->serverPort = serverPort;
        snprintf(cmdData->userName, sizeof(cmdData->userName),
            "%s", userName);
        snprintf(cmdData->userPwd, sizeof(cmdData->userPwd),
            "%s", userPwd);

        auto fn = [](World * world, void* cmdData) {
            auto data = (ConnectCmd*)cmdData;
            auto client = aoo_client_get(world, data->port,
                                         "/aoo/client/connect");
            if (client) {
                client->connect(data->serverName, data->serverPort,
                                data->userName, data->userPwd);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_disconnect(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();

    auto cmdData = CmdData::create<AooClientCmd>(world);
    if (cmdData) {
        cmdData->port = port;

        auto fn = [](World * world, void* cmdData) {
            auto data = (AooClientCmd *)cmdData;
            auto client = aoo_client_get(world, data->port,
                                         "/aoo/client/disconnect");
            if (client) {
                client->disconnect();
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_group_join(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto name = args->gets();
    auto pwd = args->gets();

    auto cmdData = CmdData::create<GroupCmd>(world);
    if (cmdData) {
        cmdData->port = port;
        snprintf(cmdData->name, sizeof(cmdData->name),
            "%s", name);
        snprintf(cmdData->pwd, sizeof(cmdData->pwd),
            "%s", pwd);

        auto fn = [](World * world, void* cmdData) {
            auto data = (GroupCmd *)cmdData;
            auto client = aoo_client_get(world, data->port,
                                         "/aoo/client/group/join");
            if (client) {
                client->joinGroup(data->name, data->pwd);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_group_leave(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto name = args->gets();

    auto cmdData = CmdData::create<GroupCmd>(world);
    if (cmdData) {
        cmdData->port = port;
        snprintf(cmdData->name, sizeof(cmdData->name),
            "%s", name);

        auto fn = [](World * world, void* cmdData) {
            auto data = (GroupCmd *)cmdData;
            auto client = aoo_client_get(world, data->port,
                                         "/aoo/client/group/leave");
            if (client) {
                client->leaveGroup(data->name);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

} // namespace

/*////////////// Setup /////////////////*/

void AooClientLoad(InterfaceTable *inTable){
    ft = inTable;

    AooPluginCmd(aoo_client_new);
    AooPluginCmd(aoo_client_free);
    AooPluginCmd(aoo_client_connect);
    AooPluginCmd(aoo_client_disconnect);
    AooPluginCmd(aoo_client_group_join);
    AooPluginCmd(aoo_client_group_leave);
}
