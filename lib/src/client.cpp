/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "client.hpp"

#include <cstring>
#include <functional>

#include "md5/md5.h"

namespace aoo {
namespace net {

std::string encrypt(const std::string& input){
    uint8_t result[16];
    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, (uint8_t *)input.data(), input.size());
    MD5Final(result, &ctx);

    char output[33];
    for (int i = 0; i < 16; ++i){
        sprintf(&output[i * 2], "%02X", result[i]);
    }

    return output;
}

}
} // aoo

/*//////////////////// AoO client /////////////////////*/

aoonet_client * aoonet_client_new(void *udpsocket, aoo_sendfn fn, int port) {
    return new aoo::net::client(udpsocket, fn, port);
}

aoo::net::client::client(void *udpsocket, aoo_sendfn fn, int port)
    : udpsocket_(udpsocket), sendfn_(fn), udpport_(port)
{
#ifdef _WIN32
    sockevent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    waitevent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
#else
    if (pipe(waitpipe_) != 0){
        // TODO handle error
    }
#endif
    commands_.resize(256, 1);
    events_.resize(256, 1);
    sendbuffer_.setup(65536);
    recvbuffer_.setup(65536);
}

void aoonet_client_free(aoonet_client *client){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::net::client *>(client);
}

aoo::net::client::~client() {
    do_disconnect();

#ifdef _WIN32
    CloseHandle(sockevent_);
    CloseHandle(waitevent_);
#else
    close(waitpipe_[0]);
    close(waitpipe_[1]);
#endif
}

int32_t aoonet_client_run(aoonet_client *client){
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
                    msg << osc::BeginMessage(AOONET_MSG_SERVER_PING)
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

        wait_for_event(timeout);

        // handle commands
        while (commands_.read_available()){
            std::unique_ptr<icommand> cmd;
            commands_.read(cmd);
            cmd->perform(*this);
        }
    }
    return 1;
}

int32_t aoonet_client_quit(aoonet_client *client){
    return client->quit();
}

int32_t aoo::net::client::quit(){
    quit_.store(true);
    signal();
    return 1;
}

int32_t aoonet_client_connect(aoonet_client *client, const char *host, int port,
                           const char *username, const char *pwd)
{
    return client->connect(host, port, username, pwd);
}

int32_t aoo::net::client::connect(const char *host, int port,
                             const char *username, const char *pwd)
{
    auto state = state_.load();
    if (state != client_state::disconnected){
        if (state == client_state::connected){
            LOG_ERROR("aoo_client: already connected!");
        } else {
            LOG_ERROR("aoo_client: already connecting!");
        }
        return 0;
    }

    username_ = username;
    password_ = encrypt(pwd);

    state_ = client_state::connecting;

    push_command(std::make_unique<connect_cmd>(host, port));

    signal();

    return 1;
}

int32_t aoonet_client_disconnect(aoonet_client *client){
    return client->disconnect();
}

int32_t aoo::net::client::disconnect(){
    auto state = state_.load();
    if (state != client_state::connected){
        LOG_WARNING("aoo_client: not connected");
        return 0;
    }

    push_command(std::make_unique<disconnect_cmd>(command_reason::user));

    signal();

    return 1;
}

int32_t aoonet_client_group_join(aoonet_client *client, const char *group, const char *pwd){
    return client->group_join(group, pwd);
}

int32_t aoo::net::client::group_join(const char *group, const char *pwd){
    push_command(std::make_unique<group_join_cmd>(group, encrypt(pwd)));

    signal();

    return 1;
}

int32_t aoonet_client_group_leave(aoonet_client *client, const char *group){
    return client->group_leave(group);
}

int32_t aoo::net::client::group_leave(const char *group){
    push_command(std::make_unique<group_leave_cmd>(group));

    signal();

    return 1;
}

int32_t aoonet_client_handle_message(aoonet_client *client, const char *data, int32_t n, void *addr){
    return client->handle_message(data, n, addr);
}

