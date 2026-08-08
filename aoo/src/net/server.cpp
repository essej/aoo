/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "server.hpp"
#include "server_events.hpp"

#include <functional>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>

#include "../binmsg.hpp"

namespace {

constexpr const char *legacy_empty_password = "D41D8CD98F00B204E9800998ECF8427E";
constexpr const char *legacy_group_public = "/group/public";
constexpr const char *legacy_group_public_add = "/aoo/client/group/public/add";
constexpr const char *legacy_group_public_del = "/aoo/client/group/public/del";
constexpr const char *legacy_client_group_public = "/aoo/client/group/public";
constexpr const char *legacy_request = "/request";
constexpr const char *legacy_client_reply = "/aoo/client/reply";
constexpr size_t external_udp_socket_index = std::numeric_limits<size_t>::max();

std::string normalize_legacy_password(const char *password) {
    if (!password || !strcmp(password, legacy_empty_password)) {
        return {};
    }
    return password;
}

bool json_bool(const AooData& data, std::string_view key, bool& value) {
    if (data.type != kAooDataJSON || !data.data || data.size <= 0) {
        return false;
    }

    std::string needle;
    needle.reserve(key.size() + 2);
    needle.push_back('"');
    needle.append(key);
    needle.push_back('"');

    std::string_view json((const char *)data.data, data.size);
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        return false;
    }
    pos += needle.size();
    while (pos < json.size() && std::isspace((unsigned char)json[pos])) {
        ++pos;
    }
    if (pos >= json.size() || json[pos++] != ':') {
        return false;
    }
    while (pos < json.size() && std::isspace((unsigned char)json[pos])) {
        ++pos;
    }
    if (json.substr(pos, 4) == "true") {
        value = true;
        return true;
    }
    if (json.substr(pos, 5) == "false") {
        value = false;
        return true;
    }
    return false;
}

bool is_sonobus_public_group(const AooData *metadata) {
    bool value = false;
    return metadata && (json_bool(*metadata, "isPublic", value)
                        || json_bool(*metadata, "public", value)) && value;
}

bool read_sonobus_public_subscription(const AooData& data, bool& value) {
    if (data.type != kAooDataJSON || !data.data || data.size <= 0) {
        return false;
    }
    std::string_view json((const char *)data.data, data.size);
    if (json.find("public_group_subscribe") == std::string_view::npos) {
        return false;
    }
    return json_bool(data, "subscribe", value)
            || json_bool(data, "watch", value)
            || json_bool(data, "enabled", value);
}

void append_json_string(std::string& output, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (unsigned char c : value) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (c < 0x20) {
                output += "\\u00";
                output.push_back(hex[c >> 4]);
                output.push_back(hex[c & 0x0f]);
            } else {
                output.push_back((char)c);
            }
        }
    }
    output.push_back('"');
}

std::string make_public_group_json(const aoo::net::group& grp, bool removed) {
    std::string result = "{\"groupId\":";
    result += std::to_string(grp.id());
    result += ",\"groupName\":";
    append_json_string(result, grp.name());
    result += ",\"removed\":";
    result += removed ? "true" : "false";
    result += ",\"type\":\"public_group_update\",\"users\":[";
    bool first = true;
    if (!removed) {
        for (auto& usr : grp.users()) {
            if (!usr.active()) {
                continue;
            }
            if (!first) {
                result.push_back(',');
            }
            append_json_string(result, usr.name());
            first = false;
        }
    }
    result += "]}";
    return result;
}

} // namespace

//----------------------- Server --------------------------//

