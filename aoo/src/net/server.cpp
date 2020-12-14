/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "server.hpp"

#include "aoo/aoo.h"

#include <functional>
#include <algorithm>

#ifndef _WIN32
#include <sys/poll.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#define AOO_NET_MSG_CLIENT_PING \
    AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT AOO_NET_MSG_PING

#define AOO_NET_MSG_CLIENT_LOGIN \
    AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT AOO_NET_MSG_LOGIN

#define AOO_NET_MSG_CLIENT_REPLY \
    AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT AOO_NET_MSG_REPLY

#define AOO_NET_MSG_CLIENT_GROUP_JOIN \
    AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT AOO_NET_MSG_GROUP AOO_NET_MSG_JOIN

#define AOO_NET_MSG_CLIENT_GROUP_LEAVE \
    AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT AOO_NET_MSG_GROUP AOO_NET_MSG_LEAVE

#define AOO_NET_MSG_CLIENT_PEER_JOIN \
    AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT AOO_NET_MSG_PEER AOO_NET_MSG_JOIN

#define AOO_NET_MSG_CLIENT_PEER_LEAVE \
    AOO_MSG_DOMAIN AOO_NET_MSG_CLIENT AOO_NET_MSG_PEER AOO_NET_MSG_LEAVE

#define AOO_NET_MSG_GROUP_JOIN \
    AOO_NET_MSG_GROUP AOO_NET_MSG_JOIN

#define AOO_NET_MSG_GROUP_LEAVE \
    AOO_NET_MSG_GROUP AOO_NET_MSG_LEAVE

namespace aoo {

uint32_t make_version();
bool check_version(uint32_t version);

namespace net {

// from aoo/client
char * copy_string(const char *s);
void * copy_sockaddr(const void *sa, int32_t len);
int32_t aoo_net_parse_pattern(const char *, int32_t, int32_t *);

} // net
} // aoo

/*//////////////////// AoO server /////////////////////*/

aoo_net_server * aoo_net_server_new(int port, uint32_t flags, int32_t *err) {
    // create UDP socket
    int udpsocket = aoo::socket_udp(port);
    if (udpsocket < 0){
        *err = aoo::socket_errno();
        LOG_ERROR("aoo_server: couldn't create UDP socket (" << *err << ")");
        return nullptr;
    }

    // create TCP socket
    int tcpsocket = aoo::socket_tcp(port);
    if (tcpsocket < 0){
        *err = aoo::socket_errno();
        LOG_ERROR("aoo_server: couldn't create TCP socket (" << *err << ")");
        aoo::socket_close(udpsocket);
        return nullptr;
    }

    // listen
    if (listen(tcpsocket, 32) < 0){
        *err = aoo::socket_errno();
        LOG_ERROR("aoo_server: listen() failed (" << *err << ")");
        aoo::socket_close(tcpsocket);
        aoo::socket_close(udpsocket);
        return nullptr;
    }

    return new aoo::net::server(tcpsocket, udpsocket);
}

aoo::net::server::server(int tcpsocket, int udpsocket)
    : tcpsocket_(tcpsocket), udpsocket_(udpsocket)
{
    type_ = socket_family(udpsocket);
    // commands_.reserve(256);
    // events_.reserve(256);
}

void aoo_net_server_free(aoo_net_server *server){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::net::server *>(server);
}

aoo::net::server::~server() {
    socket_close(tcpsocket_);
    tcpsocket_ = -1;
    socket_close(udpsocket_);
    udpsocket_ = -1;

    // clear explicitly to avoid crash!
    clients_.clear();
}

int32_t aoo_net_server_run(aoo_net_server *server){
    return server->run();
}

int32_t aoo::net::server::run(){
    // wait for networking or other events
    while (!quit_.load()){
        if (!wait_for_event()){
            break;
        }

        // handle commands
        std::unique_ptr<icommand> cmd;
        while (commands_.try_pop(cmd)){
            cmd->perform(*this);
        }
    }

    return 1;
}

int32_t aoo_net_server_quit(aoo_net_server *server){
    return server->quit();
}

