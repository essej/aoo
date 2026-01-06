/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "client.hpp"
#include "client_events.hpp"

#include "aoo_source.hpp"
#include "aoo_sink.hpp"

#include "../binmsg.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <algorithm>
#include <sstream>

#ifndef _WIN32
# include <sys/poll.h>
# include <sys/select.h>
# include <sys/time.h>
# include <unistd.h>
# include <netdb.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <errno.h>
#endif

namespace aoo {

std::string response_error_message(AooError result, int code, const char *msg) {
    if (code != 0 || *msg) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s: %s (%d)", aoo_strerror(result), msg, code);
        return buf;
    } else {
        return aoo_strerror(result);
    }
}

}

//--------------------- AooClient -----------------------------//

AOO_API AooClient * AOO_CALL AooClient_new(void) {
    try {
        return aoo::construct<aoo::net::Client>();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

aoo::net::Client::Client() {
    try {
        event_socket_= udp_socket(port_tag{}, 0);
    } catch (const socket_error& e) {
        // TODO handle error
        socket::print_error(e.code());
    }

    sendbuffer_.resize(AOO_MAX_PACKET_SIZE);
}

AOO_API void AOO_CALL AooClient_free(AooClient *client){
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::net::Client *>(client));
}

aoo::net::Client::~Client() {
    close();
}

AOO_API AooError AOO_CALL AooClient_setup(

    AooClient *client, AooClientSettings *settings)
{
    if (settings == nullptr) {
        return kAooErrorBadArgument;
    }
    return client->setup(*settings);
}

AooError AOO_CALL aoo::net::Client::setup(AooClientSettings& settings)
{
    if (settings.options & kAooClientExternalUDPSocket) {
        if (settings.sendFunc) {
            udp_sendfn_ = sendfn(settings.sendFunc, settings.userData);
        } else {
            return kAooErrorBadArgument;
        }
    } else {
        udp_sendfn_ = sendfn(udp_client::send, &udp_client_);
    }

    if (auto err = udp_client_.setup(*this, settings); err != kAooOk) {
        return err;
    }

    LOG_DEBUG("AooClient: listen on port " << udp_client_.port());

    message_handler_ = settings.messageHandler;
    user_data_ = settings.userData;

    // in case run() has been called in non-blocking mode
    close();

    // get private/global network interfaces
    auto get_address = [](ip_address::ip_type family, const char *host, int port) {
        try {
            udp_socket sock(family_tag{}, family, false);
            ip_address addr(host, port, family, false);
            sock.connect(addr);
            return sock.address();
        } catch (const socket_error&) {
            throw std::runtime_error(std::string(family == ip_address::IPv6 ? "IPv6" : "IPv4") + " networking not available");
        }
    };

    local_ipv4_addr_.clear();
#if AOO_USE_IPV6
    global_ipv6_addr_.clear();
#endif

#if AOO_USE_IPV6
    // try to get global IPv6 address
    try {
        auto ipv6_addr = get_address(ip_address::IPv6, "2001:4860:4860::8888", 80);
        global_ipv6_addr_ = ip_address(ipv6_addr.name(), udp_client_.port());
        LOG_DEBUG("AooClient: global IPv6 address: " << global_ipv6_addr_);
    } catch (const std::exception& e) {
        LOG_INFO("AooClient: could not get global IPv6 address");
        LOG_DEBUG(e.what());
    }
#endif

    // try to get private IPv4 address
    try {
        auto ipv4_addr = get_address(ip_address::IPv4, "8.8.8.8", 80);
        local_ipv4_addr_ = ip_address(ipv4_addr.name(), udp_client_.port());
        LOG_DEBUG("AooClient: private IPv4 address: " << local_ipv4_addr_);
    } catch (const std::exception& e) {
        LOG_INFO("AooClient: could not get private IPv4 address");
        LOG_DEBUG(e.what());
    }

    quit_.store(false);

    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_run(AooClient *client, AooSeconds timeout) {
    return client->run(timeout);
}

AooError AOO_CALL aoo::net::Client::run(AooSeconds timeout){
    try {
        double remaining = timeout; // only used if timeout >= 0

        while (!quit_.load()) {
            double sleep = 1e12;

            if (state_.load() == client_state::connected) {
                auto now = aoo::time_tag::now();
                server_settings_lock_.lock();
                auto settings = server_ping_settings_;
                server_settings_lock_.unlock();

                auto result = server_ping_timer_.update(now, settings);
                if (result.state == ping_state::inactive) {
                    LOG_ERROR("AooClient: server not responding");
                    close_with_error(socket_error::timeout);
                } else if (result.ping) {
                    // send ping
                    auto msg = start_server_message();

                    msg << osc::BeginMessage(kAooMsgServerPing)
                        << osc::EndMessage;

                    send_server_message(msg);

                    if (result.state == ping_state::probe) {
                        LOG_DEBUG("AooClient: send TCP probe ping to server");
                    } else {
                        LOG_DEBUG("AooClient: send TCP ping to server");
                    }
                }

                if (result.wait < sleep) {
                    sleep = result.wait;
                }
            }

            if (timeout >= 0) {
                sleep = std::min<double>(sleep, timeout);
            }
            auto didsomething = wait_for_event(sleep);

            // handle commands
            std::unique_ptr<icommand> cmd;
            while (commands_.pop(cmd)){
                cmd->perform(*this);
            }

            if (peers_.reclaim()){
                LOG_DEBUG("AooClient: free stale peers");
            }

            if (timeout >= 0) {
                if (didsomething) {
                    return kAooOk;
                } else {
                    // wait some more
                    remaining -= sleep;
                    if (remaining <= 0) {
                        return kAooErrorWouldBlock;
                    }
                    // continue
                }
            }
        }

        // NB: in non-blocking mode, close() will be called in setup()!
        // QUESTION: why not close here when quit_ is true?
        if (timeout < 0) {
            close();
        }

        return kAooOk;
    } catch (const net::error& e) {
        close();

        return e.code();
    }
}

AOO_API AooError AOO_CALL AooClient_stop(AooClient *client){
    return client->stop();
}

AooError AOO_CALL aoo::net::Client::stop(){
    quit_.store(true);
    // signal send thread
    notify();
    // signal UPD receive thread
    udp_client_.stop();
    // signal TCP thread
    if (!signal()){
        // force wakeup by closing the socket.
        // this is not nice and probably undefined behavior,
        // the MSDN docs explicitly forbid it!
        event_socket_.close();
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_send(
    AooClient *client, AooSeconds timeout){
    return client->send(timeout);
}

AooError AOO_CALL aoo::net::Client::send(AooSeconds timeout)
{
    constexpr double interval = 0.1;

    while (!quit_.load(std::memory_order_relaxed)) {
        auto now = time_tag::now();

    #if AOO_CLIENT_SIMULATE
        auto reply = simulate_.wrap(udp_sendfn_, now);
    #else
        auto reply = udp_sendfn_;
    #endif

        // send sources and sinks
        {
            sync::scoped_shared_lock lock(source_sink_mutex_);
            for (auto& s : sources_){
                s.source->send(reply.fn(), reply.user());
            }
            for (auto& s : sinks_){
                s.sink->send(reply.fn(), reply.user());
            }
        }

        // send server/peer messages
        if (state_.load() != client_state::disconnected) {
            udp_client_.update(*this, reply, now);

            // NB: if we use a seqlock, we probably do not have
            // to pass the settings to the send() method.
            peer_settings_lock_.lock();
            auto settings = peer_ping_settings_;
            peer_settings_lock_.unlock();

            // update peers
            peer_lock lock(peers_);
            for (auto& p : peers_){
                p.send(*this, reply, now, settings);
            }
        }

        if (timeout >= 0) {
            return kAooOk;
        } else {
            send_event_.wait_for(interval);
        }
    }

    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_receive(
    AooClient *client, AooSeconds timeout){
    return client->receive(timeout);
}

AooError AOO_CALL aoo::net::Client::receive(AooSeconds timeout) {
    return udp_client_.receive(timeout);
}

AOO_API AooError AOO_CALL AooClient_notify(AooClient *client)
{
    return client->notify();
}

AooError AOO_CALL aoo::net::Client::notify() {
    send_event_.set();
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_handlePacket(
    AooClient *client, const AooByte *data,
    AooInt32 size, const void *addr, AooAddrSize len)
{
    return client->handlePacket(data, size, addr, len);
}

AooError AOO_CALL aoo::net::Client::handlePacket(
    const AooByte *data, AooInt32 size,
    const void *addr, AooAddrSize len)
{
    AooMsgType type;
    AooId id;
    AooInt32 onset;
    auto err = aoo_parsePattern(data, size, &type, &id, &onset);
    if (err != kAooOk) {
        // pass to user-provided default message handler
        if (message_handler_ &&
            message_handler_(user_data_, data, size, addr, len) == kAooOk) {
            return kAooOk;
        } else {
            LOG_WARNING("AooClient: not an AOO message!");
            // TODO: why not return 'e'?
            return kAooErrorBadFormat;
        }
    }

    if (type == kAooMsgTypeSource){
        // forward to matching source
        sync::scoped_shared_lock lock(source_sink_mutex_);
        for (auto& s : sources_){
            if (s.id == id){
                return s.source->handleMessage(data, size, addr, len);
            }
        }
        LOG_WARNING("AooClient: handle_message(): source not found");
        return kAooErrorNotFound;
    } else if (type == kAooMsgTypeSink){
        // forward to matching sink
        sync::scoped_shared_lock lock(source_sink_mutex_);
        for (auto& s : sinks_){
            if (s.id == id){
                return s.sink->handleMessage(data, size, addr, len);
            }
        }
        LOG_WARNING("AooClient: handle_message(): sink not found");
        return kAooErrorNotFound;
    } else {
        // forward to UDP client
        ip_address address((const sockaddr *)addr, len);
        if (binmsg_check(data, size)) {
            return udp_client_.handle_bin_message(*this, data, size, address, type, onset);
        } else {
            return udp_client_.handle_osc_message(*this, data, size, address, type, onset);
        }
    }
}

AOO_API AooError AOO_CALL AooClient_sendPacket(
    AooClient *client, const AooByte *data,
    AooInt32 size, const void *addr, AooAddrSize len)
{
    return client->sendPacket(data, size, addr, len);
}

AooError AOO_CALL aoo::net::Client::sendPacket(
    const AooByte *data, AooInt32 size,
    const void *addr, AooAddrSize len)
{
    auto cmd = std::make_unique<packet_cmd>(
        data, size, ip_address((const sockaddr *)addr, len));
    push_command(std::move(cmd));

    notify(); // !

    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_setEventHandler(
    AooClient *sink, AooEventHandler fn, void *user, AooEventMode mode)
{
    return sink->setEventHandler(fn, user, mode);
}

AooError AOO_CALL aoo::net::Client::setEventHandler(
    AooEventHandler fn, void *user, AooEventMode mode)
{
    event_handler_ = fn;
    event_context_ = user;
    event_mode_ = (AooEventMode)mode;
    return kAooOk;
}

AOO_API AooBool AOO_CALL AooClient_eventsAvailable(AooClient *client){
    return client->eventsAvailable();
}

AooBool AOO_CALL aoo::net::Client::eventsAvailable(){
    return !event_queue_.empty();
}

AOO_API AooError AOO_CALL AooClient_pollEvents(AooClient *client){
    return client->pollEvents();
}

AooError AOO_CALL aoo::net::Client::pollEvents(){
    // always thread-safe
    event_handler fn(event_handler_, event_context_, kAooThreadLevelUnknown);
    event_ptr e;
    while (event_queue_.pop(e)){
        e->dispatch(fn);
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_addSource(
        AooClient *client, AooSource *src)
{
    return client->addSource(src);
}

AooError AOO_CALL aoo::net::Client::addSource(AooSource *src)
{
    assert(src);
    AooId id;
    src->getId(id);
    sync::scoped_lock lock(source_sink_mutex_); // writer lock!
#if 1
    for (auto& s : sources_){
        if (s.source == src){
            LOG_ERROR("AooClient: source already added");
            return kAooErrorAlreadyExists;
        } else if (s.id == id){
            LOG_WARNING("AooClient: source with id " << id
                        << " already added!");
            return kAooErrorAlreadyExists;
        }
    }
#endif
    sources_.push_back({ src, id });
    src->control(kAooCtlSetClient,
                 reinterpret_cast<intptr_t>(this), nullptr, 0);
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_removeSource(
        AooClient *client, AooSource *src)
{
    return client->removeSource(src);
}

AooError AOO_CALL aoo::net::Client::removeSource(AooSource *src)
{
    sync::scoped_lock lock(source_sink_mutex_); // writer lock!
    for (auto it = sources_.begin(); it != sources_.end(); ++it){
        if (it->source == src){
            sources_.erase(it);
            src->control(kAooCtlSetClient, 0, nullptr, 0);
            return kAooOk;
        }
    }
    LOG_ERROR("AooClient: source not found");
    return kAooErrorNotFound;
}

AOO_API AooError AOO_CALL AooClient_removeAllSources(AooClient *client)
{
    return client->removeAllSources();
}

AooError AOO_CALL aoo::net::Client::removeAllSources()
{
    sync::scoped_lock lock(source_sink_mutex_); // writer lock!
    for (auto& src : sources_) {
        src.source->control(kAooCtlSetClient, 0, nullptr, 0);
    }
    sources_.clear();
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_addSink(
        AooClient *client, AooSink *sink)
{
    return client->addSink(sink);
}

AooError AOO_CALL aoo::net::Client::addSink(AooSink *sink)
{
    assert(sink);
    AooId id;
    sink->getId(id);
    sync::scoped_lock lock(source_sink_mutex_); // writer lock!
#if 1
    for (auto& s : sinks_){
        if (s.sink == sink){
            LOG_ERROR("AooClient: sink already added");
            return kAooErrorAlreadyExists;
        } else if (s.id == id){
            LOG_WARNING("AooClient: sink with id " << id
                        << " already added!");
            return kAooErrorAlreadyExists;
        }
    }
#endif
    sinks_.push_back({ sink, id });
    sink->control(kAooCtlSetClient,
                  reinterpret_cast<intptr_t>(this), nullptr, 0);
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_removeSink(
        AooClient *client, AooSink *sink)
{
    return client->removeSink(sink);
}

AooError AOO_CALL aoo::net::Client::removeSink(AooSink *sink)
{
    sync::scoped_lock lock(source_sink_mutex_); // writer lock!
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it){
        if (it->sink == sink){
            sinks_.erase(it);
            sink->control(kAooCtlSetClient, 0, nullptr, 0);
            return kAooOk;
        }
    }
    LOG_ERROR("AooClient: sink not found");
    return kAooErrorNotFound;
}

AOO_API AooError AOO_CALL AooClient_removeAllSinks(AooClient *client)
{
    return client->removeAllSinks();
}

AooError AOO_CALL aoo::net::Client::removeAllSinks()
{
    sync::scoped_lock lock(source_sink_mutex_); // writer lock!
    for (auto& sink : sinks_) {
        sink.sink->control(kAooCtlSetClient, 0, nullptr, 0);
    }
    sinks_.clear();
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_connect(
        AooClient *client, const AooClientConnect *args,
        AooResponseHandler cb, void *context) {
    assert(args);
    return client->connect(*args, cb, context);
}

AooError AOO_CALL aoo::net::Client::connect(const AooClientConnect& args,
                                            AooResponseHandler cb, void *context) {
    if (!args.hostName || args.port == 0) {
        return kAooErrorBadArgument;
    }
    auto cmd = std::make_unique<connect_cmd>(args, cb, context);
    push_command(std::move(cmd));
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_disconnect(
        AooClient *client, AooResponseHandler cb, void *context) {
    return client->disconnect(cb, context);
}

AooError AOO_CALL aoo::net::Client::disconnect(AooResponseHandler cb, void *context) {
    auto cmd = std::make_unique<disconnect_cmd>(cb, context);
    push_command(std::move(cmd));
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_joinGroup(
        AooClient *client, const AooClientJoinGroup *args,
        AooResponseHandler cb, void *context) {
    assert(args);
    return client->joinGroup(*args, cb, context);
}

AooError AOO_CALL aoo::net::Client::joinGroup(const AooClientJoinGroup& args,
                                              AooResponseHandler cb, void *context) {
    if (!args.groupName || !args.userName) {
        return kAooErrorBadArgument;
    }
    auto cmd = std::make_unique<group_join_cmd>(args, cb, context);
    push_command(std::move(cmd));
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_leaveGroup(
        AooClient *client, AooId group, AooResponseHandler cb, void *context) {
    return client->leaveGroup(group, cb, context);
}

AooError AOO_CALL aoo::net::Client::leaveGroup(
        AooId group, AooResponseHandler cb, void *context) {
    auto cmd = std::make_unique<group_leave_cmd>(group, cb, context);
    push_command(std::move(cmd));
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_updateGroup(
        AooClient *client, AooId groupId, const AooData *groupMetadata,
        AooResponseHandler cb, void *context) {
    if (!groupMetadata) {
        return kAooErrorBadArgument;
    }
    return client->updateGroup(groupId, *groupMetadata, cb, context);
}

AooError AOO_CALL aoo::net::Client::updateGroup(
        AooId groupId, const AooData& groupMetadata,
        AooResponseHandler cb, void *context) {
    auto cmd = std::make_unique<group_update_cmd>(groupId, groupMetadata, cb, context);
    push_command(std::move(cmd));
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_updateUser(
        AooClient *client, AooId groupId, const AooData *userMetadata,
        AooResponseHandler cb, void *context) {
    if (!userMetadata) {
        return kAooErrorBadArgument;
    }
    return client->updateUser(groupId, *userMetadata, cb, context);
}

AooError AOO_CALL aoo::net::Client::updateUser(
        AooId groupId, const AooData& userMetadata,
        AooResponseHandler cb, void *context) {
    auto cmd = std::make_unique<user_update_cmd>(groupId, userMetadata, cb, context);
    push_command(std::move(cmd));
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_customRequest(
        AooClient *client, const AooData *data, AooFlag flags,
        AooResponseHandler cb, void *context) {
    if (!data) {
        return kAooErrorBadArgument;
    }
    return client->customRequest(*data, flags, cb, context);
}

AooError AOO_CALL aoo::net::Client::customRequest(
        const AooData& data, AooFlag flags, AooResponseHandler cb, void *context) {
    auto cmd = std::make_unique<custom_request_cmd>(data, flags, cb, context);
    push_command(std::move(cmd));
    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_findGroupByName(
    AooClient *client, const AooChar *name, AooId *id)
{
    return client->findGroupByName(name, id);
}

AooError AOO_CALL aoo::net::Client::findGroupByName(
    const char *name, AooId *id)
{
    sync::scoped_lock<sync::mutex> lock(group_mutex_);
    auto grp = find_group_membership(name);
    if (grp) {
        *id = grp->group_id;
        return kAooOk;
    } else {
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooClient_getGroupName(
    AooClient *client, AooId group, AooChar *buffer, AooSize *size)
{
    return client->getGroupName(group, buffer, size);
}

AooError AOO_CALL aoo::net::Client::getGroupName(
    AooId group, AooChar *buffer, AooSize *size)
{
    sync::scoped_lock<sync::mutex> lock(group_mutex_);
    auto grp = find_group_membership(group);
    if (grp) {
        auto nbytes = grp->group_name.size() + 1;
        if (*size >= nbytes) {
            memcpy(buffer, grp->group_name.c_str(), nbytes);
            *size = nbytes - 1; // exclude the 0 character!
            return kAooOk;
        } else {
            return kAooErrorInsufficientBuffer;
        }
    } else {
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooClient_findPeerByName(
        AooClient *client, const AooChar *group, const AooChar *user,
        AooId *groupId, AooId *userId, void *address, AooAddrSize *addrlen)
{
    return client->findPeerByName(group, user, groupId, userId, address, addrlen);
}

AooError AOO_CALL aoo::net::Client::findPeerByName(
        const char *group, const char *user, AooId *groupId, AooId *userId,
        void *address, AooAddrSize *addrlen)
{
    peer_lock lock(peers_);
    for (auto& p : peers_){
        if (p.match(group, user)){
            if (groupId) {
                *groupId = p.group_id();
            }
            if (userId) {
                *userId = p.user_id();
            }
            if (address && addrlen) {
                auto addr = p.address();
                if (addr.valid()) {
                    if (*addrlen >= addr.length()) {
                        memcpy(address, addr.address(), addr.length());
                        *addrlen = addr.length();
                    } else {
                        return kAooErrorInsufficientBuffer;
                    }
                } else {
                    // TODO: maybe return kAooErrorNotInitialized?
                    *addrlen = 0;
                }
            }
            return kAooOk;
        }
    }
    return kAooErrorNotFound;
}

AOO_API AooError AOO_CALL AooClient_findPeerByAddress(
        AooClient *client, const void *address, AooAddrSize addrlen,
        AooId *groupId, AooId *userId) {
    return client->findPeerByAddress(address, addrlen, groupId, userId);
}

AooError AOO_CALL aoo::net::Client::findPeerByAddress(
        const void *address, AooAddrSize addrlen, AooId *groupId, AooId *userId)
{
    ip_address addr((const struct sockaddr *)address, addrlen);
    peer_lock lock(peers_);
    for (auto& p : peers_){
        if (p.match(addr)) {
            if (groupId) {
                *groupId = p.group_id();
            }
            if (userId) {
                *userId = p.user_id();
            }
            return kAooOk;
        }
    }
    return kAooErrorNotFound;
}

AOO_API AooError AOO_CALL AooClient_getPeerName(
        AooClient *client, AooId group, AooId user,
        AooChar *groupNameBuffer, AooSize *groupNameSize,
        AooChar *userNameBuffer, AooSize *userNameSize) {
    return client->getPeerName(group, user, groupNameBuffer, groupNameSize,
                               userNameBuffer, userNameSize);
}

AooError AOO_CALL aoo::net::Client::getPeerName(
        AooId group, AooId user,
        AooChar *groupNameBuffer, AooSize *groupNameSize,
        AooChar *userNameBuffer, AooSize *userNameSize)
{
    peer_lock lock(peers_);
    for (auto& p : peers_){
        if (p.match(group, user)) {
            if (groupNameBuffer && groupNameSize) {
                auto size = p.group_name().size() + 1;
                if (*groupNameSize >= size) {
                    memcpy(groupNameBuffer, p.group_name().c_str(), size);
                    *groupNameSize = size - 1; // exclude the 0 character!
                } else {
                    return kAooErrorInsufficientBuffer;
                }
            }
            if (userNameBuffer && userNameSize) {
                auto size = p.user_name().size() + 1;
                if (*userNameSize >= size) {
                    memcpy(userNameBuffer, p.user_name().c_str(), size);
                    *userNameSize = size - 1; // exclude the 0 character!
                } else {
                    return kAooErrorInsufficientBuffer;
                }
            }
            return kAooOk;
        }
    }
    return kAooErrorNotFound;
}

AOO_API AooError AOO_CALL AooClient_sendMessage(
    AooClient *client, AooId group, AooId user,
    const AooData *message, AooNtpTime timeStamp, AooFlag flags)
{
    if (!message) {
        return kAooErrorBadArgument;
    }
    return client->sendMessage(group, user, *message, timeStamp, flags);
}

AooError AOO_CALL aoo::net::Client::sendMessage(
    AooId group, AooId user, const AooData& msg,
    AooNtpTime timeStamp, AooFlag flags)
{
    // if 'group' is a wildcard, so must be 'user'!
    if (group == kAooIdInvalid && user != kAooIdInvalid) {
        return kAooErrorBadArgument;
    }
    // TODO: check group membership? See Server::notifyGroup()
    bool reliable = flags & kAooMessageReliable;
    message m(group, user, timeStamp, msg, reliable);
    udp_client_.queue_message(std::move(m));

    notify(); // !

    return kAooOk;
}

AOO_API AooError AOO_CALL AooClient_sendRequest(
        AooClient *client, const AooRequest *request,
        AooResponseHandler callback, void *user) {
    if (!request) {
        return kAooErrorBadArgument;
    }
    return client->sendRequest(*request, callback, user);
}

AooError AOO_CALL aoo::net::Client::sendRequest(
        const AooRequest& request, AooResponseHandler callback, void *user)
{
    LOG_ERROR("AooClient: unknown request " << request.type);
    return kAooErrorNotImplemented;
}

AOO_API AooError AOO_CALL AooClient_control(
        AooClient *client, AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    return client->control(ctl, index, ptr, size);
}

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

#define CHECKARG(type) assert(size == sizeof(type))

AooError AOO_CALL aoo::net::Client::control(
        AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    switch(ctl) {
    // packetsize
    case kAooCtlSetPacketSize:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = kMessageHeaderSize + 64;
        auto packetsize = as<int32_t>(ptr);
        if (packetsize < minpacketsize){
            LOG_WARNING("AooSink: packet size too small! setting to " << minpacketsize);
            packet_size_.store(minpacketsize);
        } else if (packetsize > AOO_MAX_PACKET_SIZE){
            LOG_WARNING("AooSink: packet size too large! setting to " << AOO_MAX_PACKET_SIZE);
            packet_size_.store(AOO_MAX_PACKET_SIZE);
        } else {
            packet_size_.store(packetsize);
        }
        break;
    }
    case kAooCtlGetPacketSize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packet_size_.load();
        break;
    case kAooCtlSetBinaryFormat:
        CHECKARG(AooBool);
        binary_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetBinaryFormat:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = binary_.load();
        break;
    case kAooCtlSetPingSettings:
        CHECKARG(AooPingSettings);
        if (index == 0) {
            peer_settings_lock_.lock();
            peer_ping_settings_ = as<AooPingSettings>(ptr);
            peer_settings_lock_.unlock();
        } else if (index == 1) {
            server_settings_lock_.lock();
            server_ping_settings_ = as<AooPingSettings>(ptr);
            server_settings_lock_.unlock();
        } else {
            return kAooErrorBadArgument;
        }
        break;
    case kAooCtlGetPingSettings:
        CHECKARG(AooPingSettings);
        if (index == 0) {
            peer_settings_lock_.lock();
            as<AooPingSettings>(ptr) = peer_ping_settings_;
            peer_settings_lock_.unlock();
        } else if (index == 1) {
            server_settings_lock_.lock();
            as<AooPingSettings>(ptr) = server_ping_settings_;
            server_settings_lock_.unlock();
        } else {
            return kAooErrorBadArgument;
        }
        break;
    case kAooCtlAddInterfaceAddress:
    {
        auto ifaddr = reinterpret_cast<const AooChar *>(index);
        if (ifaddr == nullptr) {
            return kAooErrorBadArgument;
        }
        // test address
        ip_address addr(ifaddr, 0);
        if (!addr.valid() || addr.is_ipv4_mapped()) {
            return kAooErrorBadFormat;
        }
        sync::scoped_lock lock(interface_mutex_);
        if (std::find(interfaces_.begin(), interfaces_.end(), ifaddr)
                == interfaces_.end()) {
            interfaces_.push_back(ifaddr);
        } else {
            return kAooErrorAlreadyExists;
        }
        break;
    }
    case kAooCtlRemoveInterfaceAddress:
    {
        sync::scoped_lock lock(interface_mutex_);
        auto ifaddr = reinterpret_cast<const AooChar *>(index);
        if (ifaddr != NULL) {
            if (auto it = std::find(interfaces_.begin(), interfaces_.end(), ifaddr);
                    it != interfaces_.end()) {
                interfaces_.erase(it);
            } else {
                return kAooErrorNotFound;
            }
        } else {
            // remove all
            interfaces_.clear();
        }
        break;
    }
    // private controls
    case kAooCtlNeedRelay:
    {
        CHECKARG(AooBool);
        auto ep = reinterpret_cast<const AooEndpoint *>(index);
        ip_address addr((sockaddr *)ep->address, ep->addrlen);
        peer_lock lock(peers_);
        for (auto& peer : peers_){
            if (peer.match(addr)){
                as<AooBool>(ptr) = peer.need_relay();
                return kAooOk;
            }
        }
        return kAooErrorNotFound;
    }
    case kAooCtlGetRelayAddress:
    {
        CHECKARG(ip_address);
        auto ep = reinterpret_cast<const AooEndpoint *>(index);
        ip_address addr((sockaddr *)ep->address, ep->addrlen);
        peer_lock lock(peers_);
        for (auto& peer : peers_){
            if (peer.match(addr)){
                as<ip_address>(ptr) = peer.relay_address();
                return kAooOk;
            }
        }
        return kAooErrorNotFound;
    }
#if AOO_CLIENT_SIMULATE
    case kAooCtlSetSimulatePacketReorder:
        CHECKARG(AooSeconds);
        simulate_.set_packet_reorder(as<AooSeconds>(ptr));
        break;
    case kAooCtlSetSimulatePacketLoss:
        CHECKARG(float);
        simulate_.set_packet_loss(as<float>(ptr));
        break;
    case kAooCtlSetSimulatePacketJitter:
        CHECKARG(AooBool);
        simulate_.set_packet_jitter(as<AooBool>(ptr));
        break;
#endif
    default:
        LOG_WARNING("AooClient: unsupported control " << ctl);
        return kAooErrorNotImplemented;
    }
    return kAooOk;
}

namespace aoo {
namespace net {

bool Client::handle_peer_osc_message(const osc::ReceivedMessage& msg, int onset,
                                     const ip_address& addr)
{
    // all peer messages start with group ID and user ID, so we can easily
    // forward them to the corresponding peer.
    auto pattern = msg.AddressPattern() + onset;
    try {
        auto it = msg.ArgumentsBegin();
        auto group = (it++)->AsInt32();
        auto user = (it++)->AsInt32();
        auto remaining = msg.ArgumentCount() - 2;
        // forward to matching peer
        peer_lock lock(peers_);
        for (auto& p : peers_) {
            if (p.match(group, user)) {
                p.handle_osc_message(*this, pattern, it, remaining, addr);
                // notify send thread
                notify();
                return true;
            }
        }
        LOG_WARNING("AooClient: got " << pattern << " message from unknown peer "
                    << group << "|" << user << " " << addr);
        return false;
    } catch (const osc::Exception& e){
        LOG_ERROR("AooClient: got bad " << pattern
                  << " message from peer " << addr << ": " << e.what());
        return false;
    }
}

bool Client::handle_peer_bin_message(const AooByte *data, AooSize size, int onset,
                                     const ip_address& addr) {
    // all peer messages start with group ID and user ID, so we can easily
    // forward them to the corresponding peer.
    auto group = binmsg_group(data, size);
    auto user = binmsg_user(data, size);
    // forward to matching peer
    peer_lock lock(peers_);
    for (auto& p : peers_) {
        if (p.match(group, user)) {
            p.handle_bin_message(*this, data, size, onset, addr);
            // notify send thread
            notify();
            return true;
        }
    }
    LOG_WARNING("AooClient: got binary message from unknown peer "
                << group << "|" << user << " " << addr);
    return false;
}
//------------------ connect/login -----------------------//

void Client::perform(const connect_cmd& cmd)
{
    auto state = state_.load();
    if (state != client_state::disconnected){
        auto code = (state == client_state::connected) ?
            kAooErrorAlreadyConnected : kAooErrorRequestInProgress;

        cmd.reply_error(code);

        return;
    }

    ip_address_list addrlist;
    try {
        addrlist = ip_address::resolve(cmd.host_.name, cmd.host_.port,
                                            udp_client_.address_family(),
                                            udp_client_.use_ipv4_mapped());
    } catch (const resolve_error& e) {
        LOG_ERROR("AooClient: could not resolve hostname: " << e.what());
        cmd.reply_error(kAooErrorSystem, e.code(), e.what());
        return;
    }

    assert(connection_ == nullptr);
    assert(groups_.empty());
    connection_ = std::make_unique<connect_cmd>(cmd);

    LOG_DEBUG("AooClient: server address list:");
    for (auto& addr : addrlist){
        LOG_DEBUG("\t" << addr);
    }

    // prefer IPv4(-mapped) server address.
    // Typically, we need to contact the UDP server to obtain our public
    // IPv4(-mapped) address. In case the server address is IPv6-only,
    // we ping it nevertheless, e.g. in case we could not obtain our
    // global IPv6 address (for whatever reason).
    std::stable_partition(addrlist.begin(), addrlist.end(), [](auto& a) {
        return a.type() == ip_address::IPv4 || a.is_ipv4_mapped();
    });

    udp_client_.start_handshake(addrlist.front(), cmd.timeout_);
    // after start_handshake()! see udp_client::update()
    state_.store(client_state::handshake);
}

void Client::do_connect(const ip_host& server, AooSeconds timeout) {
    try {
        tcp_socket_ = tcp_socket(port_tag{}, 0);
    } catch (const socket_error& e) {
        LOG_ERROR("AooClient: couldn't create socket: " << e.what());
        throw;
    }

    auto type = tcp_socket_.family();
    ip_address_list addrlist;
    try {
        addrlist = ip_address::resolve(server.name, server.port, type, true);
    } catch (const resolve_error& e) {
        LOG_ERROR("AooClient: couldn't resolve host name: " << e.what());
        throw;
    }
    // sort IPv4(-mapped) first because it is more likely for an AOO server to be IP4-only than
    // to be IPv6-only
    std::stable_partition(addrlist.begin(), addrlist.end(), [](auto& a) {
        return a.type() == ip_address::IPv4 || a.is_ipv4_mapped();
    });

    LOG_INFO("AooClient: try to connect to " << server.name << " on port " << server.port);
    // try to connect to both addresses (just because the hostname resolves to IPv4
    // and IPv6 addresses does not mean that the AOO server actually supports both).
    socket_error err;
    for (auto& addr : addrlist) {
        LOG_DEBUG("AooClient: try to connect to " << addr);
        try {
            tcp_socket_.connect(addr, timeout);
            LOG_INFO("AooClient: successfully connected to " << addr);
            return; // success
        } catch (const socket_error& e) {
            err = e;
        }
    }
    LOG_ERROR("AooClient: couldn't connect to " << server.name << " on port "
              << server.port << ": " << err.what());
    throw err;
}

void Client::perform(const login_cmd& cmd) {
    assert(connection_ != nullptr);
    assert(groups_.empty());

    state_.store(client_state::connecting);

    try {
        do_connect(connection_->host_, connection_->timeout_);
    } catch (const socket_error& e) {
        // send error response and close connection
        connection_->reply_error(kAooErrorSocket, e.code(), e.what());
        close();
        return;
    }

    // send login request
    auto token = next_token_++;
    // create address list; start with local/global addresses
    ip_address_list addrlist;
    if (local_ipv4_addr_.valid()) {
        addrlist.push_back(local_ipv4_addr_);
    }
#if AOO_USE_IPV6
    if (global_ipv6_addr_.valid()) {
        addrlist.push_back(global_ipv6_addr_);
    }
#endif
    // add public IP address
    if (cmd.public_ip_.valid()) {
        addrlist.push_back(cmd.public_ip_);
    }
    // add user provided interface addresses
    {
        sync::scoped_lock lock(interface_mutex_);
        for (auto& ifaddr : interfaces_) {
            ip_address addr(ifaddr, udp_client_.port());
            if (addr.valid()) {
                addrlist.push_back(addr);
            } else {
                LOG_ERROR("AooClient: ignore invalid interface address " << ifaddr);
            }
        }
    }

    LOG_DEBUG("AooClient: address list:");
    for (auto& addr : addrlist) {
        LOG_DEBUG("\t" << addr);
    }

    auto msg = start_server_message(connection_->metadata_.size());

    msg << osc::BeginMessage(kAooMsgServerLogin)
        << token << aoo_getVersionString()
        << encrypt(connection_->pwd_).c_str()
        << connection_->metadata_;
    // address list
    msg << (int32_t)addrlist.size();
    for (auto& addr : addrlist){
        msg << addr;
    }
    msg << osc::EndMessage;

    send_server_message(msg);
}

void Client::perform(const timeout_cmd& cmd) {
    if (connection_ && state_.load() == client_state::handshake) {
        // send error response and close connection
        connection_->reply_error(kAooErrorUDPHandshakeTimeout);
        close();
    }
}

//------------------ disconnect -----------------------//

void Client::perform(const disconnect_cmd& cmd) {
    auto state = state_.load();
    if (state != client_state::connected) {
        auto code = (state == client_state::disconnected) ?
                kAooErrorNotConnected : kAooErrorAlreadyConnected;

        cmd.reply_error(code);

        return;
    }

    close();

    AooResponseDisconnect response { AOO_RESPONSE_INIT(Disconnect, structSize) };

    cmd.reply((AooResponse&)response); // always succeeds
}

//------------------ group_join -----------------------//

void Client::perform(const group_join_cmd& cmd)
{
    if (state_.load() != client_state::connected) {
        cmd.reply_error(kAooErrorNotConnected);
        return;
    }
    // check if we're already a group member
    for (auto& g : groups_) {
        if (g.group_name == cmd.group_name_) {
            cmd.reply_error(kAooErrorAlreadyGroupMember);
            return;
        }
    }
    auto token = next_token_++;
    pending_requests_.emplace(token, std::make_unique<group_join_cmd>(cmd));

    auto group_pwd = encrypt(cmd.group_pwd_);
    auto user_pwd = encrypt(cmd.user_pwd_);

    auto msg = start_server_message(cmd.group_md_.size() + cmd.user_md_.size());
    msg << osc::BeginMessage(kAooMsgServerGroupJoin) << token
        << cmd.group_name_.c_str() << group_pwd.c_str() << cmd.group_md_
        << cmd.user_name_.c_str() << user_pwd.c_str() << cmd.user_md_
        << cmd.relay_
        << osc::EndMessage;

    send_server_message(msg);
}

void Client::handle_response(const group_join_cmd& cmd, const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    (it++)->AsInt32(); // skip token
    auto result = (it++)->AsInt32();
    if (result == kAooErrorNone) {
        auto group_id = (it++)->AsInt32();
        auto group_flags = (AooFlag)(it++)->AsInt32();
        auto group_md = osc_read_metadata(it); // optional
        auto user_id = (it++)->AsInt32();
        auto user_flags = (AooFlag)(it++)->AsInt32();
        auto user_md = osc_read_metadata(it); // optional
        auto private_md = osc_read_metadata(it); // optional
        auto relay = osc_read_host(it); // optional

        // add group membership
        if (!find_group_membership(cmd.group_name_)) {
            group_membership m { cmd.group_name_, cmd.user_name_, group_id, user_id, {} };

            // add relay servers (in descending priority)
            auto family = udp_client_.address_family();
            auto ipv4mapped = udp_client_.use_ipv4_mapped();
            // 1) our own relay
            if (cmd.relay_.valid()) {
                try {
                    auto addrlist = ip_address::resolve(cmd.relay_.name, cmd.relay_.port,
                                                        family, ipv4mapped);
                    m.relay_list.insert(m.relay_list.end(), addrlist.begin(), addrlist.end());
                } catch (const resolve_error& e) {
                    LOG_WARNING("AooClient: cannot resolve group relay host '" << cmd.relay_.name << "'");
                }
            }
            // 2) server group relay
            if (relay) {
                if (*relay->hostName) {
                    try {
                        auto addrlist = ip_address::resolve(relay->hostName, relay->port,
                                                            family, ipv4mapped);
                        m.relay_list.insert(m.relay_list.end(), addrlist.begin(), addrlist.end());
                    } catch (const resolve_error& e) {
                        LOG_WARNING("AooClient: cannot resolve server relay host '" << relay->hostName << "'");
                    }
                } else {
                    // replace missing hostname with server IP address(es)
                    auto& host = connection_->host_;
                    try {
                        auto addrlist = ip_address::resolve(host.name, host.port, family, ipv4mapped);
                        for (auto& addr : addrlist) {
                            m.relay_list.emplace_back(addr.name(), relay->port);
                        }
                    } catch (const resolve_error& e) {
                        LOG_WARNING("AooClient: cannot resolve server relay host '" << host.name << "'");
                    }
                }
            }
            // we only have to lock for findGroupByName() and getGroupName()
            sync::scoped_lock<sync::mutex> lock(group_mutex_);
            groups_.push_back(std::move(m));
        } else {
            // shouldn't happen...
            LOG_ERROR("AooClient: group join response: already a member of group " << cmd.group_name_);
            cmd.reply_error(kAooErrorAlreadyGroupMember);
            return;
        }

        AooResponseGroupJoin response; // default constructor
        response.groupId = group_id;
        response.groupFlags = group_flags;
        if (group_md) {
            response.groupMetadata = &group_md.value();
        }
        response.userId = user_id;
        response.userFlags = user_flags;
        if (user_md) {
            response.userMetadata = &user_md.value();
        }
        if (private_md) {
            response.privateMetadata = &private_md.value();
        }
        if (relay) {
            response.relayAddress = &relay.value();
        }

        cmd.reply((AooResponse&)response);
        LOG_INFO("AooClient: successfully joined group " << cmd.group_name_);
    } else {
        auto code = (it++)->AsInt32();
        auto msg = (it++)->AsString();
        cmd.reply_error(result, code, msg);
        LOG_WARNING("AooClient: couldn't join group " << cmd.group_name_ << ": "
                    << response_error_message(result, code, msg));
    }
}

//------------------ group_leave -----------------------//

void Client::perform(const group_leave_cmd& cmd)
{
    // first check for group membership
    if (find_group_membership(cmd.group_) == nullptr) {
        LOG_WARNING("AooClient: couldn't leave group " << cmd.group_ << ": not a group member");
        cmd.reply_error(kAooErrorNotGroupMember);
        return;
    }

    auto token = next_token_++;
    pending_requests_.emplace(token, std::make_unique<group_leave_cmd>(cmd));

    auto msg = start_server_message();

    msg << osc::BeginMessage(kAooMsgServerGroupLeave)
        << token << cmd.group_ << osc::EndMessage;

    send_server_message(msg);
}

void Client::handle_response(const group_leave_cmd& cmd, const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    (it++)->AsInt32(); // skip token
    auto result = (it++)->AsInt32();
    if (result == kAooErrorNone) {
        // remove all peers from this group
        peer_lock lock(peers_);
        for (auto it = peers_.begin(); it != peers_.end(); ){
            if (it->match(cmd.group_)){
                it = peers_.erase(it);
            } else {
                ++it;
            }
        }
        lock.unlock();

        // remove group membership
        auto grp = std::find_if(groups_.begin(), groups_.end(),
                                [&](auto& g) { return g.group_id == cmd.group_; });
        if (grp != groups_.end()) {
            // we only have to lock for findGroupByName() and getGroupName()
            sync::scoped_lock<sync::mutex> lock(group_mutex_);
            groups_.erase(grp);
        } else {
            LOG_ERROR("AooClient: group leave response: not a member of group " << cmd.group_);
        }

        AooResponseGroupLeave response; // default constructor

        cmd.reply((AooResponse&)response);
        LOG_INFO("AooClient: successfully left group " << cmd.group_);
    } else {
        auto code = (it++)->AsInt32();
        auto msg = (it++)->AsString();
        cmd.reply_error(result, code, msg);
        LOG_WARNING("AooClient: couldn't leave group " << cmd.group_ << ": "
                    << response_error_message(result, code, msg));
    }
}

//------------------ group_update -----------------------//

void Client::perform(const group_update_cmd& cmd) {
    // first check for group membership
    if (find_group_membership(cmd.group_) == nullptr) {
        LOG_WARNING("AooClient: couldn't update group " << cmd.group_ << ": not a group member");
        cmd.reply_error(kAooErrorNotGroupMember);
        return;
    }

    auto token = next_token_++;
    pending_requests_.emplace(token, std::make_unique<group_update_cmd>(cmd));

    auto msg = start_server_message(cmd.md_.size());

    msg << osc::BeginMessage(kAooMsgServerGroupUpdate)
        << token << cmd.group_ << cmd.md_ << osc::EndMessage;

    send_server_message(msg);
}

void Client::handle_response(const group_update_cmd& cmd, const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    (it++)->AsInt32(); // skip token
    auto result = (it++)->AsInt32();
    if (result == kAooErrorNone) {
        AooResponseGroupUpdate response;
        response.groupMetadata.type = cmd.md_.type();
        response.groupMetadata.data = cmd.md_.data();
        response.groupMetadata.size = cmd.md_.size();

        cmd.reply((AooResponse&)response);
        LOG_INFO("AooClient: successfully updated group " << cmd.group_);
    } else {
        auto code = (it++)->AsInt32();
        auto msg = (it++)->AsString();
        cmd.reply_error(result, code, msg);
        LOG_WARNING("AooClient: could not update group " << cmd.group_ << ": "
                    << response_error_message(result, code, msg));
    }
}

//------------------ user_update -----------------------//

void Client::perform(const user_update_cmd& cmd) {
    // first check for group membership
    auto group = find_group_membership(cmd.group_);
    if (group == nullptr) {
        LOG_WARNING("AooClient: couldn't update user in group "
                    << cmd.group_ << ": not a group member");
        cmd.reply_error(kAooErrorNotGroupMember);
        return;
    }
    auto token = next_token_++;
    pending_requests_.emplace(token, std::make_unique<user_update_cmd>(cmd));

    auto msg = start_server_message(cmd.md_.size());

    msg << osc::BeginMessage(kAooMsgServerUserUpdate)
        << token << cmd.group_ << cmd.md_ << osc::EndMessage;

    send_server_message(msg);
}

void Client::handle_response(const user_update_cmd& cmd, const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    (it++)->AsInt32(); // skip token
    auto result = (it++)->AsInt32();
    if (result == kAooErrorNone) {
        AooResponseUserUpdate response;
        response.userMetadata.type = cmd.md_.type();
        response.userMetadata.data = cmd.md_.data();
        response.userMetadata.size = cmd.md_.size();

        cmd.reply((AooResponse&)response);
        LOG_INFO("AooClient: successfully updated user in group " << cmd.group_);
    } else {
        auto code = (it++)->AsInt32();
        auto msg = (it++)->AsString();
        cmd.reply_error(result, code, msg);
        LOG_WARNING("AooClient: could not update user in group " << cmd.group_ << ": "
                    << response_error_message(result, code, msg));
    }
}

//------------------ custom_request -----------------------//

void Client::perform(const custom_request_cmd& cmd) {
    if (state_.load() != client_state::connected) {
        cmd.reply_error(kAooErrorNotConnected);
        return;
    }

    auto token = next_token_++;
    pending_requests_.emplace(token, std::make_unique<custom_request_cmd>(cmd));

    auto msg = start_server_message(cmd.data_.size());

    msg << osc::BeginMessage(kAooMsgServerRequest)
        << token << (int32_t)cmd.flags_ << cmd.data_
        << osc::EndMessage;

    send_server_message(msg);
}

void Client::perform(const message& m, const sendfn& fn) {
    auto packet_size = packet_size_.load();
    auto binary = binary_.load();
    // LATER optimize this by overwriting the group ID and local user ID
    peer_lock lock(peers_);
    for (auto& peer : peers_) {
        if (peer.connected() && peer.match_wildcard(m.group_, m.user_)) {
            peer.send_message(m, fn, packet_size, binary);
        }
    }
}

void Client::send_event(event_ptr e)
{
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

void Client::push_command(command_ptr cmd){
    commands_.push(std::move(cmd));
    signal();
}

bool Client::wait_for_event(double timeout){
    // LOG_DEBUG("AooClient: wait " << timeout << " seconds");

    struct pollfd fds[2];
    fds[0].fd = event_socket_.native_handle();
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = tcp_socket_.native_handle();
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    // ceil timeout! -1: block indefinitely
    // NOTE: macOS requires the negative timeout to exactly -1!
#ifdef _WIN32
    int result = WSAPoll(fds, 2, timeout < 0 ? -1 : std::ceil(timeout * 1000.0));
#else
    int result = poll(fds, 2, timeout < 0 ? -1 : std::ceil(timeout * 1000.0));
#endif
    if (result == 0) {
        return false; // nothing to do or timeout
    } else if (result < 0) {
        int err = socket::get_last_error();
        if (err == EINTR) {
            return true;
        } else {
            // fatal error
            LOG_ERROR("AooClient: poll() failed: " << socket::strerror(err));
            throw error(kAooErrorSocket, "poll() failed");
        }
    }

    // event socket
    if (fds[0].revents) {
        try {
            // read empty packet
            char buf[64];
            event_socket_.receive(buf, sizeof(buf));
            // LOG_DEBUG("AooClient: got signalled");
        } catch (const socket_error& e) {
            LOG_ERROR("AooClient: failed to receive from event socket: " << e.what());
        }
    }

    // tcp socket
    if (tcp_socket_.is_open() && fds[1].revents){
        if (fds[1].revents & POLLERR) {
            LOG_DEBUG("AooClient: POLLERR");
        }
        if (fds[1].revents & POLLHUP) {
            LOG_DEBUG("AooClient: POLLHUP");
        }
        receive_data();
    }

    return true;
}

void Client::receive_data(){
    char buffer[AOO_MAX_PACKET_SIZE];
    try {
        auto result = tcp_socket_.receive(buffer, sizeof(buffer));
        if (result > 0) {
            try {
                receiver_.handle_message(buffer, result,
                        [&](const osc::ReceivedPacket& packet) {
                    osc::ReceivedMessage msg(packet);
                    handle_server_message(msg, packet.Size());
                });
            } catch (const osc::Exception& e) {
                LOG_ERROR("AooClient: exception in server TCP message: " << e.what());
                close_with_error(socket_error::abort);
            }
        } else {
            // connection closed by the remote server
            close_with_error(0);
        }
    } catch (const socket_error& e) {
        LOG_ERROR("AooClient: recv() failed: " << e.what());
        close_with_error(e.code());
    }
}

osc::OutboundPacketStream Client::start_server_message(size_t extra) {
    if (extra > 0) {
        auto total = AOO_MAX_PACKET_SIZE + extra;
        if (sendbuffer_.size() < total) {
            sendbuffer_.resize(total);
        }
    }
    // leave space for message size (int32_t)
    return osc::OutboundPacketStream(sendbuffer_.data() + 4, sendbuffer_.size() - 4);
}

void Client::send_server_message(const osc::OutboundPacketStream& msg) {
    if (!tcp_socket_.is_open()) {
        LOG_ERROR("AooClient: send_server_message: invalid socket");
        return;
    }
    // prepend message size (int32_t)
    auto data = msg.Data() - 4;
    auto size = msg.Size() + 4;
    // we know that the buffer is not really constant
    aoo::to_bytes<int32_t>(msg.Size(), const_cast<char *>(data));

    size_t nbytes = 0;
    while (nbytes < size){
        try {
            auto result = tcp_socket_.send(data + nbytes, size - nbytes);
            nbytes += result;
            // LOG_DEBUG("AooClient: sent " << res << " bytes");
        } catch (const socket_error& e) {
            LOG_ERROR("AooClient: send() failed: " << e.what());
            close_with_error(e.code());
            return;
        }
    }
    LOG_DEBUG("AooClient: sent " << (data + 4) << " to server");
}

void Client::handle_server_message(const osc::ReceivedMessage& msg, int32_t n){
    AooMsgType type;
    int32_t onset;
    auto err = parse_pattern((const AooByte *)msg.AddressPattern(), n, type, onset);
    if (err != kAooOk){
        LOG_WARNING("AooClient: not an AOO NET message!");
        return;
    }

    try {
        if (type == kAooMsgTypeClient){
            // now compare subpattern
            std::string_view pattern = msg.AddressPattern() + onset;
            LOG_DEBUG("AooClient: got message " << pattern << " from server");

            if (pattern == kAooMsgPing) {
                handle_ping(msg);
            } else if (pattern == kAooMsgPong) {
                handle_pong(msg);
            } else if (pattern == kAooMsgPeerJoin) {
                handle_peer_join(msg);
            } else if (pattern == kAooMsgPeerLeave) {
                handle_peer_leave(msg);
            } else if (pattern == kAooMsgPeerChanged) {
                handle_peer_changed(msg);
            } else if (pattern == kAooMsgLogin) {
                handle_login(msg);
            } else if (pattern == kAooMsgMessage) {
                handle_server_notification(msg);
            } else if (pattern == kAooMsgGroupChanged) {
                handle_group_changed(msg);
            } else if (pattern == kAooMsgUserChanged) {
                handle_user_changed(msg);
            } else if (pattern == kAooMsgGroupEject) {
                handle_group_eject(msg);
            } else if ((pattern == kAooMsgGroupJoin) ||
                       (pattern == kAooMsgGroupLeave) ||
                       (pattern == kAooMsgGroupUpdate) ||
                       (pattern == kAooMsgUserUpdate) ||
                       (pattern == kAooMsgRequest)) {
                // handle response
                auto token = msg.ArgumentsBegin()->AsInt32();
                auto it = pending_requests_.find(token);
                if (it != pending_requests_.end()) {
                    it->second->handle_response(*this, msg);
                    pending_requests_.erase(it);
                } else {
                    LOG_ERROR("AooClient: couldn't find matching request");
                }
            } else {
                LOG_WARNING("AooClient: got unspported server message " << msg.AddressPattern());
            }
        } else {
            LOG_WARNING("AooClient: got unsupported message " << msg.AddressPattern());
        }
    } catch (const osc::Exception& e){
        LOG_ERROR("AooClient: exception on handling TCP server message '" << msg.AddressPattern()
                  << "': " << e.what());
        close_with_error(socket_error::abort);
    }
}

void Client::handle_login(const osc::ReceivedMessage& msg){
    // make sure that state hasn't changed
    if (connection_) {
        auto it = msg.ArgumentsBegin();
        (it++)->AsInt32(); // skip token
        auto result = (it++)->AsInt32();
        if (result == kAooErrorNone){
            auto version = (it++)->AsString();
            auto id = (AooId)(it++)->AsInt32();
            (it++)->AsInt32(); // skip flags
            auto metadata = osc_read_metadata(it); // optional

            // check version
            if (auto err = check_version(version); err != kAooOk) {
                // send error response and close connection
                LOG_WARNING("AooClient: login failed: " << aoo_strerror(err));
                connection_->reply_error(err);
                close();
                return;
            }

            // connected!
            state_.store(client_state::connected);
            LOG_INFO("AooClient: successfully logged in (client ID: "
                     << id << ")");
            // start ping timer
            server_ping_timer_.reset();

            // notify
            AooResponseConnect response {
                AOO_REQUEST_INIT(Connect, metadata),
                id, version,
                metadata ? &metadata.value() : nullptr
            };

            connection_->reply((AooResponse&)response);
        } else {
            // send error response and close connection
            auto code = (it++)->AsInt32();
            auto msg = (it++)->AsString();
            LOG_WARNING("AooClient: login failed: "
                        << response_error_message(result, code, msg));
            connection_->reply_error(result, code, msg);
            close();
        }
    }
}

void Client::handle_server_notification(const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    auto data = osc_read_metadata(it);
    if (!data) {
        throw osc::MalformedMessageException("missing data");
    }

    auto e = std::make_unique<notification_event>(*data);
    send_event(std::move(e));

    LOG_DEBUG("AooClient: received server notification (" << data->type << ")");
}

void Client::handle_group_eject(const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    auto group = (it++)->AsInt32();

    // remove all peers from this group
    peer_lock lock(peers_);
    for (auto it = peers_.begin(); it != peers_.end(); ){
        if (it->match(group)){
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }
    lock.unlock();

    // remove group membership
    auto grp = std::find_if(groups_.begin(), groups_.end(),
                            [&](auto& m) { return m.group_id == group; });
    if (grp != groups_.end()) {
        // we only have to lock for findGroupByName() and getGroupName()
        sync::scoped_lock<sync::mutex> lock(group_mutex_);
        groups_.erase(grp);
    } else {
        LOG_ERROR("AooClient: group eject: not a member of group " << group);
    }

    auto e = std::make_unique<group_eject_event>(group);
    send_event(std::move(e));

    LOG_INFO("AooClient: ejected from group " << group);
}

void Client::handle_group_changed(const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    auto group = (it++)->AsInt32();
    auto user = (it++)->AsInt32();
    auto md = osc_read_metadata(it);
    if (!md) {
        throw osc::MalformedMessageException("missing data");
    }

    auto e = std::make_unique<group_update_event>(group, user, *md);
    send_event(std::move(e));

    LOG_INFO("AooClient: group " << group << " has been updated");
}

void Client::handle_user_changed(const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    auto group = (it++)->AsInt32();
    auto user = (it++)->AsInt32();
    auto md = osc_read_metadata(it);
    if (!md) {
        throw osc::MalformedMessageException("missing data");
    }

    auto e = std::make_unique<user_update_event>(group, user, *md);
    send_event(std::move(e));

    LOG_INFO("AooClient: user " << user << " has been updated");
}

static osc::ReceivedPacket unwrap_message(const osc::ReceivedMessage& msg, ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    // read address as is (always unmapped)
    addr = osc_read_address(it);

    const void *msg_data;
    osc::osc_bundle_element_size_t msg_size;
    (it++)->AsBlob(msg_data, msg_size);

    return osc::ReceivedPacket((const char *)msg_data, msg_size);
}

void Client::handle_peer_join(const osc::ReceivedMessage& msg){
    auto it = msg.ArgumentsBegin();
    auto group_name = (it++)->AsString();
    auto group_id = (it++)->AsInt32();
    auto user_name = (it++)->AsString();
    auto user_id = (it++)->AsInt32();
    auto version = (it++)->AsString();
    auto flags = (AooFlag)(it++)->AsInt32();
    auto metadata = osc_read_metadata(it); // optional
    auto relay = osc_read_host(it); // optional
    // IP addresses
    auto addrcount = (it++)->AsInt32();
    ip_address_list addrlist;
    for (int32_t i = 0; i < addrcount; ++i){
        // read as is! They might be used as identifiers in relay message.
        auto addr = osc_read_address(it);
        if (addr.is_ipv4_mapped()) {
            // peer addresses must be unmapped!
            LOG_WARNING("AooClient: ignore IPv4-mapped peer address " << addr);
            continue;
        }
        // filter local IPv4 addresses so that we don't accidentally ping ourselves!
        // (it is possible for peers in different networks to have the same local IPv4 address)
        // TODO: should we do the same for custom interface addresses?
        if (addr.valid() && addr != local_ipv4_addr_) {
            addrlist.push_back(addr);
        } else {
            LOG_DEBUG("AooClient: ignore local address " << addr);
        }
    }

    peer_lock lock(peers_);
    // check if peer already exists (shouldn't happen)
    for (auto& p: peers_) {
        if (p.match(group_id, user_id)){
            LOG_ERROR("AooClient: peer " << p << " already added");
            return;
        }
    }
    // find corresponding group
    auto membership = find_group_membership(group_name);
    if (!membership) {
        // shouldn't happen
        LOG_ERROR("AooClient: add peer from group " << group_name
                  << ", but we are not a group member");
        return; // ignore
    }

    // get user relay address(es)
    auto family = udp_client_.address_family();
    auto use_ipv4_mapped = udp_client_.use_ipv4_mapped();
    ip_address_list user_relay;
    if (relay) {
        if (*relay->hostName) {
            try {
                user_relay = aoo::ip_address::resolve(relay->hostName, relay->port,
                                                      family, use_ipv4_mapped);
            } catch (const resolve_error& e) {
                LOG_ERROR("AooClient: could not resolve peer relay host '" << relay->hostName << "'");
            }
        } else {
            // replace missing hostname with peer IP address(es).
            // (the relay should support the same families as the peer itself.)
            for (auto& addr : addrlist) {
                user_relay.emplace_back(addr.name(), relay->port);
            }
        }
        // add to group relay list
        auto& list = membership->relay_list;
        list.insert(list.end(), user_relay.begin(), user_relay.end());
    }
    auto md = metadata ? &metadata.value() : nullptr;
    auto local_id = membership->user_id;

    peer_args args {
        group_name, user_name, group_id, user_id, local_id,
        flags, version, md, family, use_ipv4_mapped, binary(),
        std::move(addrlist), std::move(user_relay), membership->relay_list
    };

    auto peer = peers_.emplace_front(std::move(args));

    auto e = std::make_unique<peer_event>(kAooEventPeerHandshake, *peer);
    send_event(std::move(e));

    LOG_INFO("AooClient: peer " << *peer << " joined");
}

void Client::handle_peer_leave(const osc::ReceivedMessage& msg){
    auto it = msg.ArgumentsBegin();
    auto group = (it++)->AsInt32();
    auto user = (it++)->AsInt32();

    peer_lock lock(peers_);
    auto peer = std::find_if(peers_.begin(), peers_.end(),
        [&](auto& p){ return p.match(group, user); });
    if (peer == peers_.end()){
        LOG_ERROR("AooClient: couldn't remove " << group << "|" << user);
        return;
    }

    // only send event if we're connected, which means
    // that an kAooEventPeerJoin event has been sent.
    if (peer->connected()){
        auto e = std::make_unique<peer_event>(kAooEventPeerLeave, *peer);
        send_event(std::move(e));
    }

    // find corresponding group
    auto membership = find_group_membership(group);
    if (!membership) {
        // shouldn't happen
        LOG_ERROR("AooClient: remove peer from group " << peer->group_name()
                  << ", but we are not a group member");
        return; // ignore
    }
    // remove user relay(s)
    for (auto& addr : peer->user_relay()) {
        // check if this relay is used by a peer
        for (auto& p : peers_) {
            if (p.need_relay() && p.relay_address() == addr) {
                LOG_WARNING(p << " uses a relay provided by " << *peer
                            << ", so the connection might stop working");
            }
        }
        // remove from group relay list
        auto& list = membership->relay_list;
        for (auto ptr = list.begin(); ptr != list.end(); ++ptr) {
            if (*ptr == addr) {
                list.erase(ptr);
                break;
            }
        }
    }

    peers_.erase(peer);

    LOG_INFO("AooClient: peer " << group << "|" << user << " left");
}

void Client::handle_peer_changed(const osc::ReceivedMessage& msg) {
    auto it = msg.ArgumentsBegin();
    auto group = (it++)->AsInt32();
    auto user = (it++)->AsInt32();
    auto md = osc_read_metadata(it);
    if (!md) {
        throw osc::MalformedMessageException("missing data");
    }

    peer_lock lock(peers_);
    for (auto& peer : peers_) {
        if (peer.match(group, user)) {
            auto e = std::make_unique<peer_update_event>(group, user, *md);
            send_event(std::move(e));

            LOG_INFO("AooClient: peer " << peer << " has been updated");

            return;
        }
    }

    LOG_WARNING("AooClient: peer " << group << "|" << user
                << " updated, but not found in list");
}

void Client::handle_ping(const osc::ReceivedMessage& msg) {
    LOG_DEBUG("AooClient: got TCP ping from server");

    // reply with /pong message
    auto reply = start_server_message();

    reply << osc::BeginMessage(kAooMsgServerPong)
        << osc::EndMessage;

    send_server_message(reply);

    LOG_DEBUG("AooClient: send TCP pong to server");
}

void Client::handle_pong(const osc::ReceivedMessage& msg) {
    LOG_DEBUG("AooClient: got TCP pong from server");
    server_ping_timer_.pong();
}

bool Client::signal() {
    // LOG_DEBUG("aoo_client signal");
    return event_socket_.signal();
}

void Client::close_with_error(int err) {
    auto notify = state_.load() == client_state::connected;

    close();

    if (notify) {
        auto msg = err != 0 ? "connection closed by server" : socket::strerror(err);
        auto e = std::make_unique<disconnect_event>(err, std::move(msg));
        send_event(std::move(e));
    }
}

void Client::close() {
    if (tcp_socket_.is_open()){
        tcp_socket_.close();
        LOG_INFO("AooClient: closed connection");
    }

    connection_ = nullptr;
    {
        // we only have to lock for findGroupByName() and getGroupName()
        sync::scoped_lock<sync::mutex> lock(group_mutex_);
        groups_.clear();
    }

    // remove all peers
    peer_lock lock(peers_);
    peers_.clear();

    // clear pending request
    pending_requests_.clear();

    state_.store(client_state::disconnected);
}

Client::group_membership * Client::find_group_membership(std::string_view name) {
    for (auto& g : groups_) {
        if (g.group_name == name) {
            return &g;
        }
    }
    return nullptr;
}

Client::group_membership * Client::find_group_membership(AooId id) {
    for (auto& m : groups_) {
        if (m.group_id == id) {
            return &m;
        }
    }
    return nullptr;
}

//---------------------- udp_client ------------------------//

AooError udp_client::setup(Client& client, AooClientSettings& settings) {
    // NB: socketType will be modified!
    auto& type = settings.socketType;
    if ((type & kAooSocketIPv4Mapped) &&
            (!(type & kAooSocketIPv6) || (type & kAooSocketIPv4))) {
        LOG_ERROR("AooClient: combination of setup flags not allowed");
        return kAooErrorBadArgument;
    }

    bool external = settings.options & kAooClientExternalUDPSocket;
    // external UDP socket needs IP flags and non-zero port number!
    if (external && (type == 0 || settings.portNumber == 0)) {
        return kAooErrorBadArgument;
    }
    if (type == 0) {
        type = kAooSocketDualStack; // default
    }

    if (!external) {
        // increase socket buffers
        const int sendbufsize = 1 << 18; // 256 KB
        udp_server_.set_send_buffer_size(sendbufsize);
        // NB: with a threaded UDP server we wouldn't need large receive buffers...
    #if 1
        // The receive thread may also do the sending (and encoding)! This requires a larger buffer.
        const int recvbufsize = 1 << 20; // 1 MB
    #else
        const int recvbufsize = 1 << 18; // 256 KB
    #endif
        udp_server_.set_receive_buffer_size(recvbufsize);

        try {
            // TODO: honor socket flags! For now, just use default.
            udp_server_.start(settings.portNumber,
                    [&client](const AooByte *data, AooSize size, const aoo::ip_address& addr){
                // TODO: error handling?
                client.handlePacket(data, size, addr.address(), addr.length());

            });
        } catch (const udp_error& e) {
            LOG_ERROR("AooClient: failed to start UDP socket: " << e.what());
            socket::set_last_error(e.code());
            return kAooErrorSocket;
        }
        // update socket flags
        type = udp_server_.socket().flags();
        // update port number if picked by the OS!
        if (settings.portNumber == 0) {
            settings.portNumber = udp_server_.port();
        }
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

    query_deadline_.clear();
    next_ping_time_.clear();

    return kAooOk;
}

AooError udp_client::receive(double timeout) {
    try {
        if (timeout >= 0) {
            return udp_server_.run(timeout) ? kAooOk : kAooErrorWouldBlock;
        } else {
            udp_server_.run(-1);
            return kAooOk;
        }
    } catch (udp_error& e) {
        LOG_ERROR("AooClient: UDP error: " << e.what());
        socket::set_last_error(e.code());
        return kAooErrorSocket;
    }
}

AooError udp_client::handle_bin_message(Client& client, const AooByte *data, int32_t size,
                                        const ip_address& addr, AooMsgType type, int32_t onset) {
    if (type == kAooMsgTypeRelay) {
        ip_address src;
        onset = binmsg_read_relay(data, size, src);
        if (onset > 0) {
        #if AOO_DEBUG_RELAY
            LOG_DEBUG("AooClient: handle binary relay message from " << src << " via " << addr);
        #endif
            auto msg = data + onset;
            auto msgsize = size - onset;
            return client.handlePacket(msg, msgsize, src.address(), src.length());
        } else {
            LOG_ERROR("AooClient: bad binary relay message");
            return kAooErrorBadFormat;
        }
    } else if (type == kAooMsgTypePeer) {
        // peer message
        //
        // NOTE: during the handshake process it is expected that
        // we receive UDP messages which we have to ignore:
        // a) pings from a peer which we haven't had the chance to add yet
        // b) pings sent to alternative endpoint addresses
        if (!client.handle_peer_bin_message(data, size, onset, addr)){
            LOG_INFO("AooClient: ignore UDP binary message from endpoint " << addr);
        }
        return kAooOk;
    } else {
        LOG_WARNING("AooClient: unsupported binary message");
        return kAooErrorBadFormat;
    }
}

AooError udp_client::handle_osc_message(Client& client, const AooByte *data, int32_t size,
                                        const ip_address& addr, AooMsgType type, int32_t onset) {
    try {
        osc::ReceivedPacket packet((const char *)data, size);
        osc::ReceivedMessage msg(packet);

        if (type == kAooMsgTypePeer){
            // peer message
            //
            // NOTE: during the handshake process it is expected that
            // we receive UDP messages which we have to ignore:
            // a) pings from a peer which we haven't had the chance to add yet
            // b) pings sent to alternative endpoint addresses
            if (!client.handle_peer_osc_message(msg, onset, addr)){
                LOG_INFO("AooClient: ignore UDP message " << msg.AddressPattern() + onset
                         << " from endpoint " << addr);
            }
        } else if (type == kAooMsgTypeClient){
            // server message
            if (is_server_address(addr)) {
                handle_server_message(client, msg, onset);
            } else {
                LOG_WARNING("AooClient: got OSC message from unknown server " << addr);
            }
        } else if (type == kAooMsgTypeRelay){
            ip_address src;
            auto packet = unwrap_message(msg, src);
            auto msg = (const AooByte *)packet.Contents();
            auto msgsize = packet.Size();
        #if AOO_DEBUG_RELAY
            LOG_DEBUG("AooClient: handle OSC relay message from " << src << " via " << addr);
        #endif
            return client.handlePacket(msg, msgsize, src.address(), src.length());
        } else {
            LOG_WARNING("AooClient: got unexpected message " << msg.AddressPattern());
            return kAooErrorNotImplemented;
        }

        return kAooOk;
    } catch (const osc::Exception& e){
        LOG_ERROR("AooClient: exception in handle_osc_message: " << e.what());
        return kAooErrorBadFormat;
    }
}

void udp_client::update(Client& client, const sendfn& fn, time_tag now){
    auto state = client.current_state();
    if (state == client_state::handshake) {
        // initialize timer; see start_handshake()
        if (start_handshake_.exchange(false)) {
            query_deadline_ = now + aoo::time_tag::from_seconds(query_timeout_.load());
            next_ping_time_ = now;
        }
        // check for time out
        if (now >= query_deadline_) {
            // handshake has timed out!
            auto cmd = std::make_unique<Client::timeout_cmd>();
            client.push_command(std::move(cmd));
            return;
        }
        // send handshake pings
        if (now >= next_ping_time_) {
            LOG_DEBUG("AooClient: send " << kAooMsgServerQuery);

            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage(kAooMsgServerQuery)
                << osc::EndMessage;

            send_server_message(msg, fn);

            next_ping_time_ += aoo::time_tag::from_seconds(client.query_interval());
        }
    } else if (state == client_state::connected) {
        // send regular pings
        // TODO: only do this when there are no (active) peers?
        if (now >= next_ping_time_) {
            LOG_DEBUG("AooClient: send UDP ping to server");

            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage(kAooMsgServerPing)
                << osc::EndMessage;

            send_server_message(msg, fn);

            next_ping_time_ += aoo::time_tag::from_seconds(client.ping_interval());
        }
    }

    // send outgoing peer/group messages
    messages_.consume_all([&](const auto& m) {
        client.perform(m, fn);
    });
}

void udp_client::start_handshake(const ip_address& remote,
                                 AooSeconds timeout) {
    LOG_DEBUG("AooClient: start UDP handshake with " << remote);
    if (timeout < 0) {
        timeout = max_query_timeout;
    }
    scoped_lock lock(addr_lock_);
    remote_addr_ = remote;
    got_address_ = false;
    query_timeout_.store(timeout); // before start_handshake_!
    start_handshake_.store(true);
}

void udp_client::queue_message(message&& m) {
    messages_.push(std::move(m));
}

void udp_client::send_server_message(const osc::OutboundPacketStream& msg, const sendfn& fn) {
    shared_lock lock(addr_lock_);
    auto addr = remote_addr_;
    lock.unlock();
    if (!addr.valid()) {
        LOG_ERROR("AooClient: no server address");
        return;
    }
    // send unlocked!
    fn((const AooByte *)msg.Data(), msg.Size(), addr);
}

void udp_client::handle_server_message(Client& client, const osc::ReceivedMessage& msg, int onset) {
    std::string_view pattern = msg.AddressPattern() + onset;
    LOG_DEBUG("AooClient: got server OSC message " << pattern);

    try {
        if (pattern == kAooMsgPong) {
            LOG_DEBUG("AooClient: got UDP pong from server");
        } else if (pattern == kAooMsgQuery) {
            handle_query(client, msg);
        } else {
            LOG_WARNING("AooClient: received unexpected UDP message "
                        << pattern << " from server");
        }
    } catch (const osc::Exception& e) {
        LOG_ERROR("AooClient: exception on handling " << pattern
                  << " message: " << e.what());
    }
}

void udp_client::handle_query(Client &client, const osc::ReceivedMessage &msg) {
    if (client.current_state() == client_state::handshake){
        auto it = msg.ArgumentsBegin();

        // read public address (make sure it is really unmapped)
        ip_address public_addr = osc_read_address(it).unmapped();
        unique_lock lock(addr_lock_);
        bool already_received = std::exchange(got_address_, true);
        lock.unlock();
        if (already_received) {
            LOG_DEBUG("AooClient: public address " << public_addr
                      << " already received");
            return; // already received
        }
        LOG_DEBUG("AooClient: public address: " << public_addr);

        // now we can try to login
        auto cmd = std::make_unique<Client::login_cmd>(public_addr);
        client.push_command(std::move(cmd));
    }
}

bool udp_client::is_server_address(const ip_address& addr){
    scoped_shared_lock lock(addr_lock_);
    return addr == remote_addr_;
}

} // net
} // aoo