int32_t aoo::net::client::handle_message(const char *data, int32_t n, void *addr){
    if (static_cast<struct sockaddr *>(addr)->sa_family != AF_INET){
        return 0;
    }
    try {
        osc::ReceivedPacket packet(data, n);
        osc::ReceivedMessage msg(packet);

        LOG_DEBUG("aoo_client: handle UDP message " << msg.AddressPattern());

        ip_address address((struct sockaddr *)addr);
        if (address == remote_addr_){
            // server message
            handle_server_message_udp(msg);
        } else {
            // peer message
            handle_peer_message_udp(msg, address);
        }
        return 1;
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_client: " << e.what());
    }

    return 0;
}

int32_t aoonet_client_send(aoonet_client *client){
    return client->send();
}

int32_t aoo::net::client::send(){
    auto state = state_.load();
    if (state != client_state::disconnected){
        time_tag now = time_tag::now();
        auto elapsed_time = time_tag::duration(start_time_, now);
        auto delta = elapsed_time - last_udp_ping_time_;

        if (state == client_state::handshake){
            // check for time out
            if (first_udp_ping_time_ == 0){
                first_udp_ping_time_ = elapsed_time;
            } else if ((elapsed_time - first_udp_ping_time_) > request_timeout_.load()){
                // request has timed out!
                first_udp_ping_time_ = 0;

                push_command(std::make_unique<disconnect_cmd>(command_reason::timeout));

                signal();

                return 1; // ?
            }
            // send handshakes in fast succession
            if (delta >= request_interval_.load()){
                char buf[64];
                osc::OutboundPacketStream msg(buf, sizeof(buf));
                msg << osc::BeginMessage(AOONET_MSG_SERVER_REQUEST) << osc::EndMessage;

                send_server_message_udp(msg.Data(), msg.Size());
                last_udp_ping_time_ = elapsed_time;
            }
        } else if (state == client_state::connected){
            // send regular pings
            if (delta >= ping_interval_.load()){
                char buf[64];
                osc::OutboundPacketStream msg(buf, sizeof(buf));
                msg << osc::BeginMessage(AOONET_MSG_SERVER_PING)
                    << osc::EndMessage;

                send_server_message_udp(msg.Data(), msg.Size());
                last_udp_ping_time_ = elapsed_time;
            }
        } else {
            // ignore
            return 1;
        }



    }
    return 1;
}

int32_t aoonet_client_events_available(aoonet_server *client){
    return client->events_available();
}

int32_t aoo::net::client::events_available(){
    return 1;
}

int32_t aoonet_client_handle_events(aoonet_client *client, aoo_eventhandler fn, void *user){
    return client->handle_events(fn, user);
}

int32_t aoo::net::client::handle_events(aoo_eventhandler fn, void *user){
    return 1;
}

namespace aoo {
namespace net {

void client::do_connect(const std::string &host, int port)
{
    if (tcpsocket_ >= 0){
        LOG_ERROR("aoo_client: bug client::do_connect()");
        return;
    }

    int err = try_connect(host, port);
    if (err != 0){
        do_disconnect(command_reason::error, err);
        return;
    }

    first_udp_ping_time_ = 0;
    state_ = client_state::handshake;

}

void client::do_disconnect(command_reason reason, int error){
    if (tcpsocket_ >= 0){
    #ifdef _WIN32
        // unregister event from socket.
        // actually, I think this happens automatically when closing the socket.
        WSAEventSelect(tcpsocket_, sockevent_, 0);
    #endif
        socket_close(tcpsocket_);
        tcpsocket_ = -1;
        LOG_VERBOSE("aoo_client: disconnected");
    }
    // TODO handle reason
    state_ = client_state::disconnected;
}

int client::try_connect(const std::string &host, int port){
    tcpsocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpsocket_ < 0){
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't create socket (" << err << ")");
        return err;
    }
    // resolve host name
    struct hostent *he = gethostbyname(host.c_str());
    if (!he){
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't connect (" << err << ")");
        return err;
    }

