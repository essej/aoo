/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "client.hpp"

#include <cstring>
#include <functional>
#include <algorithm>
#include <sstream>

#include "md5/md5.h"

#ifndef _WIN32
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#define AOO_NET_MSG_SERVER_PING \
    AOO_MSG_DOMAIN AOO_NET_MSG_SERVER AOO_NET_MSG_PING

#define AOO_NET_MSG_PEER_PING \
    AOO_MSG_DOMAIN AOO_NET_MSG_PEER AOO_NET_MSG_PING

#define AOO_NET_MSG_PEER_REPLY \
    AOO_MSG_DOMAIN AOO_NET_MSG_PEER AOO_NET_MSG_REPLY

#define AOO_NET_MSG_PEER_MESSAGE \
    AOO_MSG_DOMAIN AOO_NET_MSG_PEER AOO_NET_MSG_MESSAGE

#define AOO_NET_MSG_SERVER_LOGIN \
    AOO_MSG_DOMAIN AOO_NET_MSG_SERVER AOO_NET_MSG_LOGIN

#define AOO_NET_MSG_SERVER_REQUEST \
    AOO_MSG_DOMAIN AOO_NET_MSG_SERVER AOO_NET_MSG_REQUEST

#define AOO_NET_MSG_SERVER_GROUP_JOIN \
    AOO_MSG_DOMAIN AOO_NET_MSG_SERVER AOO_NET_MSG_GROUP AOO_NET_MSG_JOIN

#define AOO_NET_MSG_SERVER_GROUP_LEAVE \
    AOO_MSG_DOMAIN AOO_NET_MSG_SERVER AOO_NET_MSG_GROUP AOO_NET_MSG_LEAVE

#define AOO_NET_MSG_GROUP_JOIN \
    AOO_NET_MSG_GROUP AOO_NET_MSG_JOIN

#define AOO_NET_MSG_GROUP_LEAVE \
    AOO_NET_MSG_GROUP AOO_NET_MSG_LEAVE

#define AOO_NET_MSG_PEER_JOIN \
    AOO_NET_MSG_PEER AOO_NET_MSG_JOIN

#define AOO_NET_MSG_PEER_LEAVE \
    AOO_NET_MSG_PEER AOO_NET_MSG_LEAVE

namespace aoo {

uint32_t make_version();
bool check_version(uint32_t version);

namespace net {

std::string encrypt(const std::string& input){
    uint8_t result[16];
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, (uint8_t *)input.data(), input.size());
    MD5_Final(result, &ctx);

    char output[33];
    for (int i = 0; i < 16; ++i){
        sprintf(&output[i * 2], "%02X", result[i]);
    }

    return output;
}

char * copy_string(const char * s){
    if (s){
        auto len = strlen(s);
        auto result = new char[len + 1];
        memcpy(result, s, len + 1);
        return result;
    } else {
        return nullptr;
    }
}

void * copy_sockaddr(const void *sa, int32_t len){
    if (sa){
        auto result = new char[len];
        memcpy(result, sa, len);
        return result;
    } else {
        return nullptr;
    }
}

} // net
} // aoo

/*//////////////////// OSC ////////////////////////////*/

int32_t aoo_net_parse_pattern(const char *msg, int32_t n, int32_t *type)
{
    int32_t offset = 0;
    if (n >= AOO_MSG_DOMAIN_LEN
            && !memcmp(msg, AOO_MSG_DOMAIN, AOO_MSG_DOMAIN_LEN))
    {
        offset += AOO_MSG_DOMAIN_LEN;
        if (n >= (offset + AOO_NET_MSG_SERVER_LEN)
            && !memcmp(msg + offset, AOO_NET_MSG_SERVER, AOO_NET_MSG_SERVER_LEN))
        {
            *type = AOO_TYPE_SERVER;
            return offset + AOO_NET_MSG_SERVER_LEN;
        }
        else if (n >= (offset + AOO_NET_MSG_CLIENT_LEN)
            && !memcmp(msg + offset, AOO_NET_MSG_CLIENT, AOO_NET_MSG_CLIENT_LEN))
        {
            *type = AOO_TYPE_CLIENT;
            return offset + AOO_NET_MSG_CLIENT_LEN;
        }
        else if (n >= (offset + AOO_NET_MSG_PEER_LEN)
            && !memcmp(msg + offset, AOO_NET_MSG_PEER, AOO_NET_MSG_PEER_LEN))
        {
            *type = AOO_TYPE_PEER;
            return offset + AOO_NET_MSG_PEER_LEN;
        } else {
            return 0;
        }
    } else {
        return 0; // not an AoO message
    }
}

/*//////////////////// AoO client /////////////////////*/

aoo_net_client * aoo_net_client_new(int socket) {
    return new aoo::net::client(socket);
}

aoo::net::client::client(int socket)
    : udpsocket_(socket)
{
    ip_address addr;
    if (socket_address(socket, addr) < 0){
        // TODO handle error
        socket_error_print("socket_address");
    } else {
        udpport_ = addr.port();
        type_ = addr.type();
    }
    eventsocket_ = socket_udp(0);
    if (eventsocket_ < 0){
        // TODO handle error
        socket_error_print("socket_udp");
    }
    commands_.resize(256, 1);
    messages_.resize(256, 1);
    events_.resize(256, 1);
    sendbuffer_.setup(65536);
    recvbuffer_.setup(65536);
}

void aoo_net_client_free(aoo_net_client *client){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::net::client *>(client);
}

aoo::net::client::~client() {
    if (tcpsocket_ >= 0){
        socket_close(tcpsocket_);
    }
}

int32_t aoo_net_client_run(aoo_net_client *client){
    return client->run();
}