int32_t aoo::net::server::quit(){
    quit_.store(true);
    if (!signal()){
        // force wakeup by closing the socket.
        // this is not nice and probably undefined behavior,
        // the MSDN docs explicitly forbid it!
        socket_close(udpsocket_);
    }
    return 1;
}

int32_t aoo_net_server_events_available(aoo_net_server *server){
    return server->events_available();
}

int32_t aoo::net::server::events_available(){
    return !events_.empty();
}

int32_t aoo_net_server_poll_events(aoo_net_server *server, aoo_eventhandler fn, void *user){
    return server->poll_events(fn, user);
}

int32_t aoo::net::server::poll_events(aoo_eventhandler fn, void *user){
    // always thread-safe
    int count = 0;
    std::unique_ptr<ievent> e;
    while (events_.try_pop(e)){
        fn(user, &e->event_);
        count++;
    }
    return count;
}

namespace aoo {
namespace net {

std::string server::error_to_string(error e){
    switch (e){
    case server::error::access_denied:
        return "access denied";
    case server::error::permission_denied:
        return "permission denied";
    case server::error::wrong_password:
        return "wrong password";
    default:
        return "unknown error";
    }
}

std::shared_ptr<user> server::get_user(const std::string& name,
                                       const std::string& pwd,
                                       uint32_t version, error& e)
{
    auto usr = find_user(name);
    if (usr){
        // check if someone is already logged in
        if (usr->is_active()){
            e = error::access_denied;
            return nullptr;
        }
        // check password for existing user
        if (usr->password == pwd){
            e = error::none;
            return usr;
        } else {
            e = error::wrong_password;
            return nullptr;
        }
    } else {
        // create new user (LATER add option to disallow this)
        if (true){
            auto id = next_user_id_++;
            usr = std::make_shared<user>(name, pwd, id, version);
            users_.push_back(usr);
            e = error::none;
            return usr;
        } else {
            e = error::permission_denied;
            return nullptr;
        }
    }
}

std::shared_ptr<user> server::find_user(const std::string& name)
{
    for (auto& usr : users_){
        if (usr->name == name){
            return usr;
        }
    }
    return nullptr;
}

std::shared_ptr<group> server::get_group(const std::string& name,
                                         const std::string& pwd, error& e)
{
    auto grp = find_group(name);
    if (grp){
        // check password for existing group
        if (grp->password == pwd){
            e = error::none;
            return grp;
        } else {
            e = error::wrong_password;
            return nullptr;
        }
    } else {
        // create new group (LATER add option to disallow this)
        if (true){
            grp = std::make_shared<group>(name, pwd);
            groups_.push_back(grp);
            e = error::none;
            return grp;
        } else {
            e = error::permission_denied;
            return nullptr;
        }
    }
}

std::shared_ptr<group> server::find_group(const std::string& name)
{
    for (auto& grp : groups_){
        if (grp->name == name){
            return grp;
        }
    }
    return nullptr;
}


void server::on_user_joined(user &usr){
    auto e = std::make_unique<user_event>(AOO_NET_USER_JOIN_EVENT,
                                          usr.name.c_str(), usr.id,
                                          usr.endpoint()->local_address()); // do we need this?
    push_event(std::move(e));
}

void server::on_user_left(user &usr){
    auto e = std::make_unique<user_event>(AOO_NET_USER_LEAVE_EVENT,
                                          usr.name.c_str(), usr.id,
                                          usr.endpoint()->local_address()); // do we need this?
    push_event(std::move(e));
}

void server::on_user_joined_group(user& usr, group& grp){
    // 1) send the new member to existing group members
    // 2) send existing group members to the new member
    for (auto& peer : grp.users()){
        if (peer->id != usr.id){
            char buf[AOO_MAXPACKETSIZE];

            auto notify = [&](client_endpoint* dest, user& u){
                osc::OutboundPacketStream msg(buf, sizeof(buf));
                msg << osc::BeginMessage(AOO_NET_MSG_CLIENT_PEER_JOIN)
                    << grp.name.c_str() << u.name.c_str();
                // only v0.2-pre3 and abvoe
                if (usr.version > 0){
                    msg << u.id;
                }
                // send *unmapped* addresses in case the client is IPv4 only
                for (auto& addr : u.endpoint()->public_addresses()){
                    msg << addr.name_unmapped() << addr.port();
                }
                msg << osc::EndMessage;

                dest->send_message(msg.Data(), msg.Size());
            };

            // notify new member
            notify(usr.endpoint(), *peer);

            // notify existing member
            notify(peer->endpoint(), usr);
        }
    }

    auto e = std::make_unique<group_event>(AOO_NET_GROUP_JOIN_EVENT,
                                           grp.name.c_str(),
                                           usr.name.c_str(), usr.id);
    push_event(std::move(e));
}

void server::on_user_left_group(user& usr, group& grp){
    if (udpsocket_ < 0){
        return; // prevent sending messages during shutdown
    }
    // notify group members
    for (auto& peer : grp.users()){
        if (peer->id != usr.id){
            char buf[AOO_MAXPACKETSIZE];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage(AOO_NET_MSG_CLIENT_PEER_LEAVE)
                  << grp.name.c_str() << usr.name.c_str() << usr.id
                  << osc::EndMessage;

            peer->endpoint()->send_message(msg.Data(), msg.Size());
        }
    }

    auto e = std::make_unique<group_event>(AOO_NET_GROUP_LEAVE_EVENT,
                                           grp.name.c_str(),
                                           usr.name.c_str(), usr.id);
    push_event(std::move(e));
}

void server::handle_relay_message(const osc::ReceivedMessage& msg,
                                  const ip_address& src){
    auto it = msg.ArgumentsBegin();

    auto ip = (it++)->AsString();
    auto port = (it++)->AsInt32();
    ip_address dst(ip, port, type());

    const void *msgData;
    osc::osc_bundle_element_size_t msgSize;
    (it++)->AsBlob(msgData, msgSize);

    // forward message to matching client
    // send unmapped address in case the client is IPv4 only!
    for (auto& client : clients_){
        if (client->match(dst)) {
            char buf[AOO_MAXPACKETSIZE];
            osc::OutboundPacketStream out(buf, sizeof(buf));
            out << osc::BeginMessage(AOO_MSG_DOMAIN AOO_NET_MSG_RELAY)
                << src.name_unmapped() << src.port() << osc::Blob(msgData, msgSize)
                << osc::EndMessage;
            client->send_message(out.Data(), out.Size());
            return;
        }
    }

    LOG_WARNING("aoo_server: couldn't find matching client for relay message");
}

bool server::wait_for_event(){
    bool didclose = false;
    int numclients = clients_.size();
    // allocate two extra slots for main TCP socket and UDP socket
    int numfds = numclients + 2;
    auto fds = (struct pollfd *)alloca(numfds * sizeof(struct pollfd));
    for (int i = 0; i < numfds; ++i){
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }
    for (int i = 0; i < numclients; ++i){
        fds[i].fd = clients_[i]->socket();
    }
    int tcpindex = numclients;
    int udpindex = numclients + 1;
    fds[tcpindex].fd = tcpsocket_;
    fds[udpindex].fd = udpsocket_;

    // NOTE: macOS requires the negative timeout to be exactly -1!
#ifdef _WIN32
    int result = WSAPoll(fds, numfds, -1);
#else
    int result = poll(fds, numfds, -1);
#endif
    if (result < 0){
        int err = errno;
        if (err == EINTR){
            return true; // ?
        } else {
            LOG_ERROR("aoo_server: poll failed (" << err << ")");
            return false;
        }
    }

    if (fds[tcpindex].revents){
        // accept new client
        ip_address addr;
        auto sock = accept(tcpsocket_, addr.address_ptr(), addr.length_ptr());
        if (sock >= 0){
            clients_.push_back(std::make_unique<client_endpoint>(*this, sock, addr));
            LOG_VERBOSE("aoo_server: accepted client (IP: "
                        << addr.name() << ", port: " << addr.port() << ")");
        } else {
            int err = socket_errno();
            LOG_ERROR("aoo_server: couldn't accept client (" << err << ")");
        }
    }

    if (fds[udpindex].revents){
        receive_udp();
    }

    for (int i = 0; i < numclients; ++i){
        if (fds[i].revents){
            // receive data from client
            if (!clients_[i]->receive_data()){
                clients_[i]->close();
                didclose = true;
            }
        }
    }

    if (didclose){
        update();
    }

    return true;
}

void server::update(){
    // remove closed clients
    auto result = std::remove_if(clients_.begin(), clients_.end(),
                                 [](auto& c){ return !c->is_active(); });
    clients_.erase(result, clients_.end());
    // automatically purge stale users
    // LATER add an option so that users will persist
    for (auto it = users_.begin(); it != users_.end(); ){
        if (!(*it)->is_active()){
            it = users_.erase(it);
        } else {
            ++it;
        }
    }
    // automatically purge empty groups
    // LATER add an option so that groups will persist
    for (auto it = groups_.begin(); it != groups_.end(); ){
        if ((*it)->num_users() == 0){
            it = groups_.erase(it);
        } else {
            ++it;
        }
    }
}

void server::receive_udp(){
    if (udpsocket_ < 0){
        return;
    }
    char buf[AOO_MAXPACKETSIZE];
    ip_address addr;
    int32_t result = recvfrom(udpsocket_, buf, sizeof(buf), 0,
                              addr.address_ptr(), addr.length_ptr());
    if (result > 0){
        try {
            osc::ReceivedPacket packet(buf, result);
            osc::ReceivedMessage msg(packet);

            aoo_type type;
            auto onset = aoo_parse_pattern(buf, result, &type, nullptr);
            if (!onset){
                LOG_WARNING("aoo_server: not an AOO NET message!");
                return;
            }

            if (type != AOO_TYPE_SERVER){
                LOG_WARNING("aoo_server: not a client message!");
                return;
            }

            handle_udp_message(msg, onset, addr);
        } catch (const osc::Exception& e){
            LOG_ERROR("aoo_server: exception in receive_udp: " << e.what());
            // ignore for now
        }
    } else if (result < 0){
        int err = socket_errno();
        // TODO handle error
        LOG_ERROR("aoo_server: recv() failed (" << err << ")");
    }
    // result == 0 -> signalled
}

void server::send_udp_message(const char *msg, int32_t size,
                              const ip_address &addr)
{
    int result = ::sendto(udpsocket_, msg, size, 0,
                          addr.address(), addr.length());
    if (result < 0){
        int err = socket_errno();
        // TODO handle error
        LOG_ERROR("aoo_server: send() failed (" << err << ")");
    }
}

void server::handle_udp_message(const osc::ReceivedMessage &msg, int onset,
                                const ip_address& addr)
{
    auto pattern = msg.AddressPattern() + onset;
    LOG_DEBUG("aoo_server: handle client UDP message " << pattern);

    try {
        if (!strcmp(pattern, AOO_NET_MSG_PING)){
            // reply with /ping message
            char buf[512];
            osc::OutboundPacketStream reply(buf, sizeof(buf));
            reply << osc::BeginMessage(AOO_NET_MSG_CLIENT_PING)
                  << osc::EndMessage;

            send_udp_message(reply.Data(), reply.Size(), addr);
        } else if (!strcmp(pattern, AOO_NET_MSG_REQUEST)){
            // reply with /reply message
            // send *unmapped* address in case the client is IPv4 only
            char buf[512];
            osc::OutboundPacketStream reply(buf, sizeof(buf));
            reply << osc::BeginMessage(AOO_NET_MSG_CLIENT_REPLY)
                  << addr.name_unmapped() << addr.port()
                  << osc::EndMessage;

            send_udp_message(reply.Data(), reply.Size(), addr);
        } else {
            LOG_ERROR("aoo_server: unknown message " << pattern);
        }
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_server: exception on handling " << pattern
                  << " message: " << e.what());
        // ignore for now
    }
}

bool server::signal(){
    return socket_signal(udpsocket_);
}

/*////////////////////////// user ///////////////////////////*/

void user::on_close(server& s){
    // disconnect user from groups
    for (auto& grp : groups_){
        grp->remove_user(*this);
        s.on_user_left_group(*this, *grp);
    }

    s.on_user_left(*this);

    groups_.clear();
    // clear endpoint so the server knows it can remove the user
    endpoint_ = nullptr;
}

bool user::add_group(std::shared_ptr<group> grp){
    auto it = std::find(groups_.begin(), groups_.end(), grp);
    if (it == groups_.end()){
        groups_.push_back(grp);
        return true;
    } else {
        return false;
    }
}

bool user::remove_group(const group& grp){
    // find by address
    auto it = std::find_if(groups_.begin(), groups_.end(),
                           [&](auto& g){ return g.get() == &grp; });
    if (it != groups_.end()){
        groups_.erase(it);
        return true;
    } else {
        return false;
    }
}

/*////////////////////////// group /////////////////////////*/

bool group::add_user(std::shared_ptr<user> grp){
    auto it = std::find(users_.begin(), users_.end(), grp);
    if (it == users_.end()){
        users_.push_back(grp);
        return true;
    } else {
        LOG_ERROR("group::add_user: bug");
        return false;
    }
}

bool group::remove_user(const user& usr){
    // find by address
    auto it = std::find_if(users_.begin(), users_.end(),
                           [&](auto& u){ return u.get() == &usr; });
    if (it != users_.end()){
        users_.erase(it);
        return true;
    } else {
        LOG_ERROR("group::remove_user: bug");
        return false;
    }
}

/*///////////////////////// client_endpoint /////////////////////////////*/

client_endpoint::client_endpoint(server &s, int socket, const ip_address &addr)
    : server_(&s), socket_(socket), addr_(addr)
{
    // set TCP_NODELAY - do we need to do this?
    int val = 1;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val)) < 0){
        LOG_WARNING("client_endpoint: couldn't set TCP_NODELAY");
        // ignore
    }

    sendbuffer_.setup(65536);
    recvbuffer_.setup(65536);
}

