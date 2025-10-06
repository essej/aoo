#include "AooClient.hpp"

#include <unordered_map>

namespace {

InterfaceTable *ft;

// NB: only one World is ever accessing a given client;
// we only need to protect the dictionary itself for concurrent
// insertations/deletations.
aoo::sync::mutex gClientMutex;
std::unordered_map<int, std::shared_ptr<sc::AooClient>> gClientMap;

// Called from NRT threads(s). Throws on failure!
std::shared_ptr<sc::AooClient> createClient(int port) {
    // protect everything with a mutex!
    // no need to check dictionary for port, because AooClient
    // already does it implicitly.
    aoo::sync::scoped_lock lock(gClientMutex);
    auto client = std::make_shared<sc::AooClient>(port);
    gClientMap[port] = client;
    return client;
}

// Called from NRT thread(s)
bool destroyClient(int port) {
    aoo::sync::scoped_lock lock(gClientMutex);
    if (auto it = gClientMap.find(port); it != gClientMap.end()) {
        gClientMap.erase(it);
        return true;
    } else {
        LOG_ERROR("aoo: cannot free client on port " << port << " - not found!");
        return false;
    }
}

std::shared_ptr<sc::AooClient> findClient(int port) {
    std::lock_guard lock(gClientMutex);
    auto it = gClientMap.find(port);
    if (it != gClientMap.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

} // namespace

namespace sc {

// called in NRT thread
AooClient::AooClient(int32_t port) {
    auto node = INode::get(port);
    if (node && node->registerClient(this)) {
        LOG_INFO("new AooClient on port " << port);
        node_ = node;
    } else {
        throw std::runtime_error("cannot create");
    }
}

// called in NRT thread
AooClient::~AooClient() {
    assert(node_);
    node_->unregisterClient(this);
    LOG_DEBUG("~AooClient");
}

void AooClient::connect(int token, const char* host,
                        int port, const char *pwd, const AooData *metadata) {
    auto cb = [](void* x, const AooRequest *request,
            AooError result, const AooResponse *response) {
        auto data = (RequestData *)x;
        auto client = data->client;
        auto token = data->token;
        auto& r = response->connect;

        if (result == kAooErrorNone) {
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/connect")
                << client->node_->port() << token << (int32_t)0
                << r.clientId << r.version;
            serializeData(msg, r.metadata);
            msg << osc::EndMessage;

            client->sendReply(msg);
        } else {
            client->sendError("/aoo/client/connect", token, result, *response);
        }

        delete data;
    };

    AooClientConnect args;
    args.hostName = host;
    args.port = port;
    args.password = pwd;
    args.metadata = metadata;

    node_->client()->connect(args, cb, new RequestData { this, token });
}

void AooClient::disconnect(int token) {
    auto cb = [](void* x, const AooRequest *request,
            AooError result, const AooResponse *response) {
        auto data = (RequestData *)x;
        auto client = data->client;
        auto token = data->token;

        if (result == kAooErrorNone) {
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/disconnect")
                << client->node_->port() << token << (int32_t)0
                << osc::EndMessage;

            client->sendReply(msg);
        } else {
            client->sendError("/aoo/client/disconnect", token, result, *response);
        }

        delete data;
    };

    node_->client()->disconnect(cb, new RequestData { this, token });
}

void AooClient::joinGroup(int token, const char* groupName, const char *groupPwd,
                          const AooData *groupMetadata, const char* userName,
                          const char *userPwd, const AooData *userMetadata,
                          const AooIpEndpoint *relayAddress)
{
    auto cb = [](void* x, const AooRequest *request,
            AooError result, const AooResponse* response) {
        auto data = (RequestData *)x;
        auto client = data->client;
        auto token = data->token;
        auto& r = response->groupJoin;

        if (result == kAooErrorNone) {
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/group/join")
                << client->node_->port() << token << (int32_t)0
                << r.groupId << r.userId;
            serializeData(msg, r.groupMetadata);
            serializeData(msg, r.userMetadata);
            serializeData(msg, r.privateMetadata);
            if (r.relayAddress) {
                msg << r.relayAddress->hostName << r.relayAddress->port;
            } else {
                msg << osc::Nil << osc::Nil;
            }
            msg << osc::EndMessage;

            client->sendReply(msg);
        } else {
            client->sendError("/aoo/client/group/join", token, result, *response);
        }

        delete data;
    };

    AooClientJoinGroup args;
    args.groupName = groupName;
    args.groupPassword = groupPwd;
    args.groupMetadata = groupMetadata;
    args.userName = userName;
    args.userPassword = userPwd;
    args.userMetadata = userMetadata;
    args.relayAddress = relayAddress;

    node_->client()->joinGroup(args, cb, new RequestData { this, token });
}

void AooClient::leaveGroup(int token, AooId group) {
    auto cb = [](void* x, const AooRequest *request,
            AooError result, const AooResponse* response) {
        auto data = (RequestData *)x;
        auto client = data->client;
        auto token = data->token;

        if (result == kAooErrorNone) {
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/group/leave")
                << client->node_->port() << token << (int32_t)0
                << osc::EndMessage;

            client->sendReply(msg);
        } else {
            client->sendError("/aoo/client/group/leave", token, result, *response);
        }

        delete data;
    };

    node_->client()->leaveGroup(group, cb, new RequestData { this, token });
}

void AooClient::updateGroup(int token, AooId group,
                            const AooData& groupMetadata) {
    auto cb = [](void* x, const AooRequest *request,
                 AooError result, const AooResponse* response) {
        auto data = (RequestData *)x;
        auto client = data->client;
        auto token = data->token;
        auto& r = response->groupUpdate;

        if (result == kAooErrorNone) {
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/group/update")
                << client->node_->port() << token << (int32_t)0;
            serializeData(msg, &r.groupMetadata);
            msg << osc::EndMessage;

            client->sendReply(msg);
        } else {
            client->sendError("/aoo/client/group/update", token, result, *response);
        }

        delete data;
    };

    node_->client()->updateGroup(group, groupMetadata,
                                 cb, new RequestData { this, token });
}

void AooClient::updateUser(int token, AooId group,
                           const AooData& userMetadata) {
    auto cb = [](void* x, const AooRequest *request,
                 AooError result, const AooResponse* response) {
        auto data = (RequestData *)x;
        auto client = data->client;
        auto token = data->token;
        auto& r = response->userUpdate;

        if (result == kAooErrorNone) {
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/user/update")
                << client->node_->port() << token << (int32_t)0;
            serializeData(msg, &r.userMetadata);
            msg << osc::EndMessage;

            client->sendReply(msg);
        } else {
            client->sendError("/aoo/client/user/update", token, result, *response);
        }

        delete data;
    };

    node_->client()->updateUser(group, userMetadata,
                                cb, new RequestData { this, token });
}

// called from network thread
void AooClient::handleEvent(const AooEvent* event) {
    if (event->type == kAooEventPeerMessage) {
        handlePeerMessage(event->peerMessage);
        return;
    }

    char buf[1024];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage("/aoo/client/event") << node_->port();

    switch (event->type) {
    case kAooEventDisconnect:
    {
        msg << "disconnect" << event->disconnect.errorCode
            << event->disconnect.errorMessage;
        break;
    }
    case kAooEventPeerHandshake:
    {
        auto& e = event->peerHandshake;
        aoo::ip_address addr(e.address);
        msg << "peerHandshake" << e.groupId << e.userId
            << e.groupName << e.userName;
        break;
    }
    case kAooEventPeerTimeout:
    {
        auto& e = event->peerTimeout;
        aoo::ip_address addr(e.address);
        msg << "peerTimeout" << e.groupId << e.userId
            << e.groupName << e.userName;
        break;
    }
    case kAooEventPeerJoin:
    {
        auto& e = event->peerJoin;
        aoo::ip_address addr(e.address);
        msg << "peerJoin" << e.groupId << e.userId
            << e.groupName << e.userName << addr.name() << addr.port();
        serializeData(msg, e.metadata);
        break;
    }
    case kAooEventPeerLeave:
    {
        auto& e = event->peerLeave;
        aoo::ip_address addr(e.address);
        msg << "peerLeave" << e.groupId << e.userId
            << e.groupName << e.userName << addr.name() << addr.port();
        break;
    }
    case kAooEventPeerPing:
    {
        auto& e = event->peerPing;
        auto delta1 = aoo_ntpTimeDuration(e.t1, e.t2);
        auto delta2 = aoo_ntpTimeDuration(e.t3, e.t4);
        auto total_rtt = aoo_ntpTimeDuration(e.t1, e.t4);
        auto network_rtt = total_rtt - aoo_ntpTimeDuration(e.t2, e.t3);
        msg << "peerPing" << e.group << e.user
            << delta1 << delta2 << network_rtt << total_rtt;
        break;
    }
    case kAooEventPeerUpdate:
    {
        auto& e = event->peerUpdate;
        msg << "peerUpdate" << e.groupId << e.userId;
        serializeData(msg, &e.userMetadata);
        break;
    }
    case kAooEventGroupUpdate:
    {
        auto& e = event->groupUpdate;
        msg << "groupUpdate" << e.groupId;
        serializeData(msg, &e.groupMetadata);
        break;
    }
    default:
        LOG_DEBUG("AooClient: ignore event " << event->type);
        return; // don't send event!
    }

    msg << osc::EndMessage;

    node_->sendReply(msg);
}

void AooClient::handlePeerMessage(const AooEventPeerMessage &msg) {
    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream out(buf, sizeof(buf));
    // warp in OSC bundle to get time delta on the client side:
    bool bundle = msg.timeStamp > 0;
    if (bundle) {
        out << osc::BeginBundle(msg.timeStamp);
    }
    out << osc::BeginMessage("/aoo/client/msg")
        << node_->port() << msg.groupId << msg.userId;
    serializeData(out, &msg.data);
    out << osc::EndMessage;
    if (bundle) {
        out << osc::EndBundle;
    }

    node_->sendReply(out);
}

void AooClient::sendError(const char *cmd, AooId token, AooError error,
                          int errcode, const char *errmsg) {
    char buf[1024];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage(cmd) << node_->port() << token
        << (int32_t)error << aoo_strerror(error) << errcode << errmsg
        << osc::EndMessage;

    node_->sendReply(msg);
}

ConnectCmd::~ConnectCmd() {
    if (metadata.data) {
        RTFree(world, (void*)metadata.data);
    }
}

GroupJoinCmd::~GroupJoinCmd() {
    if (groupMetadata.data) {
        RTFree(world, (void*)groupMetadata.data);
    }
    if (userMetadata.data) {
        RTFree(world, (void*)userMetadata.data);
    }
    if (relayAddress.hostName) {
        RTFree(world, (void*)relayAddress.hostName);
    }
}

UpdateCmd::~UpdateCmd() {
    if (metadata.data) {
        RTFree(world, (void*)metadata.data);
    }
}

} // namespace sc

namespace {

template<typename T>
void doCommand(World* world, void *replyAddr, T* cmd, AsyncStageFn fn) {
    DoAsynchronousCommand(world, replyAddr, 0, cmd,
        fn, 0, 0, CmdData::free<T>, 0, 0);
}

void aoo_client_new(World* world, void* user, sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();

    auto cmdData = CmdData::create<sc::AooClientCmd>(world);
    if (cmdData) {
        cmdData->port = port;

        auto fn = [](World* world, void* data) {
            auto port = static_cast<sc::AooClientCmd*>(data)->port;
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/client/new") << port;

            try {
                auto client = createClient(port);

                msg << (int32_t)1 << osc::EndMessage;

                ::sendReply(port, msg);
            } catch (const std::exception& e) {
                msg << (int32_t)0 << e.what() << osc::EndMessage;

                ::sendReply(port, msg);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_free(World* world, void* user, sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();

    auto cmdData = CmdData::create<sc::AooClientCmd>(world);
    if (cmdData) {
        cmdData->port = port;

        auto fn = [](World * world, void* data) {
            auto port = static_cast<sc::AooClientCmd*>(data)->port;

            destroyClient(port);

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

// called from the NRT thread
std::shared_ptr<sc::AooClient> getClient(World *world, int port, int token, const char *cmd)
{
    auto client = findClient(port);
    if (client) {
        return client;
    }

    char errbuf[256];
    snprintf(errbuf, sizeof(errbuf),
        "couldn't find AooClient on port %d", port);

    if (cmd) {
        try {
            // send error reply
            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));

            msg << osc::BeginMessage(cmd) << port << token << (int32_t)0 << errbuf
                << osc::EndMessage;

            // send with temporary socket
            if (auto addr = findReplyAddr(port); addr.valid()) {
                aoo::udp_socket sock(aoo::port_tag{}, 0);
                sock.send(msg.Data(), msg.Size(), addr);
            } else {
                throw std::runtime_error("could not find reply address");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("aoo: could not send error reply: " << e.what());
        }
    } else {
        // just print error
        LOG_ERROR(errbuf);
    }
    return nullptr;
}

AooData copyMetadata(World *world, std::optional<AooData> from) {
    AooData to;
    if (from) {
        auto data = (AooByte*)RTAlloc(world, from->size);
        memcpy(data, from->data, from->size);
        to.type = from->type;
        to.size = from->size;
        to.data = data;
    } else {
        to.type = kAooDataUnspecified;
        to.data = nullptr;
        to.size = 0;
    }
    return to;
}

void aoo_client_connect(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto token = args->geti();
    auto serverHost = args->gets("");
    auto serverPort = args->geti();
    auto password = args->gets("");
    auto metadata = parseData(args);

    auto cmdData = CmdData::create<sc::ConnectCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = token;
        snprintf(cmdData->serverHost, sizeof(cmdData->serverHost),
            "%s", serverHost);
        cmdData->serverPort = serverPort;
        snprintf(cmdData->password, sizeof(cmdData->password),
            "%s", password);

        cmdData->metadata = copyMetadata(world, metadata);

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::ConnectCmd*)cmdData;
            auto client = getClient(world, data->port,
                                    data->token, "/aoo/client/connect");
            if (client) {
                client->connect(data->token, data->serverHost,
                                data->serverPort, data->password,
                                data->metadata.data ? &data->metadata : nullptr);
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
    auto token = args->geti();

    auto cmdData = CmdData::create<sc::AooClientCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = token;

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::AooClientCmd *)cmdData;
            auto client = getClient(world, data->port,
                                    data->token, "/aoo/client/disconnect");
            if (client) {
                client->disconnect(data->token);
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
    auto token = args->geti();
    auto groupName = args->gets();
    auto groupPwd = args->gets();
    auto userName = args->gets();
    auto userPwd = args->gets();
    auto groupMetadata = parseData(args);
    auto userMetadata = parseData(args);
    AooIpEndpoint relay{nullptr, 0};
    if (args->remain() > 0) {
        relay.hostName = args->gets();
        relay.port = args->geti();
    }

    auto cmdData = CmdData::create<sc::GroupJoinCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = token;
        snprintf(cmdData->groupName, sizeof(cmdData->groupName),
            "%s", groupName);
        snprintf(cmdData->groupPwd, sizeof(cmdData->groupPwd),
            "%s", groupPwd);
        snprintf(cmdData->userName, sizeof(cmdData->userName),
            "%s", userName);
        snprintf(cmdData->userPwd, sizeof(cmdData->userPwd),
            "%s", userPwd);

        cmdData->groupMetadata = copyMetadata(world, groupMetadata);

        cmdData->userMetadata = copyMetadata(world, userMetadata);

        if (relay.hostName) {
            auto len = strlen(relay.hostName);
            auto hostName = (AooChar*)RTAlloc(world, len + 1);
            memcpy(hostName, relay.hostName, len + 1);
            cmdData->relayAddress.hostName = hostName;
            cmdData->relayAddress.port = relay.port;
        } else {
            cmdData->relayAddress.hostName = nullptr;
            cmdData->relayAddress.port = 0;
        }

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::GroupJoinCmd *)cmdData;
            auto client = getClient(world, data->port,
                                    data->token, "/aoo/client/group/join");
            if (client) {
                client->joinGroup(data->token,
                                  data->groupName, data->groupPwd,
                                  data->groupMetadata.data ? &data->groupMetadata : nullptr,
                                  data->userName, data->userPwd,
                                  data->userMetadata.data ? &data->userMetadata : nullptr,
                                  data->relayAddress.hostName ? &data->relayAddress : nullptr);
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
    auto token = args->geti();
    auto group = args->geti();

    auto cmdData = CmdData::create<sc::GroupLeaveCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = token;
        cmdData->group = group;

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::GroupLeaveCmd *)cmdData;
            auto client = getClient(world, data->port,
                                    data->token, "/aoo/client/group/leave");
            if (client) {
                client->leaveGroup(data->token, data->group);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_group_update(World* world, void* user,
                             sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto token = args->geti();
    auto groupID = args->geti();
    auto groupMetadata = parseData(args);

    auto cmdData = CmdData::create<sc::UpdateCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = token;
        cmdData->groupID = groupID;
        cmdData->metadata = copyMetadata(world, groupMetadata);

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::UpdateCmd *)cmdData;
            auto client = getClient(world, data->port,
                                    data->token, "/aoo/client/group/update");
            if (client) {
                client->updateGroup(data->token, data->groupID, data->metadata);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_user_update(World* world, void* user,
                            sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto token = args->geti();
    auto groupID = args->geti();
    auto userMetadata = parseData(args);

    auto cmdData = CmdData::create<sc::UpdateCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = token;
        cmdData->groupID = groupID;
        cmdData->metadata = copyMetadata(world, userMetadata);

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::UpdateCmd *)cmdData;
            auto client = getClient(world, data->port,
                                    data->token, "/aoo/client/user/update");
            if (client) {
                client->updateUser(data->token, data->groupID, data->metadata);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_ping(World* world, void* user,
                     sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto seconds = args->getf();

    auto cmdData = CmdData::create<sc::ControlCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = -1;
        cmdData->f = seconds;

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::ControlCmd *)cmdData;
            auto client = getClient(world, data->port, 0, nullptr);
            if (client) {
                client->setPingInterval(data->f);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_packet_size(World* world, void* user,
                            sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto size = args->geti();

    auto cmdData = CmdData::create<sc::ControlCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = -1;
        cmdData->i = size;

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::ControlCmd *)cmdData;
            auto client = getClient(world, data->port, 0, nullptr);
            if (client) {
                client->setPacketSize(data->i);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_sim_packet_loss(World* world, void* user,
                                sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto f = args->getf();

    auto cmdData = CmdData::create<sc::ControlCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = -1;
        cmdData->f = f;

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::ControlCmd *)cmdData;
            auto client = getClient(world, data->port, 0, nullptr);
            if (client) {
                client->setSimulatePacketLoss(data->f);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_sim_packet_reorder(World* world, void* user,
                                   sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto f = args->getf();

    auto cmdData = CmdData::create<sc::ControlCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = -1;
        cmdData->f = f;

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::ControlCmd *)cmdData;
            auto client = getClient(world, data->port, 0, nullptr);
            if (client) {
                client->setSimulatePacketReorder(data->f);
            }

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

void aoo_client_sim_packet_jitter(World* world, void* user,
                                  sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto i = args->geti();

    auto cmdData = CmdData::create<sc::ControlCmd>(world);
    if (cmdData) {
        cmdData->world = world;
        cmdData->port = port;
        cmdData->token = -1;
        cmdData->i = i;

        auto fn = [](World * world, void* cmdData) {
            auto data = (sc::ControlCmd *)cmdData;
            auto client = getClient(world, data->port, 0, nullptr);
            if (client) {
                client->setSimulatePacketJitter(data->i);
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
    AooPluginCmd(aoo_client_group_update);
    AooPluginCmd(aoo_client_user_update);
    AooPluginCmd(aoo_client_packet_size);
    AooPluginCmd(aoo_client_ping);
    // internal commands for network simulation
    AooPluginCmd(aoo_client_sim_packet_loss);
    AooPluginCmd(aoo_client_sim_packet_reorder);
    AooPluginCmd(aoo_client_sim_packet_jitter);
}
