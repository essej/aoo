/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "client.hpp"

#include <cstring>
#include <functional>

/*//////////////////// AoO client /////////////////////*/

aoonet_client * aoonet_client_new(void *udpsocket, aoo_sendfn fn) {
    return new aoo::net::client(udpsocket, fn);
}

aoo::net::client::client(void *udpsocket, aoo_sendfn fn)
    : udpsocket_(udpsocket), sendfn_(fn)
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
    start_time_ = aoo_osctime_get();

    while (!quit_.load()){
        double timeout = 0;

        time_tag now = aoo_osctime_get();
        elapsed_time_ = time_tag::duration(start_time_, now);

        if (tcpsocket_ >= 0){
            auto diff = elapsed_time_ - last_ping_time_;
            auto ping_interval = ping_interval_.load();
            if (diff >= ping_interval){
                send_ping();
                last_ping_time_ = elapsed_time_;
                timeout = ping_interval;
            } else {
                timeout = ping_interval - diff;
            }
        } else {
            timeout = 1e9;
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
    auto cmd = std::make_unique<connect_cmd>();
    cmd->host = host;
    cmd->port = port;
    cmd->user = username;
    cmd->password = pwd;

    if (commands_.write_available()){
        commands_.write(std::move(cmd));
    }

    signal();

    return 1;
}

int32_t aoonet_client_disconnect(aoonet_client *client){
    return client->disconnect();
}

int32_t aoo::net::client::disconnect(){
    auto cmd = std::make_unique<disconnect_cmd>();

    if (commands_.write_available()){
        commands_.write(std::move(cmd));
    }

    signal();

    return 1;
}

int32_t aoonet_client_group_join(aoonet_client *client, const char *group, const char *pwd){
    return client->group_join(group, pwd);
}

int32_t aoo::net::client::group_join(const char *group, const char *pwd){
    auto cmd = std::make_unique<group_join_cmd>();
    cmd->group = group;
    cmd->password = pwd;

    if (commands_.write_available()){
        commands_.write(std::move(cmd));
    }

    signal();

    return 1;
}

int32_t aoonet_client_group_leave(aoonet_client *client, const char *group){
    return client->group_leave(group);
}

int32_t aoo::net::client::group_leave(const char *group){
    auto cmd = std::make_unique<group_leave_cmd>();
    cmd->group = group;

    if (commands_.write_available()){
        commands_.write(std::move(cmd));
    }

    signal();

    return 1;
}

int32_t aoonet_client_handle_message(aoonet_client *client, const char *data, int32_t n, void *addr){
    return client->handle_message(data, n, addr);
}

int32_t aoo::net::client::handle_message(const char *data, int32_t n, void *addr){
    return 1;
}

int32_t aoonet_client_send(aoonet_client *client){
    return client->send();
}

int32_t aoo::net::client::send(){
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

void client::do_connect(const std::string &host, int port,
                        const std::string &user, const std::string &pwd)
{
    do_disconnect();

    tcpsocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpsocket_ >= 0){
        // resolve host name
        struct hostent *he = gethostbyname(host.c_str());
        if (!he){
            // TODO handle error
            fprintf(stderr, "aoo_client: couldn't connect (%d)\n", socket_errno());
            fflush(stderr);

            socket_close(tcpsocket_);
            tcpsocket_ = -1;
            return;
        }

        // copy IP address
        struct sockaddr_in *addr = (struct sockaddr_in *)&remote_addr_.addr;
        memset(addr, 0, sizeof(*addr));
        addr->sin_family = AF_INET;
        addr->sin_port = htons(port);
        memcpy(&addr->sin_addr, he->h_addr_list[0], he->h_length);
        remote_addr_.len = sizeof(struct sockaddr_in);

        remote_port_ = port;

        // set TCP_NODELAY
        int val = 1;
        if (setsockopt(tcpsocket_, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val)) < 0){
            fprintf(stderr, "aoo_client: couldn't set TCP_NODELAY\n");
            fflush(stderr);
            // ignore
        }

        // set non-blocking
        // (this is not necessary on Windows, since WSAEventSelect will do it automatically)
    #ifndef _WIN32
        val = 1;
        if (ioctl(tcpsocket_, FIONBIO, (char *)&val) < 0){
            fprintf(stderr, "aoo_client: couldn't set socket to non-blocking (%d)\n", socket_errno());
            fflush(stderr);

            socket_close(tcpsocket_);
            tcpsocket_ = -1;
            return;
        }
    #endif

        // try to connect (LATER make timeout configurable)
        if (socket_connect(tcpsocket_, remote_addr_, 5) < 0){
            // TODO handle error
            fprintf(stderr, "aoo_client: couldn't connect (%d)\n", socket_errno());
            fflush(stderr);

            socket_close(tcpsocket_);
            tcpsocket_ = -1;
            return;
        }

        // get local address
        if (getsockname(tcpsocket_,
                        (struct sockaddr *)&local_addr_.addr, &local_addr_.len) < 0) {
            // TODO handle error
            fprintf(stderr, "aoo_client: couldn't get socket name (%d)\n", socket_errno());
            fflush(stderr);

            socket_close(tcpsocket_);
            tcpsocket_ = -1;
            return;
        }

    #ifdef _WIN32
        // register event with socket
        WSAEventSelect(tcpsocket_, sockevent_, FD_READ | FD_WRITE | FD_CLOSE);
    #else

    #endif
        auto remote_addr = (struct sockaddr_in *)&remote_addr_;
        auto local_addr = (struct sockaddr_in *)&local_addr_;

        fprintf(stderr, "aoo_client: successfully connected to %s on port %d\n",
                inet_ntoa(remote_addr->sin_addr), port);
        fprintf(stderr, "aoo_client: local address: %s\n",
                inet_ntoa(local_addr->sin_addr));
        fflush(stderr);
    } else {
        // TODO handle error
        fprintf(stderr, "aoo_client: couldn't create socket (%d)\n", socket_errno());
        fflush(stderr);
    }
}