client_endpoint::~client_endpoint(){
#ifdef _WIN32
    WSACloseEvent(event_);
#endif
    close();
}

bool client_endpoint::match(const ip_address& addr) const {
    // match public UDP addresses!
    for (auto& a : public_addresses_){
        if (a == addr){
            return true;
        }
    }
    return false;
}

void client_endpoint::close(){
    if (socket_ >= 0){
        LOG_VERBOSE("aoo_server: close client endpoint");
        socket_close(socket_);
        socket_ = -1;

        if (user_){
            user_->on_close(*server_);
        }
    }
}

void client_endpoint::send_message(const char *msg, int32_t size){
    if (sendbuffer_.write_packet((const uint8_t *)msg, size)){
        while (sendbuffer_.read_available()){
            uint8_t buf[1024];
            int32_t total = sendbuffer_.read_bytes(buf, sizeof(buf));

            int32_t nbytes = 0;
            while (nbytes < total){
                auto res = ::send(socket_, (char *)buf + nbytes, total - nbytes, 0);
                if (res >= 0){
                    nbytes += res;
                #if 0
                    LOG_VERBOSE("aoo_server: sent " << res << " bytes");
                #endif
                } else {
                    auto err = socket_errno();
                    // TODO handle error
                    LOG_ERROR("aoo_server: send() failed (" << err << ")");
                    return;
                }
            }
        }
        LOG_DEBUG("aoo_server: sent " << msg << " to client");
    } else {
        LOG_ERROR("aoo_server: couldn't send " << msg << " to client");
    }
}