AOO_API AooServer * AOO_CALL AooServer_new(void) {
    try {
        return aoo::construct<aoo::net::Server>();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

aoo::net::Server::Server() {
    sendbuffer_.resize(AOO_MAX_PACKET_SIZE);
}

AOO_API void AOO_CALL AooServer_free(AooServer *server){
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::net::Server *>(server));
}

aoo::net::Server::~Server() {}

AOO_API AooError AOO_CALL AooServer_setup(
    AooServer *server, AooServerSettings *settings)
{
    if (settings == nullptr) {
        return kAooErrorBadArgument;
    }
    return server->setup(*settings);
}

AooError AOO_CALL aoo::net::Server::setup(AooServerSettings& settings) {
    if (settings.portNumber == 0) {
        return kAooErrorBadArgument;
    }
    // NB: socketType will be modified!
    auto& type = settings.socketType;
    if ((type & kAooSocketIPv4Mapped) &&
            (!(type & kAooSocketIPv6) || (type & kAooSocketIPv4))) {
        LOG_ERROR("AooServer: combination of setup flags not allowed");
        return kAooErrorBadArgument;
    }

    bool external = settings.options & kAooServerExternalUDPSocket;
    external_udp_socket_ = external;
    force_legacy_protocol_ = settings.options & kAooServerForceLegacyProtocol;
    int legacy_port = AOO_CHECK_FIELD(&settings, AooServerSettings, legacyPortNumber)
            ? settings.legacyPortNumber : 0;
    if (external && legacy_port != 0) {
        LOG_ERROR("AooServer: legacyPortNumber requires internal UDP sockets");
        return kAooErrorBadArgument;
    }
    std::vector<int> ports { settings.portNumber };
    if (legacy_port != 0 && legacy_port != settings.portNumber) {
        ports.push_back(legacy_port);
    }
    if (external && (type == 0)) {
        // external UDP socket needs IP flags
        return kAooErrorBadArgument;
    } else if (type == 0) {
        type = kAooSocketDualStack; // default
    }

    if (external) {
        if (settings.sendFunc) {
            udp_sendfn_ = sendfn(settings.sendFunc, settings.userData);
        } else {
            return kAooErrorBadArgument;
        }
    } else {
        udp_sendfn_ = sendfn(Server::send, this);
    }

    // in case run() has been called in non-blocking mode
    close();

    // TODO: honor flags for UDP and TCP sockets! For now just use default.
    if (!external) {
        try {
            // TODO: settings
            udp_server_.start(ports,
                [this](auto... args) { handle_udp_packet(args...);
            });
        } catch (const aoo::udp_error& e) {
            LOG_ERROR("AooServer: failed to start UDP server: " << e.what());
            aoo::socket::set_last_error(e.code());
            return kAooErrorSocket;
        }
        // update socket flags
        type = udp_server_.socket().flags();
    }

    try {
        tcp_server_.start(ports,
            [this](auto... args) { return accept_client(args...); },
            [this](auto... args) { handle_client_data(args...); });
    } catch (const aoo::tcp_error& e) {
        LOG_ERROR("AooServer: failed to start TCP server: " << e.what());
        aoo::socket::set_last_error(e.code());
        return kAooErrorSocket;
    }

    if (type & kAooSocketIPv6) {
        if (type & kAooSocketIPv4) {
            address_family_ = ip_address::Unspec; // both IPv6 and IPv4
        } else {
            address_family_ = ip_address::IPv6;
        }
    } else {
        address_family_ = ip_address::IPv4;
    }

    use_ipv4_mapped_ = type & kAooSocketIPv4Mapped;

    port_ = settings.portNumber;

    return kAooOk;
}

AOO_API AooError AOO_CALL AooServer_run(
    AooServer *server, AooSeconds timeout)
{
    return server->run(timeout);
}

AooError AOO_CALL aoo::net::Server::run(AooSeconds timeout) {
    try {
        std::vector<AooId> client_timeouts;
        double remaining = timeout; // only used if timeout >= 0

        while  (tcp_server_.running()) {
            double sleep = 1e12;

            // first check client timers
            {
                auto now = aoo::time_tag::now();

                settings_lock_.lock();
                auto settings = ping_settings_;
                settings_lock_.unlock();

                sync::scoped_lock lock(mutex_); // writer lock to protect client list
                for (auto& [id, client] : clients_) {
                    auto [timeout, wait] = client.update(*this, now, settings);
                    if (timeout) {
                        LOG_INFO("AooServer: client " << id << " not responding");
                        client_timeouts.push_back(id);
                    } else if (wait < sleep) {
                        sleep = wait;
                    }
                }

                // remove timed-out clients
                for (auto& id : client_timeouts) {
                    remove_client(id, kAooErrorNotResponding, "client is not responding");
                    // remove from TCP server!
                    tcp_server_.close(id);
                }
                client_timeouts.clear();
            }

            // check and dispatch messages
            message_queue_.consume_all([this](const auto& msg) {
                dispatch_message(msg);
            });

            // finally wait for network events (with timeout)
            // NB: the TCP handler methods will lock the mutex to
            // prevent concurrent access from API methods.
            // The run() method itself must only be called after
            // the setup() method, so there is no race condition
            // regarding the TCP server itself.
            if (timeout >= 0) {
                sleep = std::min<double>(sleep, timeout);
                if (tcp_server_.run(sleep)) {
                    return kAooOk; // did something
                } else {
                    // wait some more
                    remaining -= sleep;
                    if (remaining <= 0) {
                        return kAooErrorWouldBlock;
                    }
                    // continue
                }
            } else {
                tcp_server_.run(sleep);
            }
        }

        // NB: in non-blocking mode, close() will be called in setup()!
        if (timeout < 0) {
            sync::scoped_lock lock(mutex_); // writer lock to protect client list
            close();
        }

        return kAooOk;
    }  catch (const aoo::tcp_error& e) {
        LOG_ERROR("AooServer: TCP server failed: " << e.what());
        aoo::socket::set_last_error(e.code());

        return kAooErrorSocket;
    } catch (const std::exception& e) {
        LOG_ERROR("AooServer: unhandled exception in run(): " << e.what());

        return kAooErrorInternal;
    }
}

AOO_API AooError AOO_CALL AooServer_receive(
    AooServer *server, AooSeconds timeout)
{
    return server->receive(timeout);
}

AooError AOO_CALL aoo::net::Server::receive(AooSeconds timeout) {
    try {
        if (timeout >= 0) {
            return udp_server_.run(timeout) ? kAooOk : kAooErrorWouldBlock;
        } else {
            udp_server_.run();
            return kAooOk;
        }
    } catch (const aoo::udp_error& e) {
        LOG_ERROR("AooServer: UDP server error: " << e.what());
        aoo::socket::set_last_error(e.code());
        return kAooErrorSocket;
    } catch (const std::exception& e) {
        LOG_ERROR("AooServer: unhandled exception in run(): " << e.what());

        return kAooErrorInternal;
    }
}

AOO_API AooError AOO_CALL Server_handlePacket(
    AooServer *server, const AooByte *data, AooInt32 size,
    const void *address, AooAddrSize addrlen)
{
    return server->handlePacket(data, size, address, addrlen);
}

AooError AOO_CALL aoo::net::Server::handlePacket(
    const AooByte *data, AooInt32 size,
    const void *address, AooAddrSize addrlen)
{
    aoo::ip_address addr((struct sockaddr *)address, addrlen);
    handle_udp_packet(data, size, addr, external_udp_socket_index);
    return kAooOk;
}

AOO_API AooError AOO_CALL AooServer_stop(AooServer *server)
{
    return server->stop();
}

AooError AOO_CALL aoo::net::Server::stop() {
    tcp_server_.stop();
    udp_server_.stop();
    return kAooOk;
}

AOO_API AooError AOO_CALL AooServer_setEventHandler(
    AooServer *server, AooEventHandler fn, void *user, AooEventMode mode) {
    return server->setEventHandler(fn, user, mode);
}

AooError AOO_CALL aoo::net::Server::setEventHandler(
    AooEventHandler fn, void *user, AooEventMode mode) {
    event_handler_ = fn;
    event_context_ = user;
    event_mode_ = mode;
    return kAooOk;
}

AOO_API AooBool AOO_CALL AooServer_eventsAvailable(AooServer *server) {
    return server->eventsAvailable();
}

AooBool AOO_CALL aoo::net::Server::eventsAvailable(){
    return !event_queue_.empty();
}

AOO_API AooError AOO_CALL AooServer_pollEvents(AooServer *server) {
    return server->pollEvents();
}

AooError AOO_CALL aoo::net::Server::pollEvents(){
    // always thread-safe
    event_handler fn(event_handler_, event_context_, kAooThreadLevelUnknown);
    event_ptr e;
    while (event_queue_.pop(e)){
        e->dispatch(fn);
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooServer_setRequestHandler(
        AooServer *server, AooRequestHandler cb, void *user, AooFlag flags) {
    return server->setRequestHandler(cb, user, flags);
}

AooError AOO_CALL aoo::net::Server::setRequestHandler(
        AooRequestHandler cb, void *user, AooFlag flags) {
    request_handler_ = cb;
    request_context_ = user;
    return kAooOk;
}

AOO_API AooError AooServer_handleRequest(
        AooServer *server, AooId client, AooId token, const AooRequest *request,
        AooError result, AooResponse *response)
{
    return server->handleRequest(client, token, request, result, response);
}

// NB: might be called outside the TCP event loop (asynchronous request handling)
AooError AOO_CALL aoo::net::Server::handleRequest(
        AooId client, AooId token, const AooRequest *request,
        AooError result, AooResponse *response)
{
    if (!request) {
        return kAooErrorBadArgument;
    }

    sync::scoped_lock lock(mutex_); // writer lock!

    auto c = find_client(client);
    if (!c) {
        return kAooErrorNotFound;
    }

    if (result == kAooErrorNone) {
        // request accepted
        // every request needs a response
        if (!response) {
            return kAooErrorBadArgument;
        }
        // just make sure that the response matches the request
        if (response->type != request->type) {
            return kAooErrorBadArgument;
        }

        switch (request->type) {
        case kAooRequestLogin:
            return do_login(*c, token, request->login, response->login);
        case kAooRequestGroupJoin:
            return do_group_join(*c, token, request->groupJoin, response->groupJoin);
        case kAooRequestGroupLeave:
            return do_group_leave(*c, token, request->groupLeave, response->groupLeave);
        case kAooRequestGroupUpdate:
            return do_group_update(*c, token, request->groupUpdate, response->groupUpdate);
        case kAooRequestUserUpdate:
            return do_user_update(*c, token, request->userUpdate, response->userUpdate);
        case kAooRequestCustom:
            return do_custom_request(*c, token, request->custom, response->custom);
        default:
            return kAooErrorNotImplemented;
        }
    } else {
        // request denied
        // response must be either AooRequestError or NULL
        if (response && response->type != kAooRequestError) {
            return kAooErrorBadArgument;
        }
        c->send_error(*this, token, request->type, result,
                      (const AooResponseError *)response);
    #if 1
        if (request->type == kAooRequestLogin) {
            // send event
            auto e = std::make_unique<client_login_event>(*c, result);
            send_event(std::move(e));
        }
    #endif

        return kAooOk;
    }
}

AOO_API AooError AOO_CALL AooServer_notifyClient(
        AooServer *server, AooId client, const AooData *data) {
    return server->notifyClient(client, *data);
}

AooError AOO_CALL aoo::net::Server::notifyClient(
        AooId client, const AooData &data) {
    sync::scoped_shared_lock lock(mutex_); // reader lock

    if (client == kAooIdInvalid || find_client(client)) {
        push_message(kAooIdInvalid, client, data);
        return kAooOk;
    } else {
        LOG_ERROR("AooServer: notifyClient: can't find client " << client << "!");
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooServer_notifyGroup(
        AooServer *server, AooId group, AooId user, const AooData *data) {
    return server->notifyGroup(group, user, *data);
}

AooError AOO_CALL aoo::net::Server::notifyGroup(
        AooId group, AooId user, const AooData &data) {
    sync::scoped_shared_lock lock(mutex_); // reader lock

    if (auto g = find_group(group)) {
        if (user != kAooIdInvalid && !g->find_user(user)) {
            LOG_ERROR("AooServer: notifyGroup: can't find user "
                      << user << " in group " << group);
            return kAooErrorNotFound;
        }

        push_message(group, user, data);

        return kAooOk;
    } else {
        LOG_ERROR("AooServer: notifyGroup: can't find group " << group);
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooServer_findGroup(
        AooServer *server, const AooChar *name, AooId *id) {
    return server->findGroup(name, id);
}

AooError AOO_CALL aoo::net::Server::findGroup(
        const AooChar *name, AooId *id) {
    sync::scoped_shared_lock lock(mutex_); // reader lock

    if (auto grp = find_group(name)) {
        if (id) {
            *id = grp->id();
        }
        return kAooOk;
    } else {
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooServer_addGroup(
        AooServer *server, const AooChar *name, const AooChar *password,
        const AooData *metadata, const AooIpEndpoint *relayAddress, AooFlag flags, AooId *groupId) {
    return server->addGroup(name, password, metadata, relayAddress, flags, groupId);
}

AooError AOO_CALL aoo::net::Server::addGroup(
        const AooChar *name, const AooChar *password, const AooData *metadata,
        const AooIpEndpoint *relayAddress, AooFlag flags, AooId *groupId) {
    sync::scoped_lock lock(mutex_); // writer lock

    // this might "waste" a group ID, but we don't care.
    auto id = get_next_group_id();
    std::string hashed_pwd = password ? aoo::net::encrypt(password) : "";
    group grp(name, hashed_pwd, id, metadata, relayAddress, kAooGroupPersistent);
    if (add_group(std::move(grp))) {
        if (groupId) {
            *groupId = id;
        }
        return kAooOk;
    } else {
        LOG_ERROR("AooServer: addGroup: group " << name << " already exists");
        return kAooErrorAlreadyExists;
    }
}

AOO_API AooError AOO_CALL AooServer_removeGroup(
        AooServer *server, AooId group) {
    return server->removeGroup(group);
}

AooError AOO_CALL aoo::net::Server::removeGroup(AooId group) {
    sync::scoped_lock lock(mutex_); // writer lock

    if (remove_group(group)) {
        return kAooOk;
    } else {
        LOG_ERROR("AooServer: removeGroup: group " << group << " not found");
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooServer_findUserInGroup(
        AooServer *server, AooId group, const AooChar *userName, AooId *userId) {
    return server->findUserInGroup(group, userName, userId);
}

AooError AOO_CALL aoo::net::Server::findUserInGroup(
        AooId group, const AooChar *userName, AooId *userId) {
    sync::scoped_shared_lock lock(mutex_); // reader lock

    if (auto grp = find_group(group)) {
        if (auto usr = grp->find_user(userName)) {
            if (userId) {
                *userId = usr->id();
            }
            return kAooOk;
        }
    }
    return kAooErrorNotFound;
}

AOO_API AooError AOO_CALL AooServer_addUserToGroup(
        AooServer *server, AooId group,
        const AooChar *userName, const AooChar *userPwd,
        const AooData *metadata, AooFlag flags, AooId *userId) {
    return server->addUserToGroup(group, userName, userPwd, metadata, flags, userId);
}

AooError AOO_CALL aoo::net::Server::addUserToGroup(
        AooId group, const AooChar *userName, const AooChar *userPwd,
        const AooData *metadata, AooFlag flags, AooId *userId) {
    sync::scoped_lock lock(mutex_); // writer lock

    if (auto g = find_group(group)) {
        auto id = g->get_next_user_id();
        std::string hashed_pwd = userPwd ? aoo::net::encrypt(userPwd) : "";
        auto usr = user(userName, hashed_pwd, id, g->id(), kAooIdInvalid,
                        metadata, nullptr, kAooUserPersistent);
        if (g->add_user(std::move(usr))) {
            if (userId) {
                *userId = id;
            }
            return kAooOk;
        } else {
            LOG_ERROR("AooServer: addUserToGroup: user " << userName
                      << " already exists in group " << group);
            return kAooErrorAlreadyExists;
        }
    } else {
        LOG_ERROR("AooServer: addUserToGroup: group " << group << " not found");
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooServer_removeUserFromGroup(
        AooServer *server, AooId group, AooId user) {
    return server->removeUserFromGroup(group, user);
}

AooError AOO_CALL aoo::net::Server::removeUserFromGroup(
        AooId group, AooId user) {
    sync::scoped_lock lock(mutex_); // writer lock

    if (auto grp = find_group(group)) {
        if (auto usr = grp->find_user(user)) {
            if (usr->active()) {
                // tell client that it has been kicked out of the group
                if (auto client = find_client(*usr)) {
                    client->on_group_leave(*this, *grp, *usr, true);
                } else {
                    LOG_ERROR("AooServer: removeUserFromGroup: can't find client for user " << *usr);
                }
                // notify peers
                on_user_left_group(*grp, *usr);
            }
            do_remove_user_from_group(*grp, *usr);
            return kAooOk;
        } else {
            LOG_ERROR("AooServer: removeUserToGroup: user "
                      << user << " not found  in group " << group);
        }
    } else {
        LOG_ERROR("AooServer: removeUserToGroup: group " << group << " not found");
    }
    return kAooErrorNotFound;
}

AOO_API AooError AOO_CALL AooServer_groupControl(
        AooServer *server, AooId group, AooCtl ctl,
        AooIntPtr index, void *data, AooSize size) {
    return server->groupControl(group, ctl, index, data, size);
}

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

#define CHECKARG(type) assert(size == sizeof(type))

AooError AOO_CALL aoo::net::Server ::groupControl(
        AooId group, AooCtl ctl, AooIntPtr index,
        void *ptr, AooSize size) {
    // for simplicity, always take a writer lock.
    // LATER separate between read and write options
    sync::scoped_lock lock(mutex_);

    auto grp = find_group(group);
    if (!grp) {
        LOG_ERROR("AooServer: could not find group " << group);
        return kAooErrorNotFound;
    }

    switch (ctl) {
    case kAooCtlUpdateGroup:
    {
        CHECKARG(AooData*);
        auto md = as<const AooData*>(ptr);
        if (md) {
            update_group(*grp, *md);
        } else {
            return kAooErrorBadArgument;
        }
        break;
    }
    case kAooCtlUpdateUser:
    {
        auto usr = grp->find_user(index);
        if (!usr) {
            LOG_ERROR("AooServer: could not find user "
                      << index << " in group " << group);
            return kAooErrorNotFound;
        }
        CHECKARG(AooData*);
        auto md = as<const AooData*>(ptr);
        if (md) {
            update_user(*grp, *usr, *md);
        } else {
            return kAooErrorBadArgument;
        }
        break;
    }
    default:
        LOG_WARNING("AooServer: unsupported group control " << ctl);
        return kAooErrorNotImplemented;
    }

    return kAooOk;
}

AOO_API AooError AOO_CALL AooServer_control(
        AooServer *server, AooCtl ctl,
        AooIntPtr index, void *data, AooSize size)
{
    return server->control(ctl, index, data, size);
}

AooError AOO_CALL aoo::net::Server::control(
        AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    switch (ctl) {
    case kAooCtlSetPassword:
    {
        sync::scoped_lock lock(mutex_); // writer lock
        auto pwd = reinterpret_cast<const AooChar *>(index);
        if (pwd) {
            password_ = encrypt(pwd);
        } else {
            password_ = "";
        }
        break;
    }
    case kAooCtlSetRelayHost:
    {
        sync::scoped_lock lock(mutex_); // writer lock
        auto ep = reinterpret_cast<const AooIpEndpoint *>(index);
        if (ep) {
            global_relay_addr_ = ip_host(*ep);
        } else {
            global_relay_addr_ = ip_host{};
        }
        break;
    }
    case kAooCtlSetUseInternalRelay:
        CHECKARG(AooBool);
        internal_relay_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetUseInternalRelay:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = internal_relay_.load();
        break;
    case kAooCtlSetGroupAutoCreate:
        CHECKARG(AooBool);
        group_auto_create_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetGroupAutoCreate:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = group_auto_create_.load();
        break;
    case kAooCtlSetPingSettings:
        CHECKARG(AooPingSettings);
        settings_lock_.lock();
        ping_settings_ = as<AooPingSettings>(ptr);
        settings_lock_.unlock();
        break;
    case kAooCtlGetPingSettings:
        CHECKARG(AooPingSettings);
        settings_lock_.lock();
        as<AooPingSettings>(ptr) = ping_settings_;
        settings_lock_.unlock();
        break;
    default:
        LOG_WARNING("AooServer: unsupported control " << ctl);
        return kAooErrorNotImplemented;
    }
    return kAooOk;
}

namespace aoo {
namespace net {

client_endpoint * Server::find_client(AooId id) {
    auto it = clients_.find(id);
    if (it != clients_.end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

client_endpoint * Server::find_client(const ip_address& addr) {
    for (auto& [_, client] : clients_) {
        if (client.match(addr)) {
            return &client;
        }
    }
    return nullptr;
}

group* Server::add_group(group&& grp) {
    if (!find_group(grp.name())) {
        auto [it, success] = groups_.emplace(grp.id(), std::move(grp));
        if (success) {
            return &it->second;
        }
    }
    return nullptr;
}

group* Server::find_group(AooId id) {
    auto it = groups_.find(id);
    if (it != groups_.end()) {
        return &it->second;
    } else {
        return nullptr;
    }
}

group* Server::find_group(std::string_view name) {
    for (auto& [_, grp] : groups_) {
        if (grp.name() == name) {
            return &grp;
        }
    }
    return nullptr;
}

bool Server::remove_group(AooId id) {
    auto it = groups_.find(id);
    if (it == groups_.end()) {
        return false;
    }
    // if the group has been removed manually, it might still
    // contain users, so we have to notify them!
    auto& grp = it->second;
    for (auto& usr : grp.users()) {
        if (auto client = find_client(usr)) {
            client->on_group_leave(*this, grp, usr, true);
        } else {
            LOG_ERROR("AooServer: remove_group: can't find client for user " << usr);
        }
    }
    if (grp.is_public()) {
        notify_public_group(grp, true);
    }
    groups_.erase(it);
    return true;
}

void Server::update_group(group& grp, const AooData& md) {
    LOG_DEBUG("AooServer: update group " << grp);

    grp.set_metadata(md);

    for (auto& usr : grp.users()) {
        auto client = find_client(usr.client());
        if (client) {
            // kAooIdInvalid -> updated on the server
            client->send_group_update(*this, grp, kAooIdInvalid);
        } else {
            LOG_ERROR("AooServer: could not find client for user " << usr);
        }
    }
}

void Server::update_user(const group& grp, user& usr, const AooData& md) {
    LOG_DEBUG("AooServer: update user " << usr << " in group " << grp);

    usr.set_metadata(md);

    for (auto& member : grp.users()) {
        auto client = find_client(member.client());
        if (client) {
            if (member.id() == usr.id()) {
                client->send_user_update(*this, member);
            } else {
                client->send_peer_update(*this, member);
            }
        } else {
            LOG_ERROR("AooServer: could not find client for user " << usr);
        }
    }
}

void Server::on_user_joined_group(const group& grp, const user& usr,
                                  const client_endpoint& client) {
    LOG_DEBUG("AooServer: user " << usr << " joined group " << grp);
    // 1) send the new member to existing group members
    // 2) send existing group members to the new member
    for (auto& peer : grp.users()) {
        if ((peer.id() != usr.id()) && peer.active()) {
            if (auto other = find_client(peer)) {
                // notify new member
                client.send_peer_join(*this, grp, peer, *other);
                // notify existing member
                other->send_peer_join(*this, grp, usr, client);
            } else {
                LOG_ERROR("AooServer: user_joined_group: can't find client for peer " << peer);
            }
        }
    }

    // send event
    auto e = std::make_unique<group_join_event>(grp, usr);
    send_event(std::move(e));
}

void Server::on_user_left_group(const group& grp, const user& usr) {
    LOG_DEBUG("AooServer: user " << usr << " left group " << grp);
    // notify peers
    for (auto& peer : grp.users()) {
        if (peer.id() != usr.id()) {
            if (auto other = find_client(peer)) {
                other->send_peer_leave(*this, grp, usr);
            } else {
                LOG_ERROR("AooServer: user_left_group: can't find client for peer " << peer);
            }
        }
    }

    // send event
    auto e = std::make_unique<group_leave_event>(grp, usr);
    send_event(std::move(e));
}

void Server::do_remove_user_from_group(group& grp, user& usr) {
    const bool persistent = usr.persistent();
    if (persistent) {
        // just unset
        usr.unset();
    } else {
        // remove from group
        if (!grp.remove_user(usr.id())) {
            LOG_ERROR("AooServer: can't remove user " << usr << " from group " << grp);
        }
    }

    if (grp.is_public()) {
        notify_public_group(grp);
    }

    if (!persistent) {
        // remove group if empty and not persistent
        if (!grp.persistent() && !grp.user_count()) {
            // send event
            auto e = std::make_unique<group_remove_event>(grp);
            send_event(std::move(e));

            // finally remove it
            remove_group(grp.id());
        }
    }
}

void Server::send_public_group(client_endpoint& client, const group& grp,
                               bool removed) {
    if (!client.watches_public_groups()) {
        return;
    }

    if (client.protocol() == wire_protocol::legacy) {
        auto msg = start_message();
        if (removed) {
            msg << osc::BeginMessage(legacy_group_public_del)
                << grp.name().c_str() << osc::EndMessage;
        } else {
            int32_t active_users = 0;
            for (auto& usr : grp.users()) {
                active_users += usr.active() ? 1 : 0;
            }
            msg << osc::BeginMessage(legacy_group_public_add)
                << grp.name().c_str() << active_users << osc::EndMessage;
        }
        client.send_message(msg);
    } else {
        auto json = make_public_group_json(grp, removed);
        AooData data {
            kAooDataJSON, (const AooByte *)json.data(), (AooSize)json.size()
        };
        client.send_notification(*this, data);
    }
}

void Server::notify_public_group(const group& grp, bool removed) {
    for (auto& [id, client] : clients_) {
        if (client.active()) {
            send_public_group(client, grp, removed);
        }
    }
}

osc::OutboundPacketStream Server::start_message(size_t extra_size) {
    if (extra_size > 0) {
        auto total = AOO_MAX_PACKET_SIZE + extra_size;
        if (sendbuffer_.size() < total) {
            sendbuffer_.resize(total);
        }
    }
    // leave space for message size (int32_t)
    return osc::OutboundPacketStream(sendbuffer_.data() + 4, sendbuffer_.size() - 4);
}

AooId Server::accept_client(const aoo::ip_address& addr, aoo::tcp_server::reply_func fn) {
    sync::scoped_lock lock(mutex_); // writer lock; see run() method

    auto id = get_next_client_id();
    // TODO: check max. client count
    clients_.emplace(id, client_endpoint(id, fn));

    LOG_DEBUG("AooServer: add client " << id);
    return id;
}

bool Server::remove_client(AooId id, AooError err, std::string_view msg) {
    auto it = clients_.find(id);
    if (it == clients_.end()) {
        LOG_ERROR("AooServer: removeClient: client " << id << " not found");
        return false;
    }
    // remove from group(s) and send notifications
    it->second.on_close(*this);

    if (it->second.active()) {
        // only send event if logged in!
        auto e = std::make_unique<client_logout_event>(id, err, msg);
        send_event(std::move(e));
    }

    clients_.erase(it);

    LOG_DEBUG("AooServer: removed client " << id);

    return true;
}

// called from TCP server
void Server::handle_client_data(AooId id, int err, const AooByte *data,
                                AooInt32 size, const aoo::ip_address& addr) {
    sync::scoped_lock lock(mutex_); // writer lock; see run() method

    auto client = find_client(id);
    if (!client) {
        LOG_ERROR("AooServer: handle_client_data: can't find client " << id);
        return;
    }
    if (size > 0) {
        try {
            client->handle_data(*this, data, size);
        } catch (const error& e) {
            LOG_ERROR("AooServer: could not handle client message: " << e.what());
            remove_client(id, e.code(), e.what());
#if 1
            // remove from TCP server!
            tcp_server_.close(id);
#endif
        } catch (const osc::Exception& e) {
            LOG_ERROR("AooServer: malformed client message: " << e.what());
            remove_client(id, kAooErrorBadFormat, e.what());
#if 1
            // remove from TCP server!
            tcp_server_.close(id);
#endif
        }
    } else if (err == 0) {
        // disconnected
        remove_client(id, kAooOk);
    } else {
        // socket error
        remove_client(id, kAooErrorSocket, socket::strerror(err));
    }
}

void Server::handle_ping(client_endpoint& client,
                         const osc::ReceivedMessage& msg) {
    // send reply
    auto reply = start_message();

    reply << osc::BeginMessage(kAooMsgClientPong) << osc::EndMessage;

    client.send_message(reply);
}

void Server::handle_pong(client_endpoint &client,
                         const osc::ReceivedMessage &msg) {
    client.handle_pong();
}

void Server::handle_message(client_endpoint& client,
                            const osc::ReceivedMessage& msg, int32_t size) {
    AooMsgType type;
    int32_t onset;
    auto err = parse_pattern((const AooByte *)msg.AddressPattern(), size, type, onset);
    if (err != kAooOk){
        throw error(kAooErrorBadFormat, "not an AOO message!");
    }

    try {
        if (type == kAooMsgTypeServer){
            std::string_view pattern = msg.AddressPattern() + onset;
            LOG_DEBUG("AooServer: got server message " << pattern);
            if (pattern == kAooMsgLogin){
                handle_login(client, msg);
            } else {
                // all other messages must be received after login!
                if (!client.active()) {
                    throw error(kAooErrorNotPermitted, "not logged in");
                }
                if (pattern == kAooMsgPing) {
                    handle_ping(client, msg);
                } else if (pattern == kAooMsgPong) {
                    handle_pong(client, msg);
                } else if (pattern == kAooMsgGroupJoin) {
                    handle_group_join(client, msg);
                } else if (pattern == kAooMsgGroupLeave) {
                    handle_group_leave(client, msg);
                } else if (pattern == kAooMsgGroupUpdate) {
                    handle_group_update(client, msg);
                } else if (pattern == kAooMsgUserUpdate) {
                    handle_user_update(client, msg);
                } else if (pattern == kAooMsgRequest) {
                    handle_custom_request(client, msg);
                } else {
                    // NB: the client is supposed to check the server version
                    // and only send supported messages.
                    std::stringstream ss;
                    ss << "unknown server message " << pattern;
                    throw error(kAooErrorNotImplemented, ss.str());
                }
            }
        } else {
            std::stringstream ss;
            ss << "unexpected message " << msg.AddressPattern();
            throw error(kAooErrorBadFormat, ss.str());
        }
    } catch (const osc::WrongArgumentTypeException& e) {
        std::stringstream ss;
        ss << "wrong argument(s) for " << msg.AddressPattern() << " message";
        throw error(kAooErrorBadArgument, ss.str());
    } catch (const osc::MissingArgumentException& e) {
        std::stringstream ss;
        ss << "missing argument(s) for " << msg.AddressPattern() << " message";
        throw error(kAooErrorBadArgument, ss.str());
    } catch (const osc::Exception& e) {
        std::stringstream ss;
        ss << "malformed " << msg.AddressPattern() << " message: " << e.what();
        throw error(kAooErrorBadFormat, ss.str());
    }
}

void Server::handle_legacy_message(client_endpoint& client,
                                   const osc::ReceivedMessage& msg, int32_t size) {
    client.handle_pong();

    AooMsgType type;
    int32_t onset;
    auto err = parse_pattern((const AooByte *)msg.AddressPattern(), size, type, onset);
    if (err != kAooOk || type != kAooMsgTypeServer) {
        throw error(kAooErrorBadFormat, "not an AOO server message");
    }

    try {
        std::string_view pattern = msg.AddressPattern() + onset;
        if (pattern == kAooMsgLogin) {
            handle_legacy_login(client, msg);
            return;
        }
        if (!client.active()) {
            LOG_WARNING("AooServer: ignore legacy message before login " << pattern);
            return;
        }
        if (pattern == kAooMsgPing) {
            auto reply = start_message();
            reply << osc::BeginMessage(kAooMsgClientPing) << osc::EndMessage;
            client.send_message(reply);
        } else if (pattern == kAooMsgGroupJoin) {
            handle_legacy_group_join(client, msg);
        } else if (pattern == kAooMsgGroupLeave) {
            handle_legacy_group_leave(client, msg);
        } else if (pattern == legacy_group_public) {
            handle_legacy_group_public(client, msg);
        } else {
            LOG_WARNING("AooServer: unknown legacy message " << pattern);
        }
    } catch (const osc::Exception& e) {
        LOG_WARNING("AooServer: malformed legacy message "
                    << msg.AddressPattern() << ": " << e.what());
    } catch (const std::exception& e) {
        LOG_WARNING("AooServer: failed to handle legacy message "
                    << msg.AddressPattern() << ": " << e.what());
    }
}

void Server::handle_legacy_login(client_endpoint& client,
                                 const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    std::string username = (it++)->AsString();
    auto password = normalize_legacy_password((it++)->AsString());
    std::string public_ip = (it++)->AsString();
    auto public_port = (it++)->AsInt32();
    std::string local_ip = (it++)->AsString();
    auto local_port = (it++)->AsInt32();
    int64_t token = msg.ArgumentCount() > 6 ? (it++)->AsInt64() : 0;

    std::string error_message;
    if (!password_.empty()) {
        error_message = "access denied";
    } else if (client.active()) {
        error_message = "already logged in";
    } else if (username.empty()) {
        error_message = "access denied";
    } else {
        for (auto& [id, other] : clients_) {
            if (id != client.id() && other.active()
                    && other.protocol() == wire_protocol::legacy
                    && other.legacy_name() == username) {
                error_message = "access denied";
                break;
            }
        }
    }

    ip_address public_address;
    ip_address local_address;
    if (error_message.empty()) {
        try {
            public_address = ip_address(public_ip, (port_type)public_port);
            local_address = ip_address(local_ip, (port_type)local_port);
        } catch (const resolve_error&) {
            error_message = "access denied";
        }
    }

    auto reply = start_message();
    if (!error_message.empty()) {
        reply << osc::BeginMessage(kAooMsgClientLogin)
              << (int32_t)0 << error_message.c_str() << osc::EndMessage;
        client.send_message(reply);
        return;
    }

    client.activate_legacy(std::move(username), std::move(password),
                           public_address, local_address, token);
    remember_udp_protocol(public_address, wire_protocol::legacy);

    reply << osc::BeginMessage(kAooMsgClientLogin)
          << (int32_t)1 << "" << osc::EndMessage;
    client.send_message(reply);

    auto event = std::make_unique<client_login_event>(client, kAooOk);
    send_event(std::move(event));
}

void Server::handle_legacy_group_join(client_endpoint& client,
                                      const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    std::string group_name = (it++)->AsString();
    auto group_password = normalize_legacy_password((it++)->AsString());
    bool is_public = msg.ArgumentCount() > 2 ? (it++)->AsBool() : false;
    std::string error_message;

    auto grp = find_group(group_name);
    user *usr = nullptr;
    if (grp) {
        if (grp->is_public() != is_public) {
            error_message = "permission denied";
        } else if (!grp->check_pwd(group_password.empty() ? nullptr
                                                          : group_password.c_str())) {
            error_message = "wrong password";
        } else {
            usr = grp->find_user(client.legacy_name());
            if (usr) {
                if (usr->active()) {
                    error_message = usr->client() == client.id()
                            ? "already a group member" : "access denied";
                } else if (!usr->check_pwd(client.legacy_password().empty() ? nullptr
                                                  : client.legacy_password().c_str())) {
                    error_message = "wrong password";
                }
            } else if (!grp->user_auto_create()) {
                error_message = "permission denied";
            }
        }
    } else if (!group_auto_create_.load()) {
        error_message = "permission denied";
    }

    if (!error_message.empty()) {
        auto reply = start_message();
        reply << osc::BeginMessage(kAooMsgClientGroupJoin)
              << group_name.c_str() << (int32_t)0 << error_message.c_str()
              << osc::EndMessage;
        client.send_message(reply);
        return;
    }

    AooRequestGroupJoin request {
        AOO_REQUEST_INIT(GroupJoin, relayAddress),
        group_name.c_str(), group_password.empty() ? nullptr : group_password.c_str(),
        grp ? grp->id() : kAooIdInvalid, nullptr,
        client.legacy_name().c_str(),
        client.legacy_password().empty() ? nullptr : client.legacy_password().c_str(),
        usr ? usr->id() : kAooIdInvalid, nullptr, nullptr
    };
    AooResponseGroupJoin response;
    do_group_join(client, 0, request, response, is_public);
}

void Server::handle_legacy_group_leave(client_endpoint& client,
                                       const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    std::string group_name = (it++)->AsString();
    std::string error_message;

    auto grp = find_group(group_name);
    if (!grp) {
        error_message = "couldn't find group";
    } else if (auto usr = grp->find_user(client)) {
        on_user_left_group(*grp, *usr);
        client.on_group_leave(*this, *grp, *usr, false);
        do_remove_user_from_group(*grp, *usr);
    } else {
        error_message = "not a group member";
    }

    auto reply = start_message();
    reply << osc::BeginMessage(kAooMsgClientGroupLeave)
          << group_name.c_str() << (int32_t)(error_message.empty() ? 1 : 0)
          << error_message.c_str() << osc::EndMessage;
    client.send_message(reply);
}

void Server::handle_legacy_group_public(client_endpoint& client,
                                        const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    bool watch = (it++)->AsBool();
    client.set_watches_public_groups(watch);

    if (watch) {
        for (auto& [id, grp] : groups_) {
            if (grp.is_public()) {
                send_public_group(client, grp, false);
            }
        }
    }

    auto reply = start_message();
    reply << osc::BeginMessage(legacy_client_group_public)
          << watch << (int32_t)0 << "" << osc::EndMessage;
    client.send_message(reply);
}

//------------------------- login ------------------------------//

void Server::handle_login(client_endpoint& client, const osc::ReceivedMessage& msg)
{
    auto it = msg.ArgumentsBegin();
    auto token = (AooId)(it++)->AsInt32();
    if (force_legacy_protocol_) {
        client.send_error(*this, token, kAooRequestLogin, kAooErrorVersionNotSupported);
        return;
    }
    auto version = (it++)->AsString();
    auto pwd = (it++)->AsString();
    auto metadata = osc_read_metadata(it); // optional
    // collect IP addresses
    auto addrcount = (it++)->AsInt32();
    for (int32_t i = 0; i < addrcount; ++i) {
        auto address = osc_read_address(it);
        client.add_public_address(address);
        if (find_udp_protocol(address) == wire_protocol::current) {
            client.set_observed_address(address);
        }
    }

    AooRequestLogin request {
        AOO_REQUEST_INIT(Login, metadata),
        version, (*pwd ? pwd : nullptr),
        (metadata ? &metadata.value() : nullptr)
    };

    // check version
    if (auto err = check_version(version); err != kAooOk) {
        LOG_DEBUG("AooServer: client " << client.id() << ": version mismatch");
        client.send_error(*this, token, request.type, err);
        // send event
        auto e = std::make_unique<client_login_event>(client, err);
        send_event(std::move(e));
        return;
    }
    // check password
    if (!password_.empty() && pwd != password_) {
        LOG_DEBUG("AooServer: client " << client.id() << ": wrong password");
        client.send_error(*this, token, request.type, kAooErrorWrongPassword);
        // send event
        auto e = std::make_unique<client_login_event>(client, kAooErrorWrongPassword);
        send_event(std::move(e));
        return;
    }

    if (!handle_request(client, token, (AooRequest&)request)) {
        AooResponseLogin response; // default constructor

        do_login(client, token, request, response);
    }
}

AooError Server::do_login(client_endpoint& client, AooId token,
                          const AooRequestLogin& request,
                          AooResponseLogin& response) {
    client.activate(request.version);

    // send reply
    auto extra = response.metadata ? response.metadata->size : 0;
    auto msg = start_message(extra);

    msg << osc::BeginMessage(kAooMsgClientLogin)
        << token << kAooErrorNone
        << aoo_getVersionString() << client.id()
        << (int32_t)0 << metadata_view(response.metadata)
        << osc::EndMessage;

    client.send_message(msg);

    auto e = std::make_unique<client_login_event>(client, kAooOk, aoo::metadata(request.metadata));
    send_event(std::move(e));

    return kAooOk;
}

//----------------------- group_join ---------------------------//

void Server::handle_group_join(client_endpoint& client, const osc::ReceivedMessage& msg)
{
    auto it = msg.ArgumentsBegin();
    auto token = (AooId)(it++)->AsInt32();
    auto group_name = (it++)->AsString();
    auto group_pwd = (it++)->AsString();
    auto group_md = osc_read_metadata(it); // optional
    auto user_name = (it++)->AsString();
    auto user_pwd = (it++)->AsString();
    auto user_md = osc_read_metadata(it); // optional
    auto relay = osc_read_host(it); // optional

    // NB: group and user IDs are to be created and will be overriden alter!
    AooRequestGroupJoin request {
        AOO_REQUEST_INIT(GroupJoin, relayAddress),
        group_name, (*group_pwd ? group_pwd : nullptr), kAooIdInvalid,
        (group_md ? &group_md.value() : nullptr),
        user_name, (*user_pwd ? user_pwd : nullptr), kAooIdInvalid,
        (user_md ? &user_md.value() : nullptr),
        (relay ? &relay.value() : nullptr)
    };

    auto grp = find_group(request.groupName);
    user *usr = nullptr;
    if (grp) {
        request.groupId = grp->id();
        // check group password
        if (!grp->check_pwd(request.groupPwd)) {
            client.send_error(*this, token, request.type, kAooErrorWrongPassword);
            return;
        }
        usr = grp->find_user(request.userName);
        if (usr) {
            request.userId = usr->id();
            // check if someone is already logged in
            if (usr->active()) {
                client.send_error(*this, token, request.type, kAooErrorUserAlreadyExists);
                return;
            }
            // check user password
            if (!usr->check_pwd(request.userPwd)) {
                client.send_error(*this, token, request.type, kAooErrorWrongPassword);
                return;
            }
        } else {
            // user needs to be created
            // check if the client is allowed to create users
            if (!grp->user_auto_create()) {
                client.send_error(*this, token, request.type, kAooErrorCannotCreateUser);
                return;
            }
        }
    } else {
        // group needs to be created
        // check if the client is allowed to create groups
        if (!group_auto_create_.load()) {
            client.send_error(*this, token, request.type, kAooErrorCannotCreateGroup);
            return;
        }
    }

    if (!handle_request(client, token, (AooRequest&)request)) {
        AooResponseGroupJoin response; // default constructor

        do_group_join(client, token, request, response,
                      is_sonobus_public_group(request.groupMetadata));
    }
}

AooError Server::do_group_join(client_endpoint &client, AooId token,
                               const AooRequestGroupJoin& request,
                               AooResponseGroupJoin& response,
                               bool is_public) {
    bool did_create_group = false;
    // find/create group
    auto grp = find_group(request.groupId);
    if (!grp) {
        auto group_pwd = request.groupPwd ? request.groupPwd : "";
        // prefer response group metadata
        auto group_md = response.groupMetadata ? response.groupMetadata : request.groupMetadata;
        is_public = is_public || is_sonobus_public_group(group_md);

        grp = add_group(group(request.groupName, group_pwd, get_next_group_id(),
                              group_md, response.relayAddress, 0, is_public));
        if (grp) {
            // send event
            auto e = std::make_unique<group_add_event>(*grp);
            send_event(std::move(e));
        } else {
            // group has been added in the meantime... LATER try to deal with this
            if (client.protocol() == wire_protocol::legacy) {
                auto msg = start_message();
                msg << osc::BeginMessage(kAooMsgClientGroupJoin)
                    << request.groupName << (int32_t)0 << "permission denied"
                    << osc::EndMessage;
                client.send_message(msg);
            } else {
                client.send_error(*this, token, request.type,
                                  kAooErrorCannotCreateGroup);
            }
            return kAooErrorCannotCreateGroup;
        }
        did_create_group = true;
    }

    // find user or create it if necessary
    auto usr = grp->find_user(request.userId);
    if (!usr) {
        auto user_pwd = request.userPwd ? request.userPwd : "";
        // prefer response user metadata
        auto user_md = response.userMetadata ? response.userMetadata : request.userMetadata;

        auto id = grp->get_next_user_id();
        AooFlag flags = did_create_group ? kAooUserGroupCreator : 0;
        usr = grp->add_user(user(request.userName, user_pwd, id, grp->id(),
                                 client.id(), user_md, request.relayAddress, flags));
        if (!usr) {
            // user has been added in the meantime... LATER try to deal with this
            if (client.protocol() == wire_protocol::legacy) {
                auto msg = start_message();
                msg << osc::BeginMessage(kAooMsgClientGroupJoin)
                    << request.groupName << (int32_t)0 << "access denied"
                    << osc::EndMessage;
                client.send_message(msg);
            } else {
                client.send_error(*this, token, request.type,
                                  kAooErrorCannotCreateUser);
            }
            return kAooErrorCannotCreateUser;
        }
    } else {
        usr->set_client(client.id());
    }

    // update response, so we may inspect it after calling handlingRequest()!
    response.groupId = grp->id();
    response.groupFlags = grp->flags();
    response.userId = usr->id();
    response.userFlags = usr->flags();

    client.on_group_join(*this, *grp, *usr);

    // prefer group relay over global relay address; both may be empty!
    net::ip_host relay_addr;
    if (grp->relay_addr().valid()) {
        relay_addr = grp->relay_addr();
    } else {
        if (global_relay_addr_.valid()) {
            relay_addr = global_relay_addr_;
        } else if (internal_relay_.load()) {
            // internal UDP server
            relay_addr.name = ""; // empty string -> server IP
            relay_addr.port = port_;
        }
    }
    if (client.protocol() == wire_protocol::legacy) {
        // Legacy clients may receive peer notifications before the join reply.
        on_user_joined_group(*grp, *usr, client);

        if (grp->is_public()) {
            notify_public_group(*grp);
        }

        auto msg = start_message();
        msg << osc::BeginMessage(kAooMsgClientGroupJoin)
            << grp->name().c_str() << (int32_t)1 << ""
            << osc::EndMessage;
        client.send_message(msg);
    } else {
        // send reply
        auto extra = grp->metadata().size() + usr->metadata().size() +
                (response.privateMetadata ? response.privateMetadata->size : 0);
        auto msg = start_message(extra);

        msg << osc::BeginMessage(kAooMsgClientGroupJoin)
            << token << kAooErrorNone
            << grp->id() << (int32_t)grp->flags() << grp->metadata()
            << usr->id() << (int32_t)usr->flags() << usr->metadata()
            << metadata_view(response.privateMetadata)
            << relay_addr
            << osc::EndMessage;

        client.send_message(msg);

        // after reply!
        on_user_joined_group(*grp, *usr, client);
        if (grp->is_public()) {
            notify_public_group(*grp);
        }
    }

    return kAooOk; // success
}

//---------------------- group_leave -------------------------//

void Server::handle_group_leave(client_endpoint& client, const osc::ReceivedMessage& msg){
    auto it = msg.ArgumentsBegin();
    auto token = (AooId)(it++)->AsInt32();
    auto group = (it++)->AsInt32();

    AooRequestGroupLeave request { AOO_REQUEST_INIT(GroupLeave, group), group };

    AooResponseGroupLeave response; // default constructor

    if (!handle_request(client, token, (AooRequest&)request)) {
        do_group_leave(client, token, request, response);
    }
}

AooError Server::do_group_leave(client_endpoint& client, AooId token,
                                const AooRequestGroupLeave& request,
                                AooResponseGroupLeave& response) {
    AooError result = kAooErrorNone;

    if (auto grp = find_group(request.group)) {
        // find the user in the group that is associated with this client
        // LATER we might move this logic to the client side and send the
        // user ID to the server.
        if (auto usr = grp->find_user(client)) {
            on_user_left_group(*grp, *usr);

            client.on_group_leave(*this, *grp, *usr, false);

            do_remove_user_from_group(*grp, *usr);

            // send reply
            auto msg = start_message();

            msg << osc::BeginMessage(kAooMsgClientGroupLeave)
                << token << kAooErrorNone
                << osc::EndMessage;

            client.send_message(msg);

            return kAooOk;
        } else {
            result = kAooErrorNotGroupMember;
        }
    } else {
        result = kAooErrorGroupDoesNotExist;
    }
    client.send_error(*this, token, request.type, result);

    return result;
}

//----------------------- group_update --------------------------//

void Server::handle_group_update(client_endpoint& client, const osc::ReceivedMessage& msg)
{
    auto it = msg.ArgumentsBegin();
    auto token = (AooId)(it++)->AsInt32();
    auto group_id = (it++)->AsInt32();
    auto md = osc_read_metadata(it);
    if (!md) {
        throw osc::MalformedMessageException("missing data");
    }

    AooRequestGroupUpdate request {
        AOO_REQUEST_INIT(GroupUpdate, groupMetadata),
        group_id, *md
    };

    auto grp = find_group(request.groupId);
    if (!grp) {
        client.send_error(*this, token, request.type, kAooErrorGroupDoesNotExist);
        return;
    }
    // verify group membership (in addition to client-side check)
    if (!grp->find_user(client)) {
        client.send_error(*this, token, request.type, kAooErrorNotPermitted);
        return;
    }

    if (!handle_request(client, token, (AooRequest&)request)) {
        AooResponseGroupUpdate response; // default constructor
        response.groupMetadata = request.groupMetadata;

        do_group_update(client, token, request, response);
    }
}

AooError Server::do_group_update(client_endpoint &client, AooId token,
                                 const AooRequestGroupUpdate& request,
                                 AooResponseGroupUpdate& response) {
    auto grp = find_group(request.groupId);
    if (!grp) {
        LOG_ERROR("AooServer: could not find group");
        return kAooErrorNotFound;
    }

    auto usr = grp->find_user(client);
    if (!usr) {
        LOG_ERROR("AooServer: could not find user");
        return kAooErrorNotFound;
    }

    LOG_DEBUG("AooServer: group " << *grp << " updated by user " << *usr);

    grp->set_metadata(response.groupMetadata);

    // notify peers
    for (auto& p : grp->users()) {
        if (p.client() != client.id()) {
            if (auto c = find_client(p.client())) {
                c->send_group_update(*this, *grp, usr->id());
            } else {
                LOG_ERROR("AooServer: could not find client for user " << p);
            }
        }
    }

    // send reply
    auto msg = start_message(grp->metadata().size());

    msg << osc::BeginMessage(kAooMsgClientGroupUpdate)
        << token << kAooErrorNone << grp->metadata()
        << osc::EndMessage;

    client.send_message(msg);

    // send event
    auto e = std::make_unique<group_update_event>(*grp, *usr);
    send_event(std::move(e));

    return kAooOk; // success
}

//---------------------- user_update -------------------------//

void Server::handle_user_update(client_endpoint& client, const osc::ReceivedMessage& msg)
{
    auto it = msg.ArgumentsBegin();
    auto token = (AooId)(it++)->AsInt32();
    auto group_id = (it++)->AsInt32();
    auto md = osc_read_metadata(it);
    if (!md) {
        throw osc::MalformedMessageException("missing data");
    }

    AooRequestUserUpdate request {
        AOO_REQUEST_INIT(UserUpdate, userMetadata),
        group_id, kAooIdInvalid, *md
    };

    auto grp = find_group(request.groupId);
    if (!grp) {
        client.send_error(*this, token, request.type, kAooErrorGroupDoesNotExist);
        return;
    }
    // verify group membership (in addition to client-side check)
    // and get user ID.
    auto usr = grp->find_user(client);
    if (!usr) {
        client.send_error(*this, token, request.type, kAooErrorNotPermitted);
        return;
    }
    request.userId = usr->id();

    if (!handle_request(client, token, (AooRequest&)request)) {
        AooResponseUserUpdate response; // default constructor
        response.userMetadata = request.userMetadata;

        do_user_update(client, token, request, response);
    }
}

AooError Server::do_user_update(client_endpoint &client, AooId token,
                                const AooRequestUserUpdate& request,
                                AooResponseUserUpdate& response) {
    auto grp = find_group(request.groupId);
    if (!grp) {
        LOG_ERROR("AooServer: could not find group");
        return kAooErrorNotFound;
    }

    auto usr = grp->find_user(request.userId);
    if (!usr) {
        LOG_ERROR("AooServer: could not find user");
        return kAooErrorNotFound;
    }

    LOG_DEBUG("AooServer: update usr " << *usr);

    usr->set_metadata(response.userMetadata);

    // notify peers
    for (auto& member : grp->users()) {
        if (member.id() != usr->id()) {
            if (auto c = find_client(member.client())) {
                c->send_peer_update(*this, *usr);
            } else {
                LOG_ERROR("AooServer: could not find client for user " << member);
            }
        }
    }

    // send reply
    auto msg = start_message(usr->metadata().size());

    msg << osc::BeginMessage(kAooMsgClientUserUpdate)
        << token << kAooErrorNone << usr->metadata()
        << osc::EndMessage;

    client.send_message(msg);

    // send event
    auto e = std::make_unique<user_update_event>(*usr);
    send_event(std::move(e));

    return kAooOk; // success
}

//-------------------- custom_request ---------------------//

void Server::handle_custom_request(client_endpoint& client, const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    auto token = (AooId)(it++)->AsInt32();
    auto flags = (it++)->AsInt32();
    auto data = osc_read_metadata(it);
    if (!data) {
        throw osc::MalformedMessageException("missing data");
    }

    AooRequestCustom request {
        AOO_REQUEST_INIT(Custom, flags),
        *data, (AooFlag)flags
    };

    bool watch_public_groups;
    if (read_sonobus_public_subscription(request.data, watch_public_groups)) {
        client.set_watches_public_groups(watch_public_groups);
        AooResponseCustom response;
        do_custom_request(client, token, request, response);
        if (watch_public_groups) {
            for (auto& [id, grp] : groups_) {
                if (grp.is_public()) {
                    send_public_group(client, grp, false);
                }
            }
        }
        return;
    }

    if (!handle_request(client, token, (AooRequest&)request)) {
        // requests must be handled by the user!
        client.send_error(*this, token, request.type, kAooErrorUnhandledRequest);
    }
}

AooError Server::do_custom_request(client_endpoint& client, AooId token,
                                   const AooRequestCustom& request,
                                   AooResponseCustom& response) {
    // send reply
    auto msg = start_message(response.data.size);

    msg << osc::BeginMessage(kAooMsgClientRequest)
        << token << kAooErrorNone << (int32_t)response.flags
        << metadata_view(&response.data) << osc::EndMessage;

    client.send_message(msg);

    return kAooOk;
}

//----------------------- UDP messages --------------------------//

void Server::handle_udp_packet(const AooByte *data, AooInt32 size,
                               const aoo::ip_address& addr,
                               size_t socket_index) {
    AooMsgType type;
    int32_t onset;
    auto err = parse_pattern(data, size, type, onset);
    if (err != kAooOk){
        LOG_WARNING("AooServer: not an AOO NET message!");
        return;
    }

    if (type == kAooMsgTypeServer){
        handle_udp_message(data, size, onset, addr, socket_index);
    } else if (type == kAooMsgTypeRelay){
        handle_relay(data, size, addr, socket_index);
    } else {
        LOG_WARNING("AooServer: not a client message!");
    }
}

void Server::handle_udp_message(const AooByte *data, AooSize size, int onset,
                                const ip_address& addr, size_t socket_index) {
    if (binmsg_check(data, size)) {
        LOG_WARNING("AooServer: unsupported binary message");
        return;
    }
    // OSC format
    try {
        osc::ReceivedPacket packet((const char *)data, size);
        osc::ReceivedMessage msg(packet);

        std::string_view pattern = msg.AddressPattern() + onset;
        LOG_DEBUG("AooServer: handle client UDP message " << pattern);

        if (pattern == kAooMsgPing) {
            handle_ping(msg, addr, socket_index);
        } else if (pattern == kAooMsgQuery) {
            handle_query(msg, addr, socket_index);
        } else if (pattern == legacy_request) {
            handle_legacy_request(msg, addr, socket_index);
        } else {
            LOG_ERROR("AooServer: unknown message " << pattern);
            return;
        }
    } catch (const osc::Exception& e){
        auto pattern = (const char *)data + onset;
        LOG_ERROR("AooServer: exception on handling " << pattern
                  << " message: " << e.what());
    }
}

void Server::handle_relay(const AooByte *data, AooSize size,
                          const ip_address& addr, size_t socket_index) {
    if (!internal_relay_.load()) {
    #if AOO_DEBUG_RELAY
        LOG_DEBUG("AooServer: ignore relay message from " << addr);
    #endif
        return;
    }

    auto src_addr = addr.unmapped();

    auto check_addr = [&](ip_address& addr) {
        if (addr.is_ipv4_mapped()) {
            LOG_DEBUG("AooServer: relay destination must not be IPv4-mapped");
            return false;
        }
        if (address_family_ == ip_address::IPv6 && addr.type() == ip_address::IPv4) {
            if (use_ipv4_mapped_) {
                // map address to IPv4
                addr = addr.ipv4_mapped();
            } else {
                // cannot relay to IPv4 address with IPv6-only socket
                LOG_DEBUG("AooClient: cannot relay to destination address " << addr);
                return false;
            }
        } else if (address_family_ == ip_address::IPv4 && addr.type() == ip_address::IPv6) {
            // cannot relay to IPv6 address with IPv4-only socket
            LOG_DEBUG("AooClient: cannot relay to destination address " << addr);
            return false;
        }
        // ip_address::Unspec -> always pass
        return true;
    };

    if (binmsg_check(data, size)) {
        // --- binary format ---
        ip_address dst_addr;
        auto onset = binmsg_read_relay(data, size, dst_addr);
        if (!check_addr(dst_addr)) {
            return;
        }
        if (onset > 0) {
        #if AOO_DEBUG_RELAY
            LOG_DEBUG("AooServer: forward binary relay message from " << addr << " to " << dst);
        #endif
            if (src_addr.type() == dst_addr.type()) {
                // simply replace the header (= rewrite address)
                binmsg_write_relay(const_cast<AooByte *>(data), size, src_addr);
                send_udp(socket_index, dst_addr, data, size);
            } else {
                // rewrite whole message
                AooByte buf[AOO_MAX_PACKET_SIZE];
                auto result = write_relay_message(buf, sizeof(buf), data + onset,
                                                  size - onset, src_addr, true);
                if (result > 0) {
                    send_udp(socket_index, dst_addr, buf, result);
                } else {
                    LOG_ERROR("AooServer: can't relay: buffer too small");
                }
            }
        } else {
            LOG_ERROR("AooServer: bad relay message");
        }
    } else {
        // --- OSC format ---
        try {
            osc::ReceivedPacket packet((const char *)data, size);
            osc::ReceivedMessage msg(packet);

            auto it = msg.ArgumentsBegin();
            auto dst_addr = osc_read_address(it);
            if (!check_addr(dst_addr)) {
                return;
            }

            const void *msgData;
            osc::osc_bundle_element_size_t msgSize;
            (it++)->AsBlob(msgData, msgSize);

            // don't prepend size for UDP message!
            char buf[AOO_MAX_PACKET_SIZE];
            osc::OutboundPacketStream out(buf, sizeof(buf));
            out << osc::BeginMessage(kAooMsgDomain kAooMsgRelay)
                << src_addr << osc::Blob(msgData, msgSize)
                << osc::EndMessage;

        #if AOO_DEBUG_RELAY
            LOG_DEBUG("AooServer: forward OSC relay message from " << addr << " to " << dst);
        #endif
            send_udp(socket_index, dst_addr, (const AooByte *)out.Data(), out.Size());
        } catch (const osc::Exception& e){
            LOG_ERROR("AooServer: exception in handle_relay: " << e.what());
        }
    }
}

void Server::handle_ping(const osc::ReceivedMessage& msg, const ip_address& addr,
                         size_t socket_index) {
    auto protocol = find_udp_protocol(addr);
    auto pattern = protocol == wire_protocol::legacy
            || (protocol == wire_protocol::unknown && force_legacy_protocol_)
            ? kAooMsgClientPing : kAooMsgClientPong;

    // NB: don't prepend size for UDP message!
    char buf[512];
    osc::OutboundPacketStream reply(buf, sizeof(buf));
    reply << osc::BeginMessage(pattern)
          << osc::EndMessage;

    send_udp(socket_index, addr, (const AooByte *)reply.Data(), reply.Size());
}

void Server::handle_query(const osc::ReceivedMessage& msg, const ip_address& addr,
                          size_t socket_index) {
    remember_udp_protocol(addr, wire_protocol::current);

    // NB: do not prepend size for UDP message!
    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream reply(buf, sizeof(buf));
    reply << osc::BeginMessage(kAooMsgClientQuery)
          << addr.unmapped() // return unmapped(!) public IP
          << osc::EndMessage;

    send_udp(socket_index, addr, (const AooByte *)reply.Data(), reply.Size());
}

void Server::handle_legacy_request(const osc::ReceivedMessage& msg,
                                   const ip_address& addr,
                                   size_t socket_index) {
    remember_udp_protocol(addr, wire_protocol::legacy);

    auto public_address = addr.unmapped();
    char buf[512];
    osc::OutboundPacketStream reply(buf, sizeof(buf));
    reply << osc::BeginMessage(legacy_client_reply)
          << public_address.name_unmapped()
          << (int32_t)public_address.port()
          << osc::EndMessage;

    send_udp(socket_index, addr, (const AooByte *)reply.Data(), reply.Size());
}

void Server::send_udp(size_t socket_index, const ip_address& addr,
                      const AooByte *data, AooSize size) {
    if (!external_udp_socket_ && socket_index != external_udp_socket_index) {
        udp_server_.send(socket_index, addr, data, size);
    } else {
        udp_sendfn_(data, size, addr, 0);
    }
}

void Server::remember_udp_protocol(const ip_address& addr,
                                   wire_protocol protocol) {
    constexpr size_t max_udp_protocols = 2048;
    sync::scoped_lock<sync::spinlock> lock(udp_protocol_lock_);
    for (auto& endpoint : udp_protocols_) {
        if (endpoint.address == addr) {
            endpoint.protocol = protocol;
            return;
        }
    }
    if (udp_protocols_.size() >= max_udp_protocols) {
        udp_protocols_.erase(udp_protocols_.begin());
    }
    udp_protocols_.push_back({ addr, protocol });
}

wire_protocol Server::find_udp_protocol(const ip_address& addr) {
    sync::scoped_lock<sync::spinlock> lock(udp_protocol_lock_);
    for (auto it = udp_protocols_.rbegin(); it != udp_protocols_.rend(); ++it) {
        if (it->address == addr) {
            return it->protocol;
        }
    }
    return wire_protocol::unknown;
}

AooId Server::get_next_client_id(){
    // LATER make random group ID
    return next_client_id_++;
}

AooId Server::get_next_group_id(){
    // LATER make random group ID
    return next_group_id_++;
}

void Server::push_message(AooId group, AooId user, const AooData& data) {
    message msg;
    msg.group = group;
    msg.user = user;
    msg.type = data.type;
    msg.data.assign(data.data, data.data + data.size);

    message_queue_.push(std::move(msg));

    tcp_server_.notify();
}

void Server::dispatch_message(const message &msg) {
    if (msg.group == kAooIdInvalid) {
        // client message
        if (msg.user == kAooIdInvalid) {
            // all clients
            AooData data;
            data.type = msg.type;
            data.data = msg.data.data();
            data.size = msg.data.size();

            for (auto& [_, c] : clients_) {
                c.send_notification(*this, data);
            }
        }else if (auto c = find_client(msg.user)) {
            // single client
            AooData data;
            data.type = msg.type;
            data.data = msg.data.data();
            data.size = msg.data.size();

            c->send_notification(*this, data);
        } else {
            LOG_WARNING("AooServer: cannot send message to client " << msg.user
                        << " because it does not exist anymore");
        }
    } else {
        // group/user message
        if (auto g = find_group(msg.group)) {
            AooData data;
            data.type = msg.type;
            data.data = msg.data.data();
            data.size = msg.data.size();

            if (msg.user == kAooIdInvalid) {
                // all users
                for (auto& u : g->users()) {
                    if (auto c = find_client(u)) {
                        c->send_notification(*this, data);
                    } else {
                        LOG_WARNING("AooServer: cannot send message to user "
                                    << msg.user << " in group " << msg.group
                                    << " because it does not exist anymore");
                    }
                }
            } else {
                // single user
                if (auto u = g->find_user(msg.user)) {
                    if (auto c = find_client(*u)) {
                        c->send_notification(*this, data);
                    } else {
                        LOG_ERROR("AooServer: cannot find client for user " << *u);
                    }
                } else {
                    LOG_WARNING("AooServer: cannot send message to user "
                                << msg.user << " in group " << msg.group
                                << " because it does not exist anymore");
                }
            }
        } else {
            LOG_WARNING("AooServer: cannot send message to group " << msg.group
                        << " because it does not exist anymore");
        }
    }
}

void Server::send_event(event_ptr e) {
    switch (event_mode_){
    case kAooEventModePoll:
        event_queue_.push(std::move(e));
        break;
    case kAooEventModeCallback:
    {
        event_handler fn(event_handler_, event_context_, kAooThreadLevelNetwork);
        e->dispatch(fn);
        break;
    }
    default:
        break;
    }
}

void Server::close() {
    // JC: need to close all the clients sockets without having them
    // send anything out, so that active communication between connected
    // peers can continue if the server goes down for maintainence
    clients_.clear();
    groups_.clear();
    message_queue_.clear();
}

} // net
} // aoo