int32_t aoo::net::client::run(){
    start_time_ = time_tag::now();

    while (!quit_.load()){
        double timeout = 0;

        time_tag now = time_tag::now();
        auto elapsed_time = time_tag::duration(start_time_, now);

        if (tcpsocket_ >= 0 && state_.load() == client_state::connected){
            auto delta = elapsed_time - last_tcp_ping_time_;
            auto ping_interval = ping_interval_.load();
            if (delta >= ping_interval){
                // send ping
                if (tcpsocket_ >= 0){
                    char buf[64];
                    osc::OutboundPacketStream msg(buf, sizeof(buf));
                    msg << osc::BeginMessage(AOO_NET_MSG_SERVER_PING)
                        << osc::EndMessage;

                    send_server_message_tcp(msg.Data(), msg.Size());
                } else {
                    LOG_ERROR("aoo_client: bug send_ping()");
                }

                last_tcp_ping_time_ = elapsed_time;
                timeout = ping_interval;
            } else {
                timeout = ping_interval - delta;
            }
        } else {
            timeout = -1;
        }

        if (!wait_for_event(timeout)){
            break;
        }

        // handle commands
        while (commands_.read_available()){
            std::unique_ptr<icommand> cmd;
            commands_.read(cmd);
            cmd->perform(*this);
        }
    }
    return 1;
}

int32_t aoo_net_client_quit(aoo_net_client *client){
    return client->quit();
}

int32_t aoo::net::client::quit(){
    quit_.store(true);
    if (!signal()){
        // force wakeup by closing the socket.
        // this is not nice and probably undefined behavior,
        // the MSDN docs explicitly forbid it!
        socket_close(udpsocket_);
    }
    return 1;
}

int32_t aoo_net_client_request(aoo_net_client *client,
                               aoo_net_request_type request, void *data,
                               aoo_net_callback callback, void *user) {
    return client->send_request(request, data, callback, user);
}

int32_t aoo::net::client::send_request(aoo_net_request_type request, void *data,
                                       aoo_net_callback callback, void *user){
    switch (request){
    case AOO_NET_CONNECT_REQUEST:
    {
        auto d = (aoo_net_connect_request *)data;
        do_connect(d->host, d->port, d->user_name, d->user_pwd, callback, user);
        break;
    }
    case AOO_NET_DISCONNECT_REQUEST:
        do_disconnect(callback, user);
        break;
    case AOO_NET_GROUP_JOIN_REQUEST:
    {
        auto d = (aoo_net_group_request *)data;
        do_join_group(d->group_name, d->group_pwd, callback, user);
        break;
    }
    case AOO_NET_GROUP_LEAVE_REQUEST:
    {
        auto d = (aoo_net_group_request *)data;
        do_leave_group(d->group_name, callback, user);
        break;
    }
    default:
        LOG_ERROR("aoo client: unknown request " << request);
        return 0;
    }
    return 1;
}

int32_t aoo_net_client_send_message(aoo_net_client *client, const char *data, int32_t n,
                                    const void *addr, int32_t len, int32_t flags)
{
    return client->send_message(data, n, addr, len, flags);
}