bool client_endpoint::receive_data(){
    char buffer[AOO_MAXPACKETSIZE];
    auto result = recv(socket_, buffer, sizeof(buffer), 0);
    if (result == 0){
        LOG_WARNING("client_endpoint: connection was closed");
        return false;
    }
    if (result < 0){
        int err = socket_errno();
        // TODO handle error
        LOG_ERROR("client_endpoint: recv() failed (" << err << ")");
        return false;
    }

    recvbuffer_.write_bytes((uint8_t *)buffer, result);

    // handle packets
    uint8_t buf[AOO_MAXPACKETSIZE];
    int32_t size;
    while ((size = recvbuffer_.read_packet(buf, sizeof(buf))) > 0){
        try {
            osc::ReceivedPacket packet((char *)buf, size);
            if (packet.IsBundle()){
                osc::ReceivedBundle bundle(packet);
                if (!handle_bundle(bundle)){
                    return false;
                }
            } else {
                if (!handle_message(packet.Contents(), packet.Size())){
                    return false;
                }
            }
        } catch (const osc::Exception& e){
            LOG_ERROR("aoo_server: exception in client_endpoint::receive_data: " << e.what());
            return false; // close
        }
    }
    return true;
}

bool client_endpoint::handle_message(const char *data, int32_t n){
    osc::ReceivedPacket packet(data, n);
    osc::ReceivedMessage msg(packet);

    int32_t type;
    auto onset = aoo_net_parse_pattern(data, n, &type);
    if (!onset){
        LOG_WARNING("aoo_server: not an AOO NET message!");
        return false;
    }

    try {
        if (type == AOO_TYPE_SERVER){
            auto pattern = msg.AddressPattern() + onset;
            LOG_DEBUG("aoo_server: got server message " << pattern);
            if (!strcmp(pattern, AOO_NET_MSG_PING)){
                handle_ping(msg);
            } else if (!strcmp(pattern, AOO_NET_MSG_LOGIN)){
                handle_login(msg);
            } else if (!strcmp(pattern, AOO_NET_MSG_GROUP_JOIN)){
                handle_group_join(msg);
            } else if (!strcmp(pattern, AOO_NET_MSG_GROUP_LEAVE)){
                handle_group_leave(msg);
            } else {
                LOG_ERROR("aoo_server: unknown server message " << pattern);
                return false;
            }
        } else if (type == AOO_TYPE_RELAY){
            server_->handle_relay_message(msg, addr_);
        } else {
            LOG_WARNING("aoo_client: got unexpected message " << msg.AddressPattern());
            return false;
        }

        return true;
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_server: exception on handling " << msg.AddressPattern()
                  << " message: " << e.what());
        return false;
    }
}