    // copy IP address
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], he->h_length);

    remote_addr_ = ip_address((struct sockaddr *)&sa);

    // set TCP_NODELAY
    int val = 1;
    if (setsockopt(tcpsocket_, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val)) < 0){
        LOG_WARNING("aoo_client: couldn't set TCP_NODELAY");
        // ignore
    }

    // try to connect (LATER make timeout configurable)
    if (socket_connect(tcpsocket_, remote_addr_, 5) < 0){
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't connect (" << err << ")");
        return err;
    }

    // get local network interface
    ip_address tmp;
    if (getsockname(tcpsocket_,
                    (struct sockaddr *)&tmp.address, &tmp.length) < 0) {
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't get socket name (" << err << ")");
        return err;
    }
    local_addr_ = ip_address(tmp.name(), udpport_);

#ifdef _WIN32
    // register event with socket
    WSAEventSelect(tcpsocket_, sockevent_, FD_READ | FD_WRITE | FD_CLOSE);
#else
    // set non-blocking
    // (this is not necessary on Windows, since WSAEventSelect will do it automatically)
    val = 1;
    if (ioctl(tcpsocket_, FIONBIO, (char *)&val) < 0){
        int err = socket_errno();
        LOG_ERROR("aoo_client: couldn't set socket to non-blocking (" << err << ")");
        return err;
    }
#endif

    LOG_VERBOSE("aoo_client: successfully connected to "
                << remote_addr_.name() << " on port " << remote_addr_.port());
    LOG_VERBOSE("aoo_client: local address: " << local_addr_.name());

    return 0;
}

void client::do_login(){
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    msg << osc::BeginMessage(AOONET_MSG_SERVER_LOGIN)
        << username_.c_str() << password_.c_str()
        << public_addr_.name().c_str() << public_addr_.port()
        << local_addr_.name().c_str() << local_addr_.port()
        << osc::EndMessage;

    send_server_message_tcp(msg.Data(), msg.Size());
}

void client::do_group_join(const std::string &group, const std::string &pwd){

}

void client::do_group_leave(const std::string &group){

}

void client::wait_for_event(float timeout){
    LOG_DEBUG("aoo_server: wait " << timeout << " seconds");
#ifdef _WIN32
    HANDLE events[2];
    int numevents;
    events[0] = waitevent_;
    if (tcpsocket_ >= 0){
        events[1] = sockevent_;
        numevents = 2;
    } else {
        numevents = 1;
    }

    DWORD time = timeout < 0 ? INFINITE : (timeout * 1000 + 0.5); // round up to 1 ms!
    DWORD result = WaitForMultipleObjects(numevents, events, FALSE, time);
    if (result == WAIT_TIMEOUT){
        LOG_DEBUG("aoo_server: timed out");
        return;
    }
    // only the second event is a socket
    if (result - WAIT_OBJECT_0 == 1){
        WSANETWORKEVENTS ne;
        memset(&ne, 0, sizeof(ne));
        WSAEnumNetworkEvents(tcpsocket_, sockevent_, &ne);

        if (ne.lNetworkEvents & FD_READ){
            // ready to receive data from server
            receive_data();
        } else if (ne.lNetworkEvents & FD_CLOSE){
            // connection was closed
            int err = ne.iErrorCode[FD_CLOSE_BIT];
            LOG_WARNING("aoo_client: connection was closed (" << err << ")");

            do_disconnect(command_reason::error, err);
        } else {
            // ignore FD_WRITE event
        }
    }
#else
#if 1 // poll() version
    struct pollfd fds[2];
    fds[0].fd = waitpipe_[0];
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = tcpsocket_;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    // round up to 1 ms! negative value: block indefinitely
    int result = poll(fds, 2, timeout * 1000.0 + 0.5);
    if (result < 0){
        int err = errno;
        if (err == EINTR){
            // ?
        } else {
            LOG_ERROR("aoo_client: poll failed (" << err << ")");
            // what to do?
        }
        return;
    }

    if (fds[0].revents & POLLIN){
        // clear pipe
        char c;
        read(waitpipe_[0], &c, 1);
    }

    if (fds[1].revents & POLLIN){
        receive_data();
    }
#else // select() version
    fd_set rdset;
    FD_ZERO(&rdset);
    int fdmax = std::max<int>(waitpipe_[0], tcpsocket_); // tcpsocket might be -1
    FD_SET(waitpipe_[0], &rdset);
    if (tcpsocket_ >= 0){
        FD_SET(tcpsocket_, &rdset);
    }

    struct timeval time;
    time.tv_sec = (time_t)timeout;
    time.tv_usec = (timeout - (double)time.tv_sec) * 1000000;

    if (select(fdmax + 1, &rdset, 0, 0, timeout < 0 ? nullptr : &time) < 0){
        int err = errno;
        if (err == EINTR){
            // ?
        } else {
            LOG_ERROR("aoo_client: select failed (" << err << ")");
            // what to do?
        }
        return;
    }

    if (FD_ISSET(waitpipe_[0], &rdset)){
        // clear pipe
        char c;
        read(waitpipe_[0], &c, 1);
    }

    if (FD_ISSET(tcpsocket_, &rdset)){
        receive_data();
    }
#endif
#endif
}