int32_t aoo::net::client::send_message(const char *data, int32_t n,
                                       const void *addr, int32_t len, int32_t flags)
{
    std::unique_ptr<icommand> cmd;
    if (addr){
        if (len > 0){
            // peer message
            cmd = std::make_unique<peer_message_cmd>(
                        data, n, (const sockaddr *)addr, len, flags);
        } else {
            // group message
            cmd = std::make_unique<group_message_cmd>(
                        data, n, (const char *)addr, flags);
        }
    } else {
        cmd = std::make_unique<message_cmd>(data, n, flags);
    }
    if (messages_.write_available()){
        messages_.write(std::move(cmd));
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_net_client_handle_message(aoo_net_client *client, const char *data,
                                      int32_t n, const void *addr, int32_t len)
{
    return client->handle_message(data, n, addr, len);
}

int32_t aoo::net::client::handle_message(const char *data, int32_t n,
                                         const void *addr, int32_t len){
    try {
        osc::ReceivedPacket packet(data, n);
        osc::ReceivedMessage msg(packet);

        int32_t type;
        auto onset = aoo_net_parse_pattern(data, n, &type);
        if (!onset){
            LOG_WARNING("aoo_client: not an AOO NET message!");
            return 0;
        }

        ip_address address((struct sockaddr *)addr, len); // FIXME

        LOG_DEBUG("aoo_client: handle UDP message " << msg.AddressPattern()
            << " from " << address.name() << ":" << address.port());

        // check for server message
        for (auto& remote : remote_addr_){
            if (remote == address){
                // server message
                if (type != AOO_TYPE_CLIENT){
                    LOG_WARNING("aoo_client: not a server message!");
                    return 0;
                }
                handle_server_message_udp(msg, onset);
                return 1;
            }
        }

        // peer message
        if (type != AOO_TYPE_PEER){
            LOG_WARNING("aoo_client: not a peer message!");
            return 0;
        }
        bool success = false;
        {
            shared_lock lock(peer_lock_);
            // NOTE: we have to loop over *all* peers because there can
            // be more than 1 peer on a given IP endpoint, since a single
            // user can join multiple groups.
            // LATER make this more efficient, e.g. by associating IP endpoints
            // with peers instead of having them all in a single vector.
            for (auto& p : peers_){
                if (p->match(address)){
                    p->handle_message(msg, onset, address);
                    success = true;
                }
            }
        }
        // NOTE: during the handshake process it is expected that
        // we receive UDP messages which we have to ignore:
        // a) pings from a peer which we haven't had the chance to add yet
        // b) pings sent to the other endpoint address
        if (!success){
            LOG_VERBOSE("aoo_client: ignoring UDP message "
                        << msg.AddressPattern() << " from endpoint "
                        << address.name() << ":" << address.port());
        }
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_client: exception in handle_message: " << e.what());
    #if 0
        scoped_lock lock(socket_lock_);
        on_exception("UDP message", e);
    #endif
    }

    return 0;
}

int32_t aoo_net_client_send(aoo_net_client *client){
    return client->send();
}

int32_t aoo::net::client::send(){
    // send server messages
    auto state = state_.load();
    if (state != client_state::disconnected){
        time_tag now = time_tag::now();
        auto elapsed_time = time_tag::duration(start_time_, now);
        auto delta = elapsed_time - last_udp_ping_time_;

        if (state == client_state::handshake){
            // check for time out
            if (first_udp_ping_time_ == 0){
                first_udp_ping_time_ = elapsed_time;
            } else if ((elapsed_time - first_udp_ping_time_) > request_timeout()){
                // handshake has timed out!

                // get callback before closing!
                auto cb = connect_callback_;
                auto user = connect_userdata_;

                unique_lock lock(socket_lock_);
                close();
                lock.unlock();

                aoo_net_error_reply reply;
                reply.errorcode = 0;
                reply.errormsg = "UDP handshake time out";

                if (cb) cb(user, -1, &reply);

                return 1; // ?
            }
            // send handshakes in fast succession
            if (delta >= request_interval()){
                char buf[64];
                osc::OutboundPacketStream msg(buf, sizeof(buf));
                msg << osc::BeginMessage(AOO_NET_MSG_SERVER_REQUEST) << osc::EndMessage;

                for (auto& addr : remote_addr_){
                    send_message_udp(msg.Data(), msg.Size(), addr);
                }
                last_udp_ping_time_ = elapsed_time;
            }
        } else if (state == client_state::connected){
            // send regular pings
            if (delta >= ping_interval()){
                char buf[64];
                osc::OutboundPacketStream msg(buf, sizeof(buf));
                msg << osc::BeginMessage(AOO_NET_MSG_SERVER_PING)
                    << osc::EndMessage;

                send_server_message_udp(msg.Data(), msg.Size());
                last_udp_ping_time_ = elapsed_time;
            }
        } else {
            // ignore connecting and login
            return 1;
        }

        // send outgoing peer/group messages
        while (messages_.read_available() > 0){
            std::unique_ptr<icommand> cmd;
            messages_.read(cmd);
            cmd->perform(*this);
        }

        // update peers
        shared_lock lock(peer_lock_);
        for (auto& p : peers_){
            p->send(now);
        }
    }
    return 1;
}

int32_t aoo_net_client_events_available(aoo_net_server *client){
    return client->events_available();
}

int32_t aoo::net::client::events_available(){
    return 1;
}

int32_t aoo_net_client_poll_events(aoo_net_client *client, aoo_eventhandler fn, void *user){
    return client->poll_events(fn, user);
}

int32_t aoo::net::client::poll_events(aoo_eventhandler fn, void *user){
    // always thread-safe
    int count = 0;
    while (events_.read_available() > 0){
        std::unique_ptr<ievent> e;
        events_.read(e);
        fn(user, &e->event_);
        count++;
    }
    return count;
}

namespace aoo {
namespace net {

void client::do_send_message(const char *data, int32_t size, int32_t flags,
                             const ip_address* vec, int32_t n)
{
    // for now ignore 'flags'. LATER we might use this to distinguish
    // between reliable and unreliable messages, and maybe other things.

    // embed inside an OSC message:
    // /aoo/peer/msg' (b)<message>
    try {
        char buf[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream msg(buf, sizeof(buf));
        msg << osc::BeginMessage(AOO_NET_MSG_PEER_MESSAGE)
            << osc::Blob(data, size) << osc::EndMessage;

        for (int i = 0; i < n; ++i){
            auto& addr = vec[i];
            LOG_DEBUG("aoo_client: send message " << data
                      << " to " << addr.name() << ":" << addr.port());
            send_message_udp(msg.Data(), msg.Size(),addr);
        }
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_client: error sending OSC message: " << e.what());
    }
}

void client::perform_send_message(const char *data, int32_t n,
                                  int32_t flags)
{
    shared_lock lock(peer_lock_);
    // make a temporary copy of peer addresses, so we can
    // send messages without holding a lock.
    auto count = peers_.size();
    auto vec = (ip_address *)alloca(count * sizeof(ip_address));
    for (int i = 0; i < count; ++i){
        new (vec + i) ip_address(peers_[i]->address());
    }
    lock.unlock();

    do_send_message(data, n, flags, vec, count);
}

void client::perform_send_message(const char *data, int32_t n,
                                  const ip_address& address, int32_t flags)
{
    do_send_message(data, n, flags, &address, 1);
}

void client::perform_send_message(const char *data, int32_t n,
                                  const std::string& group, int32_t flags)
{
    shared_lock lock(peer_lock_);
    // make a temporary copy of matching peer addresses,
    // so we can send messages without holding a lock.
    // LATER we should use a group dictionary to avoid the linear search.
    int count = 0;
    auto numpeers = peers_.size();
    auto vec = (ip_address *)alloca(numpeers * sizeof(ip_address));
    for (int i = 0; i < numpeers; ++i){
        if (peers_[i]->group() == group){
            new (vec + count++) ip_address(peers_[i]->address());
        }
    }
    lock.unlock();

    LOG_DEBUG("send message to group " << group);

    do_send_message(data, n, flags, vec, count);
}

void client::do_connect(const char *host, int port,
                        const char *name, const char *pwd,
                        aoo_net_callback cb, void *user)
{
    auto state = state_.load();
    if (state != client_state::disconnected){
        aoo_net_error_reply reply;
        reply.errorcode = 0;
        if (state == client_state::connected){
            reply.errormsg = "already connected";
        } else {
            reply.errormsg = "already connecting";
        }

        if (cb) cb(user, -1, &reply);

        return;
    }

    username_ = name;
    password_ = encrypt(pwd);
    connect_callback_ = cb;
    connect_userdata_ = user;
    public_addr_.clear();
    remote_addr_.clear();
    local_addr_ = ip_address {};

    auto cmd = std::make_unique<connect_cmd>(cb, user, host, port);

    push_command(std::move(cmd));

    state_ = client_state::connecting;

    signal();
}

void client::perform_connect(const std::string &host, int port,
                             aoo_net_callback cb, void *user)
{
    if (tcpsocket_ >= 0){
        LOG_ERROR("aoo_client: bug client::do_connect()");
        return;
    }

    int err = try_connect(host, port);
    if (err != 0){
        // event
        std::string errmsg = socket_strerror(err);

        close();

        aoo_net_error_reply reply;
        reply.errorcode = err;
        reply.errormsg = errmsg.c_str();

        if (cb) cb(user, -1, &reply);

        return;
    }

    first_udp_ping_time_ = 0;
    state_ = client_state::handshake;
}

int client::try_connect(const std::string &host, int port){
    tcpsocket_ = socket_tcp(0);
    if (tcpsocket_ < 0){
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't create socket (" << err << ")");
        return err;
    }
    // resolve host name
    auto result = ip_address::get_list(host, port, type_);
    if (result.empty()){
        int err = socket_errno();
        // LATER think about best way for error handling. Maybe exception?
        LOG_ERROR("aoo_client: couldn't resolve hostname (" << socket_errno() << ")");
        return err;
    }
    remote_addr_ = std::move(result);
    // for actual TCP connection, just pick the first result
    auto& remote = remote_addr_.front();
    LOG_VERBOSE("try to connect to " << remote.name() << ":" << port);

    // try to connect (LATER make timeout configurable)
    if (socket_connect(tcpsocket_, remote, 5) < 0){
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't connect (" << err << ")");
        return err;
    }

    // get local network interface
    ip_address temp;
    if (socket_address(tcpsocket_, temp) < 0){
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't get socket name (" << err << ")");
        return err;
    }
    local_addr_ = ip_address(temp.name(), udpport_, type_);

    LOG_VERBOSE("aoo_client: successfully connected to "
                << remote.name() << " on port " << remote.port());
    LOG_VERBOSE("aoo_client: local address: " << local_addr_.name());

    return 0;
}

void client::do_disconnect(aoo_net_callback cb, void *user){
    auto state = state_.load();
    if (state != client_state::connected){
        aoo_net_error_reply reply;
        reply.errormsg = (state == client_state::disconnected)
                ? "not connected" : "still connecting";
        reply.errorcode = 0;

        if (cb) cb(user, -1, &reply);

        return;
    }

    auto cmd = std::make_unique<disconnect_cmd>(cb, user);
    push_command(std::move(cmd));

    signal();
}

void client::perform_disconnect(aoo_net_callback cb, void *user){
    close(true);

    if (cb) cb(user, 0, nullptr); // always succeeds
}

void client::perform_login(){
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage(AOO_NET_MSG_SERVER_LOGIN)
        << (int32_t)make_version()
        << username_.c_str() << password_.c_str()
        // unmap IPv4 addresses for IPv4 only servers
        << local_addr_.name_unmapped() << local_addr_.port();
    for (auto& addr : public_addr_){
        msg << addr.name_unmapped() << addr.port();
    }
    msg << osc::EndMessage;

    send_server_message_tcp(msg.Data(), msg.Size());
}

void client::do_join_group(const char *group, const char *pwd,
                           aoo_net_callback cb, void *user){
    auto cmd = std::make_unique<group_join_cmd>(cb, user, group, encrypt(pwd));

    push_command(std::move(cmd));

    signal();
}

void client::perform_join_group(const std::string &group, const std::string &pwd,
                                aoo_net_callback cb, void *user)
{

    auto request = [group, cb, user](
            const char *pattern,
            const osc::ReceivedMessage& msg)
    {
        if (!strcmp(pattern, AOO_NET_MSG_GROUP_JOIN)){
            auto it = msg.ArgumentsBegin();
            std::string g = (it++)->AsString();
            if (g == group){
                int32_t status = (it++)->AsInt32();
                if (status > 0){
                    LOG_VERBOSE("aoo_client: successfully joined group " << group);
                    if (cb) cb(user, 0, nullptr);
                } else {
                    std::string errmsg;
                    if (msg.ArgumentCount() > 2){
                        errmsg = (it++)->AsString();
                        LOG_WARNING("aoo_client: couldn't join group "
                                    << group << ": " << errmsg);
                    } else {
                        errmsg = "unknown error";
                    }
                    // reply
                    aoo_net_error_reply reply;
                    reply.errorcode = 0;
                    reply.errormsg = errmsg.c_str();

                    if (cb) cb(user, -1, &reply);
                }

                return true;
            }
        }
        return false;
    };
    pending_requests_.push_back(request);

    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage(AOO_NET_MSG_SERVER_GROUP_JOIN)
        << group.c_str() << pwd.c_str() << osc::EndMessage;

    send_server_message_tcp(msg.Data(), msg.Size());
}

void client::do_leave_group(const char *group,
                            aoo_net_callback cb, void *user){
    auto cmd = std::make_unique<group_leave_cmd>(cb, user, group);

    push_command(std::move(cmd));

    signal();
}

void client::perform_leave_group(const std::string &group,
                                 aoo_net_callback cb, void *user)
{
    auto request = [this, group, cb, user](
            const char *pattern,
            const osc::ReceivedMessage& msg)
    {
        if (!strcmp(pattern, AOO_NET_MSG_GROUP_LEAVE)){
            auto it = msg.ArgumentsBegin();
            std::string g = (it++)->AsString();
            if (g == group){
                int32_t status = (it++)->AsInt32();
                if (status > 0){
                    LOG_VERBOSE("aoo_client: successfully left group " << group);

                    // remove all peers from this group
                    unique_lock lock(peer_lock_); // writer lock!
                    for (auto it = peers_.begin(); it != peers_.end(); ){
                        if ((*it)->group() == group){
                            it = peers_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    lock.unlock();

                    if (cb) cb(user, 0, nullptr);
                } else {
                    std::string errmsg;
                    if (msg.ArgumentCount() > 2){
                        errmsg = (it++)->AsString();
                        LOG_WARNING("aoo_client: couldn't leave group "
                                    << group << ": " << errmsg);
                    } else {
                        errmsg = "unknown error";
                    }
                    // reply
                    aoo_net_error_reply reply;
                    reply.errorcode = 0;
                    reply.errormsg = errmsg.c_str();

                    if (cb) cb(user, -1, &reply);
                }

                return true;
            }
        }
        return false;
    };
    pending_requests_.push_back(request);

    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage(AOO_NET_MSG_SERVER_GROUP_LEAVE)
        << group.c_str() << osc::EndMessage;

    send_server_message_tcp(msg.Data(), msg.Size());
}

void client::send_message_udp(const char *data, int32_t size, const ip_address& addr)
{
    sendto(udpsocket_, data, size, 0, addr.address(), addr.length());
}

void client::push_event(std::unique_ptr<ievent> e)
{
    _scoped_lock<spinlock> lock(event_lock_);
    if (events_.write_available()){
        events_.write(std::move(e));
    }
}

bool client::wait_for_event(float timeout){
    LOG_DEBUG("aoo_client: wait " << timeout << " seconds");

    struct pollfd fds[2];
    fds[0].fd = eventsocket_;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = tcpsocket_;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    // round up to 1 ms! -1: block indefinitely
    // NOTE: macOS requires the negative timeout to exactly -1!
#ifdef _WIN32
    int result = WSAPoll(fds, 2, timeout < 0 ? -1 : timeout * 1000.0 + 0.5);
#else
    int result = poll(fds, 2, timeout < 0 ? -1 : timeout * 1000.0 + 0.5);
#endif
    if (result < 0){
        int err = socket_errno();
        if (err == EINTR){
            return true; // ?
        } else {
            LOG_ERROR("aoo_client: poll failed (" << err << ")");
            socket_error_print("poll");
            return false;
        }
    }

    if (fds[0].revents){
        // read from socket (empty packet)
        char buf[64];
        recv(eventsocket_, buf, sizeof(buf), 0);
        // LOG_DEBUG("aoo_client: got signalled");
    }

    if (fds[1].revents){
        receive_data();
    }
    return true;
}

void client::receive_data(){
    scoped_lock lock(socket_lock_);
    if (tcpsocket_ < 0){
        return;
    }
    char buffer[AOO_MAXPACKETSIZE];
    auto result = recv(tcpsocket_, buffer, sizeof(buffer), 0);
    if (result > 0){
        recvbuffer_.write_bytes((uint8_t *)buffer, result);

        // handle packets
        uint8_t buf[AOO_MAXPACKETSIZE];
        while (true){
            auto size = recvbuffer_.read_packet(buf, sizeof(buf));
            if (size > 0){
                try {
                    osc::ReceivedPacket packet((char *)buf, size);
                    if (packet.IsBundle()){
                        osc::ReceivedBundle bundle(packet);
                        handle_server_bundle_tcp(bundle);
                    } else {
                        osc::ReceivedMessage msg(packet);
                        handle_server_message_tcp(msg);
                    }
                } catch (const osc::Exception& e){
                    LOG_ERROR("aoo_client: exception in receive_data: " << e.what());
                    on_exception("server TCP message", e);
                }
            } else {
                break;
            }
        }
    } else if (result == 0){
        // connection closed by the remote server
        on_socket_error(0);
    } else {
        int err = socket_errno();
        LOG_ERROR("aoo_client: recv() failed (" << err << ")");
        on_socket_error(err);
    }
}

void client::send_server_message_tcp(const char *data, int32_t size){
    if (tcpsocket_ >= 0){
        if (sendbuffer_.write_packet((const uint8_t *)data, size)){
            while (sendbuffer_.read_available()){
                uint8_t buf[1024];
                int32_t total = sendbuffer_.read_bytes(buf, sizeof(buf));

                int32_t nbytes = 0;
                while (nbytes < total){
                    auto res = ::send(tcpsocket_, (char *)buf + nbytes, total - nbytes, 0);
                    if (res >= 0){
                        nbytes += res;
                    #if 0
                        LOG_DEBUG("aoo_client: sent " << res << " bytes");
                    #endif
                    } else {
                        auto err = socket_errno();
                        LOG_ERROR("aoo_client: send() failed (" << err << ")");
                        on_socket_error(err);
                        return;
                    }
                }
            }
            LOG_DEBUG("aoo_client: sent " << data << " to server");
        } else {
            LOG_ERROR("aoo_client: couldn't send " << data << " to server");
        }
    } else {
        LOG_ERROR("aoo_client: can't send server message - socket closed!");
    }
}

void client::send_server_message_udp(const char *data, int32_t size)
{
    if (!remote_addr_.empty() && remote_addr_.front().valid()){
        send_message_udp(data, size, remote_addr_.front());
    }
}

void client::handle_server_message_tcp(const osc::ReceivedMessage& msg){
    // first check main pattern
    int32_t len = strlen(msg.AddressPattern());
    int32_t onset = AOO_MSG_DOMAIN_LEN + AOO_NET_MSG_CLIENT_LEN;

    if ((len < onset) ||
        memcmp(msg.AddressPattern(), AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT, onset))
    {
        LOG_ERROR("aoo_client: received bad message " << msg.AddressPattern()
                  << " from server");
        return;
    }

    // now compare subpattern
    auto pattern = msg.AddressPattern() + onset;
    LOG_DEBUG("aoo_client: got message " << pattern << " from server");

    try {
        if (!strcmp(pattern, AOO_NET_MSG_PING)){
            LOG_DEBUG("aoo_client: got TCP ping from server");
        } else if (!strcmp(pattern, AOO_NET_MSG_PEER_JOIN)){
            handle_peer_add(msg);
        } else if (!strcmp(pattern, AOO_NET_MSG_PEER_LEAVE)){
            handle_peer_remove(msg);
        } else if (!strcmp(pattern, AOO_NET_MSG_LOGIN)){
            handle_login(msg);
        } else {
            // handle reply
            for (auto it = pending_requests_.begin(); it != pending_requests_.end();){
                if ((*it)(pattern, msg)){
                    it = pending_requests_.erase(it);
                    return;
                } else {
                    ++it;
                }
            }
            LOG_ERROR("aoo_client: couldn't handle reply " << pattern);
        }
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_client: exception on handling " << pattern
                  << " message: " << e.what());
        on_exception("server TCP message", e, pattern);
    }
}

void client::handle_server_bundle_tcp(const osc::ReceivedBundle &bundle){
    auto it = bundle.ElementsBegin();
    while (it != bundle.ElementsEnd()){
        if (it->IsBundle()){
            osc::ReceivedBundle b(*it);
            handle_server_bundle_tcp(b);
        } else {
            osc::ReceivedMessage msg(*it);
            handle_server_message_tcp(msg);
        }
        ++it;
    }
}

void client::handle_login(const osc::ReceivedMessage& msg){
    if (state_.load() == client_state::login){
        auto it = msg.ArgumentsBegin();
        int32_t status = (it++)->AsInt32();

        // make copies just to be safe
        auto cb = connect_callback_;
        auto user = connect_userdata_;
        auto local_addr = local_addr_;
        auto public_addr = public_addr_.front(); // LATER fixme

        if (status > 0){
            int32_t id = (it++)->AsInt32();
            // connected!
            state_.store(client_state::connected);
            LOG_VERBOSE("aoo_client: successfully logged in (user ID: "
                        << id << " )");
            // notify
            aoo_net_connect_reply reply;
            reply.local_address = local_addr.address();
            reply.local_addrlen = local_addr.length();
            reply.public_address = public_addr.address();
            reply.public_addrlen = public_addr.length();
            reply.user_id = id;

            LOG_VERBOSE("aoo_client: public address: " << public_addr.name());

            if (cb) cb(user, 0, &reply);
        } else {
            std::string errmsg;
            if (msg.ArgumentCount() > 1){
                errmsg = (it++)->AsString();
            } else {
                errmsg = "unknown error";
            }
            LOG_WARNING("aoo_client: login failed: " << errmsg);

            unique_lock lock(socket_lock_);
            close();
            lock.unlock();

            // notify
            aoo_net_error_reply reply;
            reply.errorcode = 0;
            reply.errormsg = errmsg.c_str();

            if (cb) cb(user, -1, &reply);
        }
    }
}

void client::handle_peer_add(const osc::ReceivedMessage& msg){
    auto count = msg.ArgumentCount();
    auto it = msg.ArgumentsBegin();
    std::string group = (it++)->AsString();
    std::string user = (it++)->AsString();
    int32_t id = (it++)->AsInt32();
    count -= 3;

    std::vector<ip_address> addrlist;
    while (count >= 2){
        std::string ip = (it++)->AsString();
        int32_t port = (it++)->AsInt32();
        ip_address addr(ip, port, type_);
        if (addr.valid()){
            addrlist.push_back(addr);
        }
        count -= 2;
    }

    unique_lock lock(peer_lock_); // writer lock!

    // check if peer already exists (shouldn't happen)
    for (auto& p: peers_){
        if (p->match(group, user, id)){
            LOG_ERROR("aoo_client: peer " << *p << " already added");
            return;
        }
    }

    auto p = std::make_unique<peer>(*this, id, group, user, std::move(addrlist));

    peers_.push_back(std::move(p));

    // don't handle event yet, wait for ping handshake

    LOG_VERBOSE("aoo_client: new peer " << *peers_.back());
}

void client::handle_peer_remove(const osc::ReceivedMessage& msg){
    auto it = msg.ArgumentsBegin();
    std::string group = (it++)->AsString();
    std::string user = (it++)->AsString();
    int32_t id = (it++)->AsInt32();

    unique_lock lock(peer_lock_); // writer lock!

    auto result = std::find_if(peers_.begin(), peers_.end(),
        [&](auto& p){ return p->match(group, user, id); });
    if (result == peers_.end()){
        LOG_ERROR("aoo_client: couldn't remove " << group << "|" << user);
        return;
    }

    bool connected = (*result)->connected();
    ip_address addr = (*result)->address();

    peers_.erase(result);

    // only send event if we're connected, which means
    // that an AOO_NET_PEER_JOIN_EVENT has been sent.
    if (connected){
        auto e = std::make_unique<peer_event>(
                    AOO_NET_PEER_LEAVE_EVENT, addr, group.c_str(), user.c_str(), id);
        push_event(std::move(e));
    }

    LOG_VERBOSE("aoo_client: peer " << group << "|" << user << " left");
}

void client::handle_server_message_udp(const osc::ReceivedMessage &msg, int onset){
    auto pattern = msg.AddressPattern() + onset;
    try {
        if (!strcmp(pattern, AOO_NET_MSG_PING)){
            LOG_DEBUG("aoo_client: got UDP ping from server");
        } else if (!strcmp(pattern, AOO_NET_MSG_REPLY)){
            if (state_.load() == client_state::handshake){
                // retrieve public IP + port
                auto it = msg.ArgumentsBegin();
                std::string ip = (it++)->AsString();
                int port = (it++)->AsInt32();

                ip_address addr(ip, port, type_);
                for (auto& a : public_addr_){
                    if (a == addr){
                        return;
                    }
                }
                public_addr_.push_back(addr);
                LOG_VERBOSE("aoo_client: public address is "
                            << addr.name() << " " << addr.port());

                // check if we got all public addresses
                // LATER improve this
                if (public_addr_.size() == remote_addr_.size()){
                    // now we can try to login
                    client_state expected = client_state::handshake;
                    if (state_.compare_exchange_strong(expected, client_state::login)){
                        auto cmd = std::make_unique<login_cmd>();

                        push_command(std::move(cmd));

                        signal();
                    }
                }
            }
        } else {
            LOG_WARNING("aoo_client: received unknown UDP message "
                        << pattern << " from server");
        }
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_client: exception on handling " << pattern
                  << " message: " << e.what());
        on_exception("server UDP message", e, pattern);
    }
}

bool client::signal(){
    // LOG_DEBUG("aoo_client signal");
    return socket_signal(eventsocket_);
}

void client::close(bool manual){
    if (tcpsocket_ >= 0){
        socket_close(tcpsocket_);
        tcpsocket_ = -1;
        LOG_VERBOSE("aoo_client: connection closed");
    }

    first_udp_ping_time_ = 0;
    username_.clear();
    password_.clear();
    connect_callback_ = nullptr;
    connect_userdata_ = nullptr;

    // remove all peers
    unique_lock lock(peer_lock_);
    peers_.clear();
    lock.unlock();

    // clear pending request
    // LATER call them all with some dummy input
    // to avoid memleak if clients pass heap
    // allocated request data, which is supposed
    // to be freed in the callback.
    pending_requests_.clear();

    auto oldstate = state_.exchange(client_state::disconnected);
    if (!manual && oldstate == client_state::connected){
        auto e = std::make_unique<event>(AOO_NET_DISCONNECT_EVENT);
        push_event(std::move(e));
    }
}

void client::on_socket_error(int err){
    std::string msg = err ? socket_strerror(err)
                          : "connection closed by server";
    auto e = std::make_unique<error_event>(err, msg.c_str());

    push_event(std::move(e));

    close();
}

void client::on_exception(const char *what, const osc::Exception &err,
                          const char *pattern){
    char msg[256];
    if (pattern){
        snprintf(msg, sizeof(msg), "exception in %s (%s): %s",
                 what, pattern, err.what());
    } else {
        snprintf(msg, sizeof(msg), "exception in %s: %s",
                 what, err.what());
    }

    auto e = std::make_unique<error_event>(0, msg);

    push_event(std::move(e));

    close();
}

/*///////////////////// events ////////////////////////*/

client::error_event::error_event(int32_t code, const char *msg)
{
    error_event_.type = AOO_NET_ERROR_EVENT;
    error_event_.errorcode = code;
    error_event_.errormsg = copy_string(msg);
}

client::error_event::~error_event()
{
    delete error_event_.errormsg;
}

client::ping_event::ping_event(const ip_address& addr, uint64_t tt1,
                               uint64_t tt2, uint64_t tt3)
{
    ping_event_.type = AOO_NET_PING_EVENT;
    ping_event_.address = copy_sockaddr(addr.address(), addr.length());
    ping_event_.length = addr.length();
    ping_event_.tt1 = tt1;
    ping_event_.tt2 = tt2;
    ping_event_.tt3 = tt3;
}

client::ping_event::~ping_event()
{
    delete (const sockaddr *)ping_event_.address;
}

client::peer_event::peer_event(int32_t type, const ip_address& addr,
                               const char *group, const char *user, int32_t id)
{
    peer_event_.type = type;
    peer_event_.address = copy_sockaddr(addr.address(), addr.length());
    peer_event_.length = addr.length();
    peer_event_.group_name = copy_string(group);
    peer_event_.user_name = copy_string(user);
    peer_event_.user_id = id;
}

client::peer_event::~peer_event()
{
    delete peer_event_.user_name;
    delete peer_event_.group_name;
    delete (const sockaddr *)peer_event_.address;
}

client::message_event::message_event(const char *data, int32_t size,
                                     const ip_address& addr)
{
    message_event_.type = AOO_NET_MESSAGE_EVENT;
    message_event_.address = copy_sockaddr(addr.address(), addr.length());
    message_event_.length = addr.length();
    auto msg = new char[size];
    memcpy(msg, data, size);
    message_event_.data = msg;
    message_event_.size = size;
}

client::message_event::~message_event()
{
    delete message_event_.data;
    delete (const sockaddr *)message_event_.address;
}

/*///////////////////// peer //////////////////////////*/

peer::peer(client& client, int32_t id,
           const std::string& group, const std::string& user,
           std::vector<ip_address>&& addrlist)
    : client_(&client), id_(id), group_(group), user_(user),
      addresses_(std::move(addrlist))
{
    start_time_ = time_tag::now();

    LOG_DEBUG("create peer " << *this);
}

peer::~peer(){
    LOG_DEBUG("destroy peer " << *this);
}

bool peer::match(const ip_address &addr) const {
    auto real_addr = address_.load();
    if (real_addr){
        return *real_addr == addr;
    } else {
        return true; // match all messages!
    }
}

bool peer::match(const std::string& group, const std::string& user,
                 int32_t id)
{
    return id_ == id && group_ == group && user_ == user;
}

std::ostream& operator << (std::ostream& os, const peer& p)
{
    os << p.group_ << "|" << p.user_;
    return os;
}

void peer::send(time_tag now){
    auto elapsed_time = time_tag::duration(start_time_, now);
    auto delta = elapsed_time - last_pingtime_;

    auto real_addr = address_.load();
    if (real_addr){
        // send regular ping
        if (delta >= client_->ping_interval()){
            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage(AOO_NET_MSG_PEER_PING) << osc::EndMessage;

            client_->send_message_udp(msg.Data(), msg.Size(), *real_addr);

            last_pingtime_ = elapsed_time;
        }

        // send reply
        if (send_reply_.exchange(false)){
            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage(AOO_NET_MSG_PEER_REPLY) << osc::EndMessage;

            client_->send_message_udp(msg.Data(), msg.Size(), *real_addr);
        }
    } else if (!timeout_) {
        // try to establish UDP connection with peer
        if (elapsed_time > client_->request_timeout()){
            // couldn't establish peer connection!
            LOG_ERROR("aoo_client: couldn't establish UDP connection to "
                      << *this << "; timed out after "
                      << client_->request_timeout() << " seconds");
            timeout_ = true;

            std::stringstream ss;
            ss << "couldn't establish connection with peer " << *this;

            auto e = std::make_unique<client::error_event>(0, ss.str().c_str());
            client_->push_event(std::move(e));

            return;
        }
        // send handshakes in fast succession to *both* addresses
        // until we get a reply from one of them (see handle_message())
        if (delta >= client_->request_interval()){
            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            // Include group+name+id, so peers can identify us even
            // if we're behind a symmetric NAT.
            // NOTE: This trick doesn't work if both parties are
            // behind a symmetrict NAT; in that case, UDP hole punching
            // simply doesn't work.
            msg << osc::BeginMessage(AOO_NET_MSG_PEER_PING)
                << group_.c_str() << user_.c_str() << (int32_t)id_
                << osc::EndMessage;

            for (auto& addr : addresses_){
                client_->send_message_udp(msg.Data(), msg.Size(), addr);
            }

            // LOG_DEBUG("send ping to " << *this);

            last_pingtime_ = elapsed_time;
        }
    }
}

bool peer::handle_first_message(const osc::ReceivedMessage &msg, int onset,
                                const ip_address &addr)
{
    // Try to find matching address
    for (auto& a : addresses_){
        if (a == addr){
            real_address_ = addr;
            address_.store(&real_address_);
            return true;
        }
    }

    // We might get a message from a peer behind a symmetric NAT.
    // To be sure, check group, user and ID, but only if
    // provided (for backwards compatibility with older AOO clients)
    auto pattern = msg.AddressPattern() + onset;
    if (!strcmp(pattern, AOO_NET_MSG_PING)){
        if (msg.ArgumentCount() >= 3){
            try {
                auto it = msg.ArgumentsBegin();
                std::string group = (it++)->AsString();
                std::string user = (it++)->AsString();
                int32_t id = (it++)->AsInt32();
                if (group == group_ && user == user_ && id == id_){
                    real_address_ = addr;
                    address_.store(&real_address_);
                    LOG_WARNING("aoo_client: peer " << *this
                                << " is located behind a symmetric NAT!");
                    return true;
                }
            } catch (const osc::Exception& e){
                LOG_ERROR("aoo_client: got bad " << pattern
                          << " message from peer: " << e.what());
            }
        } else {
            // ignore silently!
        }
    } else {
        LOG_ERROR("aoo_client: got " << pattern
                  << " message from unknown peer");
    }
    return false;
}

void peer::handle_message(const osc::ReceivedMessage &msg, int onset,
                          const ip_address& addr)
{
    if (!address_.load()){
        if (!handle_first_message(msg, onset, addr)){
            return;
        }

        // push event
        auto e = std::make_unique<client::peer_event>(
                    AOO_NET_PEER_JOIN_EVENT, addr,
                    group().c_str(), user().c_str(), id());

        client_->push_event(std::move(e));

        LOG_VERBOSE("aoo_client: successfully established connection with "
                  << *this << " (" << addr.name() << ":" << addr.port() << ")");
    }

    auto pattern = msg.AddressPattern() + onset;
    try {
        if (!strcmp(pattern, AOO_NET_MSG_PING)){
            send_reply_ = true;
            LOG_DEBUG("aoo_client: got ping from " << *this);
        } else if (!strcmp(pattern, AOO_NET_MSG_REPLY)){
            LOG_DEBUG("aoo_client: got reply from " << *this);
        } else if (!strcmp(pattern, AOO_NET_MSG_MESSAGE)){
            // get embedded OSC message
            const void *data;
            osc::osc_bundle_element_size_t size;
            msg.ArgumentsBegin()->AsBlob(data, size);

            LOG_DEBUG("aoo_client: got message " << (const char *)data
                      << " from " << addr.name() << ":" << addr.port());

            auto e = std::make_unique<client::message_event>(
                        (const char *)data, size, addr);

            client_->push_event(std::move(e));
        } else {
            LOG_WARNING("aoo_client: received unknown message "
                        << pattern << " from " << *this);
        }
    } catch (const osc::Exception& e){
        // peer exceptions are not fatal!
        LOG_ERROR("aoo_client: " << *this << ": exception on handling "
                  << pattern << " message: " << e.what());
    }
}

} // net
} // aoo