bool client_endpoint::handle_bundle(const osc::ReceivedBundle &bundle){
    auto it = bundle.ElementsBegin();
    while (it != bundle.ElementsEnd()){
        if (it->IsBundle()){
            osc::ReceivedBundle b(*it);
            if (!handle_bundle(b)){
                return false;
            }
        } else {
            if (!handle_message(it->Contents(), it->Size())){
                return false;
            }
        }
        ++it;
    }
    return true;
}

void client_endpoint::handle_ping(const osc::ReceivedMessage& msg){
    // send reply
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream reply(buf, sizeof(buf));
    reply << osc::BeginMessage(AOO_NET_MSG_CLIENT_PING) << osc::EndMessage;

    send_message(reply.Data(), reply.Size());
}

void client_endpoint::handle_login(const osc::ReceivedMessage& msg)
{
    int32_t result = 0;
    uint32_t version = 0;
    std::string errmsg;

    auto it = msg.ArgumentsBegin();
    auto count = msg.ArgumentCount();
    if (count > 6){
        version = (uint32_t)(it++)->AsInt32();
        count--;
    }
    // for now accept login messages without version.
    // LATER they should fail, so clients have to upgrade.
    if (version == 0 || check_version(version)){
        std::string username = (it++)->AsString();
        std::string password = (it++)->AsString();
        count -= 2;

        server::error err;
        if (!user_){
            user_ = server_->get_user(username, password, version, err);
            if (user_){
                // success - collect addresses
                while (count >= 2){
                    std::string ip = (it++)->AsString();
                    int32_t port = (it++)->AsInt32();
                    ip_address addr(ip, port, server_->type());
                    if (addr.valid()){
                        public_addresses_.push_back(addr);
                    }
                    count -= 2;
                }
                user_->set_endpoint(this);

                LOG_VERBOSE("aoo_server: login: id: " << user_->id
                            << ", username: " << username << ", password: " << password);

                result = 1;

                server_->on_user_joined(*user_);
            } else {
                errmsg = server::error_to_string(err);
            }
        } else {
            errmsg = "already logged in"; // shouldn't happen
        }
    } else {
        errmsg = "version not supported";
    }
    // send reply
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream reply(buf, sizeof(buf));
    reply << osc::BeginMessage(AOO_NET_MSG_CLIENT_LOGIN) << result;
    if (result){
        reply << user_->id;
    } else {
        reply << errmsg.c_str();
    }
    reply << osc::EndMessage;

    send_message(reply.Data(), reply.Size());
}