void client::receive_data(){
    // read as much data as possible until recv() would block
    while (true){
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

                        std::function<void(const osc::ReceivedBundle&)> dispatchBundle
                                = [&](const osc::ReceivedBundle& bundle){
                            auto it = bundle.ElementsBegin();
                            while (it != bundle.ElementsEnd()){
                                if (it->IsMessage()){
                                    osc::ReceivedMessage msg(*it);
                                    handle_server_message_tcp(msg);
                                } else if (it->IsBundle()){
                                    osc::ReceivedBundle bundle2(*it);
                                    dispatchBundle(bundle2);
                                } else {
                                    // ignore
                                }
                                ++it;
                            }
                        };

                        if (packet.IsMessage()){
                            osc::ReceivedMessage msg(packet);
                            handle_server_message_tcp(msg);
                        } else if (packet.IsBundle()){
                            osc::ReceivedBundle bundle(packet);
                            dispatchBundle(bundle);
                        } else {
                            // ignore
                        }
                    } catch (const osc::Exception& e){
                        LOG_ERROR("aoo_client: " << e.what());
                    }
                } else {
                    break;
                }
            }
        } else if (result == 0){
            // connection closed by the remote server
            do_disconnect(command_reason::error, 0);
        } else {
            int err = socket_errno();
        #ifdef _WIN32
            if (err == WSAEWOULDBLOCK)
        #else
            if (err == EWOULDBLOCK)
        #endif
            {
            #if 0
                LOG_VERBOSE("aoo_client: recv() would block");
            #endif
            } else {
                // TODO handle error
                LOG_ERROR("aoo_client: recv() failed (" << err << ")");
            }
            return;
        }
    }
}

