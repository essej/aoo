#include "AooServer.hpp"

#include <unordered_map>
#include <stdexcept>

namespace {

InterfaceTable *ft;

// NB: only one World is ever accessing a given server;
// we only need to protect the dictionary itself for concurrent
// insertations/deletations.
aoo::sync::mutex gServerMutex;
std::unordered_map<int, std::shared_ptr<sc::AooServer>> gServerMap;

// called from NRT thread(s). Throws on failure!
std::shared_ptr<sc::AooServer> createServer(int port, const char *password,
                                            bool relay) {
    aoo::sync::scoped_lock lock(gServerMutex);
    if (gServerMap.count(port)) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "server on port %d already exists", port);
        throw std::runtime_error(buf);
    }
    auto server = std::make_shared<sc::AooServer>(port, password, relay);
    gServerMap[port] = server;
    return server;
}

// called from NRT thread(s)
bool destroyServer(int port) {
    aoo::sync::scoped_lock lock(gServerMutex);
    if (auto it = gServerMap.find(port); it != gServerMap.end()) {
        gServerMap.erase(it);
        return true;
    } else {
        LOG_ERROR("aoo: cannot free client - not found!");
        return false;
    }
}

#if 0

std::shared_ptr<sc::AooServer> findServer(int port) {
    aoo::sync::scoped_shared_lock lock(gServerMutex);
    auto it = gServerMap.find(port);
    if (it != gServerMap.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

#endif

} // namespace

namespace sc {

// called in NRT thread
AooServer::AooServer(int port, const char *password, bool relay)
    : port_(port)
{
    auto server = ::AooServer::create(); // does not really fail

    // first set event handler!
    server->setEventHandler([](void *x, const AooEvent *e, AooThreadLevel) {
        static_cast<sc::AooServer *>(x)->handleEvent(e);
    }, this, kAooEventModeCallback);

    if (password) {
        server->setPassword(password);
    }

    server->setUseInternalRelay(relay);

    // setup server
    AooServerSettings settings;
    settings.portNumber = port;
    if (auto err = server->setup(settings); err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        throw std::runtime_error(msg);
    }

    server_ = std::move(server);

    // finally start server threads
    thread_ = std::thread([this]() {
        aoo::sync::set_low_realtime_priority();
        run();
    });

    udp_thread_ = std::thread([this]() {
        aoo::sync::set_low_realtime_priority();
        receive();
    });

    LOG_INFO("AooServer: listening on port " << port);
}

AooServer::~AooServer() {
    server_->stop();
    if (thread_.joinable()) {
        thread_.join();
    }
    if (udp_thread_.joinable()) {
        udp_thread_.join();
    }
}

void AooServer::handleEvent(const AooEvent *event){
    char buf[1024];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage("/aoo/server/event") << port_;

    switch (event->type) {
    case kAooEventClientLogin:
    {
        auto& e = event->clientLogin;
        if (e.error == kAooOk) {
            // TODO: socket address
            msg << "clientAdd" << e.id << e.version;
            serializeData(msg, e.metadata);
        } else {
            LOG_WARNING("AooServer: client " << e.id << " failed to login: "
                        << aoo_strerror(e.error));
        }
        break;
    }
    case kAooEventClientLogout:
    {
        auto& e = event->clientLogout;
        msg << "clientRemove" << e.id << e.errorCode << e.errorMessage;
        break;
    }
    case kAooEventGroupAdd:
    {
        auto& e = event->groupAdd;
        msg << "groupAdd" << e.id << e.name;
        serializeData(msg, e.metadata);
        break;
    }
    case kAooEventGroupRemove:
    {
        auto& e = event->groupRemove;
        msg << "groupRemove" << e.id << e.name;
        break;
    }
    case kAooEventGroupJoin:
    {
        auto& e = event->groupJoin;
        msg << "groupJoin" << e.groupId << e.userId
            << e.groupName << e.userName << e.clientId;
        serializeData(msg, e.userMetadata);
        break;
    }
    case kAooEventGroupLeave:
    {
        auto& e = event->groupLeave;
        msg << "groupLeave" << e.groupId << e.userId
            << e.groupName << e.userName;
        break;
    }
    default:
        LOG_DEBUG("AooServer: got unknown event " << event->type);
        return; // don't send event!
    }

    msg << osc::EndMessage;

    ::sendReply(port_, msg);
}

void AooServer::run() {
    auto err = server_->run(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        LOG_ERROR("AooServer: server error: " << msg);
        // TODO: handle error
    }
}

void AooServer::receive() {
    auto err = server_->receive(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        LOG_ERROR("AooServer: UDP error: " << msg);
        // TODO: handle error
    }
}

} // sc

namespace {

template<typename T>
void doCommand(World* world, void *replyAddr, T* cmd, AsyncStageFn fn) {
    DoAsynchronousCommand(world, replyAddr, 0, cmd,
        fn, 0, 0, CmdData::free<T>, 0, 0);
}

void aoo_server_new(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();
    auto pwd = args->gets("");
    auto relay = (bool)args->geti();

    auto cmdData = CmdData::create<sc::AooServerCreateCmd>(world);
    if (cmdData) {
        cmdData->port = port;
        cmdData->relay = relay;
        snprintf(cmdData->password, sizeof(cmdData->password), "%s", pwd);

        auto fn = [](World* world, void* x) {
            auto data = (sc::AooServerCreateCmd *)x;
            auto port = data->port;
            auto pwd = data->password;
            auto relay = data->relay;

            char buf[1024];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage("/aoo/server/new") << port;

            try {
                auto server = createServer(port, pwd, relay);

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

void aoo_server_free(World* world, void* user,
    sc_msg_iter* args, void* replyAddr)
{
    auto port = args->geti();

    auto cmdData = CmdData::create<sc::AooServerCmd>(world);
    if (cmdData) {
        cmdData->port = port;

        auto fn = [](World * world, void* data) {
            auto port = static_cast<sc::AooServerCmd*>(data)->port;

            destroyServer(port);

            return false; // done
        };

        doCommand(world, replyAddr, cmdData, fn);
    }
}

} // namespace

/*////////////// Setup /////////////////*/

void AooServerLoad(InterfaceTable *inTable){
    ft = inTable;

    AooPluginCmd(aoo_server_new);
    AooPluginCmd(aoo_server_free);
}