void client_endpoint::handle_group_join(const osc::ReceivedMessage& msg)
{
    int result = 0;
    std::string errmsg;

    auto it = msg.ArgumentsBegin();
    std::string name = (it++)->AsString();
    std::string password = (it++)->AsString();

    server::error err;
    if (user_){
        auto grp = server_->get_group(name, password, err);
        if (grp){
            if (user_->add_group(grp)){
                grp->add_user(user_);
                server_->on_user_joined_group(*user_, *grp);
                result = 1;
            } else {
                errmsg = "already a group member";
            }
        } else {
            errmsg = server::error_to_string(err);
        }
    } else {
        errmsg = "not logged in";
    }

    // send reply
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream reply(buf, sizeof(buf));
    reply << osc::BeginMessage(AOO_NET_MSG_CLIENT_GROUP_JOIN)
          << name.c_str() << result << errmsg.c_str() << osc::EndMessage;

    send_message(reply.Data(), reply.Size());
}

void client_endpoint::handle_group_leave(const osc::ReceivedMessage& msg){
    int result = 0;
    std::string errmsg;

    auto it = msg.ArgumentsBegin();
    std::string name = (it++)->AsString();

    if (user_){
        auto grp = server_->find_group(name);
        if (grp){
            if (user_->remove_group(*grp)){
                grp->remove_user(*user_);
                server_->on_user_left_group(*user_, *grp);
                result = 1;
            } else {
                errmsg = "not a group member";
            }
        } else {
            errmsg = "couldn't find group";
        }
    } else {
        errmsg = "not logged in";
    }

    // send reply
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream reply(buf, sizeof(buf));
    reply << osc::BeginMessage(AOO_NET_MSG_CLIENT_GROUP_LEAVE)
          << name.c_str() << result << errmsg.c_str() << osc::EndMessage;

    send_message(reply.Data(), reply.Size());
}

/*///////////////////// events ////////////////////////*/

server::error_event::error_event(int32_t type, int32_t result,
                                 const char * errmsg)
{
    error_event_.type = type;
    error_event_.errorcode = result;
    error_event_.errormsg = copy_string(errmsg);
}

server::error_event::~error_event()
{
    delete error_event_.errormsg;
}

server::user_event::user_event(int32_t type,
                               const char *name, int32_t id,
                               const ip_address& address){
    user_event_.type = type;
    user_event_.user_name = copy_string(name);
    user_event_.user_id = id;
    user_event_.address = copy_sockaddr(address.address(), address.length());
    user_event_.length = address.length();
}

server::user_event::~user_event()
{
    delete user_event_.user_name;
    delete (const sockaddr *)user_event_.address;
}

server::group_event::group_event(int32_t type, const char *group,
                                 const char *user, int32_t id)
{
    group_event_.type = type;
    group_event_.group_name = copy_string(group);
    group_event_.user_name = copy_string(user);
    group_event_.user_id = id;
}

server::group_event::~group_event()
{
    delete group_event_.group_name;
    delete group_event_.user_name;
}

} // net
} // aoo