void client::send_server_message_tcp(const char *data, int32_t size){
    if (tcpsocket_ >= 0){
        if (sendbuffer_.write_packet((const uint8_t *)data, size)){
            // try to send as much as possible until send() would block
            while (true){
                uint8_t buf[1024];
                int32_t total = 0;
                // first try to send pending data
                if (!pending_send_data_.empty()){
                     std::copy(pending_send_data_.begin(), pending_send_data_.end(), buf);
                     total = pending_send_data_.size();
                     pending_send_data_.clear();
                } else if (sendbuffer_.read_available()){
                     total = sendbuffer_.read_bytes(buf, sizeof(buf));
                } else {
                    break;
                }

                int32_t nbytes = 0;
                while (nbytes < total){
                    auto res = ::send(tcpsocket_, (char *)buf + nbytes, total - nbytes, 0);
                    if (res >= 0){
                        nbytes += res;
                    #if 0
                        LOG_VERBOSE("aoo_client: sent " << res << " bytes");
                    #endif
                    } else {
                        auto err = socket_errno();
                    #ifdef _WIN32
                        if (err == WSAEWOULDBLOCK)
                    #else
                        if (err == EWOULDBLOCK)
                    #endif
                        {
                            // store in pending buffer
                            pending_send_data_.assign(buf + nbytes, buf + total);
                        #if 1
                            LOG_VERBOSE("aoo_client: send() would block");
                        #endif
                        }
                        else
                        {
                            // TODO handle error
                            LOG_ERROR("aoo_client: send() failed (" << err << ")");
                        }
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
    sendfn_(udpsocket_, data, size, (void *)&remote_addr_.address);
}

void client::handle_server_message_tcp(const osc::ReceivedMessage& msg){
    LOG_DEBUG("aoo_client: got message " << msg.AddressPattern() << " from server");
    if (!strcmp(msg.AddressPattern(), AOONET_MSG_CLIENT_PING)){
        LOG_VERBOSE("aoo_client: got TCP ping from server");
    } else if (!strcmp(msg.AddressPattern(), AOONET_MSG_CLIENT_LOGIN)){
        if (state_.load() == client_state::login){
            // retrieve result
            if (msg.ArgumentCount() < 1){
                LOG_ERROR("client::handle_server_message_tcp: "
                          "too little arguments for /login message");
                return;
            }
            auto it = msg.ArgumentsBegin();
            int32_t status = (it++)->AsInt32();
            if (status > 0){
                // connected!
                state_ = client_state::connected;
                LOG_VERBOSE("aoo_client: successfully logged in!");
                // TODO event
            } else {
                if (msg.ArgumentCount() > 1){
                    std::string errmsg = (it++)->AsString();
                    LOG_VERBOSE("aoo_client: login failed: " << errmsg);
                    // TODO event
                }
                do_disconnect();
            }
        }
    }
}

void client::handle_server_message_udp(const osc::ReceivedMessage &msg){
    if (!strcmp(msg.AddressPattern(), AOONET_MSG_CLIENT_PING)){
        LOG_VERBOSE("aoo_client: got UDP ping from server");
    } else if (!strcmp(msg.AddressPattern(), AOONET_MSG_CLIENT_REPLY)){
        client_state expected = client_state::handshake;
        if (state_.compare_exchange_strong(expected, client_state::login)){
            // retrieve public IP + port
            if (msg.ArgumentCount() < 2){
                LOG_ERROR("client::handle_server_message_udp: "
                          "too little arguments for /reply message");
                return;
            }
            auto it = msg.ArgumentsBegin();
            std::string ip = (it++)->AsString();
            int port = (it++)->AsInt32();
            public_addr_ = ip_address(ip, port);
            LOG_VERBOSE("aoo_client: public endpoint is "
                        << public_addr_.name() << " " << public_addr_.port());

            // now we can try to login
            push_command(std::make_unique<login_cmd>());

            signal();
        }
    } else {
        LOG_WARNING("aoo_client: received unknown UDP message "
                    << msg.AddressPattern() << " from server");
    }
}

void client::handle_peer_message_udp(const osc::ReceivedMessage &msg, const ip_address &addr){
    if (false){

    } else {
        LOG_WARNING("aoo_client: received unknown UDP message "
                    << msg.AddressPattern() << " from peer " << addr.name());
    }
}

void client::signal(){
#ifdef _WIN32
    SetEvent(waitevent_);
#else
    write(waitpipe_[1], "\0", 1);
#endif
}

} // net
} // aoo