void client::do_disconnect(){
    if (tcpsocket_ >= 0){
    #ifdef _WIN32
        // unregister event from socket.
        // actually, I think this happens automatically when closing the socket.
        WSAEventSelect(tcpsocket_, sockevent_, 0);
    #else

    #endif
        socket_close(tcpsocket_);
        tcpsocket_ = -1;
        fprintf(stderr, "aoo_client: disconnected\n");
        fflush(stderr);
    }
}

void client::do_group_join(const std::string &group, const std::string &pwd){

}

void client::do_group_leave(const std::string &group){

}

void client::send_ping(){
    if (tcpsocket_ >= 0){
        char buf[64];
        osc::OutboundPacketStream msg(buf, sizeof(buf));
        msg << osc::BeginMessage(AOO_MSG_DOMAIN AOO_MSG_SERVER AOO_MSG_PING)
            << osc::EndMessage;

        send_server_message(msg.Data(), msg.Size());
    } else {
        fprintf(stderr, "aoo_client: bug send_ping()\n");
        fflush(stderr);
    }
}

void client::wait_for_event(float timeout){
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

    DWORD time = timeout * 1000;
    DWORD result = WaitForMultipleObjects(numevents, events, FALSE, time);
    if (result == WAIT_TIMEOUT){
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
            fprintf(stderr, "aoo_client: connection was closed (%d)\n", err);
            fflush(stderr);

            do_disconnect();
        } else {
            // ignore FD_WRITE event
        }
    }
#else
    struct fd_set rdset;
    FD_ZERO(&rdset);
    int fdmax = std::max<int>(waitpipe_[0], tcpsocket_); // tcpsocket might be -1
    FD_SET(waitpipe_[0], &rdset);
    if (tcpsocket_ >= 0){
        FD_SET(tcpsocket_, &rdset);
    }

    struct timeval time;
    time.tv_sec = (time_t)timeout;
    time.tv_usec = (timeout - (double)time.tv_sec) * 1000000;

    if (select(fdmax + 1, &rdset, 0, 0, &time) < 0){
        int err = errno;
        if (err == EINTR){
            // ?
        } else {
            fprintf(stderr, "aoo_client: select failed (%d)\n", err);
            fflush(stderr);
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
                                    handle_server_message(msg);
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
                            handle_server_message(msg);
                        } else if (packet.IsBundle()){
                            osc::ReceivedBundle bundle(packet);
                            dispatchBundle(bundle);
                        } else {
                            // ignore
                        }
                    } catch (const osc::Exception& e){
                        fprintf(stderr, "aoo_client: %s\n", e.what());
                        fflush(stderr);
                    }
                } else {
                    break;
                }
            }
        } else if (result == 0){
            // connection closed by the remote server
            do_disconnect();
        } else {
            int err = socket_errno();
        #ifdef _WIN32
            if (err == WSAEWOULDBLOCK)
        #else
            if (err == EWOULDBLOCK)
        #endif
            {
            #if 0
                fprintf(stderr, "aoo_client: recv() would block\n");
                fflush(stderr);
            #endif
            } else {

                // TODO handle error
                fprintf(stderr, "aoo_client: recv() failed (%d)\n", err);
                fflush(stderr);
            }
            return;
        }
    }
}

void client::send_server_message(const char *data, int32_t size){
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
                        fprintf(stderr, "aoo_client: sent %d bytes\n", res);
                        fflush(stderr);
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
                            fprintf(stderr, "aoo_client: send() would block\n");
                            fflush(stderr);
                        #endif
                        }
                        else
                        {
                            // TODO handle error
                            fprintf(stderr, "aoo_client: send() failed (%d)\n", socket_errno());
                            fflush(stderr);
                        }
                        return;
                    }
                }
            }
            fprintf(stderr, "aoo_client: sent %s to server\n", data);
            fflush(stderr);
        } else {
            fprintf(stderr, "aoo_client: couldn't send %s to server\n", data);
            fflush(stderr);
        }
    } else {
        fprintf(stderr, "aoo_client: can't send server message - socket closed!\n");
        fflush(stderr);
    }
}

void client::handle_server_message(const osc::ReceivedMessage& msg){
    fprintf(stderr, "aoo_client: got message %s from server\n", msg.AddressPattern());
    fflush(stderr);
}

void client::signal(){
#ifdef _WIN32
    SetEvent(waitevent_);
#else
    write(waitpipe_[1], "\n", 1);
#endif
}

} // net
} // aoo
