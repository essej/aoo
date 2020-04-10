/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "server.hpp"

#include <functional>
#include <algorithm>

/*//////////////////// AoO server /////////////////////*/

aoonet_server * aoonet_server_new(int port, int32_t *err) {
    int val = 0;

    // make 'any' address
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(port);

    // create and bind UDP socket
    int udpsocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpsocket < 0){
        *err = aoo::net::socket_errno();
        return nullptr;
    }

    // set non-blocking
    // (this is not necessary on Windows, because WSAEventSelect will do it automatically)
#ifndef _WIN32
    val = 1;
    if (ioctl(udpsocket, FIONBIO, (char *)&val) < 0){
        fprintf(stderr, "aoo_server: couldn't set socket to non-blocking (%d)\n", socket_errno());
        fflush(stderr);
        aoo::net::socket_close(udpsocket);
        return nullptr;
    }
#endif

    if (bind(udpsocket, (sockaddr *)&sa, sizeof(sa)) < 0){
        *err = aoo::net::socket_errno();
        aoo::net::socket_close(udpsocket);
        return nullptr;
    }

    // create TCP socket
    int tcpsocket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpsocket < 0){
        *err = aoo::net::socket_errno();
        aoo::net::socket_close(udpsocket);
        return nullptr;
    }

    // set SO_REUSEADDR
    val = 1;
    if (setsockopt(tcpsocket, SOL_SOCKET, SO_REUSEADDR,
                      (char *)&val, sizeof(val)) < 0)
    {
        *err = aoo::net::socket_errno();
        fprintf(stderr, "aoo_server: couldn't set SO_REUSEADDR (%d)\n", *err);
        fflush(stderr);
        aoo::net::socket_close(tcpsocket);
        aoo::net::socket_close(udpsocket);
        return nullptr;
    }

    // set TCP_NODELAY
    if (setsockopt(tcpsocket, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val)) < 0){
        fprintf(stderr, "aoo_server: couldn't set TCP_NODELAY\n");
        fflush(stderr);
        // ignore
    }

    // set non-blocking
    // (this is not necessary on Windows, because WSAEventSelect will do it automatically)
#ifndef _WIN32
    val = 1;
    if (ioctl(tcpsocket_, FIONBIO, (char *)&val) < 0){
        fprintf(stderr, "aoo_server: couldn't set socket to non-blocking (%d)\n", socket_errno());
        fflush(stderr);
        aoo::net::socket_close(tcpsocket);
        aoo::net::socket_close(udpsocket);
        return nullptr;
    }
#endif

    // bind TCP socket
    if (bind(tcpsocket, (sockaddr *)&sa, sizeof(sa)) < 0){
        *err = aoo::net::socket_errno();
        aoo::net::socket_close(tcpsocket);
        aoo::net::socket_close(udpsocket);
        return nullptr;
    }

    // listen
    if (listen(tcpsocket, 32) < 0){
        *err = aoo::net::socket_errno();
        aoo::net::socket_close(tcpsocket);
        aoo::net::socket_close(udpsocket);
        return nullptr;
    }

    return new aoo::net::server(tcpsocket, udpsocket);
}

aoo::net::server::server(int tcpsocket, int udpsocket)
    : tcpsocket_(tcpsocket), udpsocket_(udpsocket)
{
#ifdef _WIN32
    waitevent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    tcpevent_ = WSACreateEvent();
    udpevent_ = WSACreateEvent();
    WSAEventSelect(tcpsocket_, tcpevent_, FD_ACCEPT);
    WSAEventSelect(udpsocket_, udpevent_, FD_READ | FD_WRITE);
#else
    if (pipe(waitpipe_) != 0){
        // TODO handle error
    }
#endif
    commands_.resize(256, 1);
    events_.resize(256, 1);
}

void aoonet_server_free(aoonet_server *server){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::net::server *>(server);
}

aoo::net::server::~server() {
#ifdef _WIN32
    CloseHandle(waitevent_);
    WSACloseEvent(tcpevent_);
    WSACloseEvent(udpevent_);
#else
    close(waitpipe_[0]);
    close(waitpipe_[1]);
#endif

    socket_close(tcpsocket_);
    socket_close(udpsocket_);
}

int32_t aoonet_server_run(aoonet_server *server){
    return server->run();
}

int32_t aoo::net::server::run(){
    while (!quit_.load()){
        // wait for networking or other events
        wait_for_event();

        // handle commands
        while (commands_.read_available()){
            std::unique_ptr<icommand> cmd;
            commands_.read(cmd);
            cmd->perform(*this);
        }
    }

    return 1;
}

int32_t aoonet_server_quit(aoonet_server *server){
    return server->quit();
}

int32_t aoo::net::server::quit(){
    quit_.store(true);
    signal();
    return 0;
}

int32_t aoonet_server_events_available(aoonet_server *server){
    return server->events_available();
}

int32_t aoo::net::server::events_available(){
    return 0;
}

int32_t aoonet_server_handle_events(aoonet_server *server, aoo_eventhandler fn, void *user){
    return server->handle_events(fn, user);
}

int32_t aoo::net::server::handle_events(aoo_eventhandler fn, void *user){
    return 0;
}

namespace aoo {
namespace net {

void server::wait_for_event(){
    bool didclose = false;
#ifdef _WIN32
    // allocate three extra slots for master TCP socket, UDP socket and wait event
    int numevents = (clients_.size() + 3);
    auto events = (HANDLE *)alloca(numevents * sizeof(HANDLE));
    int numclients = clients_.size();
    for (int i = 0; i < numclients; ++i){
        events[i] = clients_[i].event;
    }
    int tcpindex = numclients;
    int udpindex = numclients + 1;
    int waitindex = numclients + 2;
    events[tcpindex] = tcpevent_;
    events[udpindex] = udpevent_;
    events[waitindex] = waitevent_;

    DWORD result = WaitForMultipleObjects(numevents, events, FALSE, INFINITE);

    WSANETWORKEVENTS ne;
    memset(&ne, 0, sizeof(ne));

    int index = result - WAIT_OBJECT_0;
    if (index == tcpindex){
        WSAEnumNetworkEvents(tcpsocket_, tcpevent_, &ne);

        if (ne.lNetworkEvents & FD_ACCEPT){
            // accept new clients
            while (true){
                ip_address addr;
                auto sock = accept(tcpsocket_, (struct sockaddr *)&addr.addr, &addr.len);
                if (sock != INVALID_SOCKET){
                    clients_.emplace_back(*this, sock, addr);
                    fprintf(stderr, "aoo_server: accepted client (IP: %s, port: %d)\n",
                            inet_ntoa(reinterpret_cast<struct sockaddr_in *>(&addr.addr)->sin_addr),
                            (int)htons(reinterpret_cast<struct sockaddr_in *>(&addr.addr)->sin_port));
                    fflush(stderr);
                } else {
                    int err = socket_errno();
                    if (err != WSAEWOULDBLOCK){
                        fprintf(stderr, "aoo_server: couldn't accept client (%d)\n", socket_errno());
                        fflush(stderr);
                    }
                    break;
                }
            }
        }
    } else if (index == udpindex){
        WSAEnumNetworkEvents(udpsocket_, udpevent_, &ne);

        if (ne.lNetworkEvents & FD_READ){
            receive_udp();
        }
    } else if (index >= 0 && index < numclients){
        // iterate over all clients, starting at index (= the first item which caused an event)
        for (int i = index; i < numclients; ++i){
            result = WaitForMultipleObjects(1, &events[i], TRUE, 0);
            if (result == WAIT_FAILED || result == WAIT_TIMEOUT){
                continue;
            }
            WSAEnumNetworkEvents(clients_[i].socket, clients_[i].event, &ne);

            if (ne.lNetworkEvents & FD_READ){
                // receive data from client
                if (!clients_[i].receive_data()){
                    clients_[i].close();
                    didclose = true;
                }
            } else if (ne.lNetworkEvents & FD_CLOSE){
                // connection was closed
                int err = ne.iErrorCode[FD_CLOSE_BIT];
                fprintf(stderr, "aoo_server: client connection was closed (%d)\n", err);
                fflush(stderr);

                clients_[i].close();
                didclose = true;
            } else {
                // ignore FD_WRITE
            }
        }
    }
#else
    // allocate three extra slots for master TCP socket, UDP socket and wait pipe
    int numfds = (clients_.size() + 3);
    auto fds = (struct pollfd *)alloca(numfds * sizeof(pollfd));
    for (int i = 0; i < numfds; ++i){
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }

    for (int i = 0; i < numclients; ++i){
        fds[i].fd = clients_[i].socket;
    }
    int tcpindex = numclients;
    int udpindex = numclients + 1;
    int waitindex = numclients + 2;
    fds[tcpindex].fd = tcpsocket_;
    fds[udpindex].fd = udpsocket_;
    fds[waitindex].fd = waitpipe_[0];

    int result = poll(fds, numfds, -1);
    if (result < 0){
        int err = errno;
        if (err == EINTR){
            // ?
        } else {
            fprintf(stderr, "aoo_server: poll failed (%d)\n", err);
            fflush(stderr);
            // what to do?
        }
        return;
    }

    if (fds[tcpindex].revents & POLLIN){
        // accept new clients
        while (true){
            ip_address addr;
            auto sock = accept(tcpsocket_, (struct sockaddr *)&addr.addr, &addr.len);
            if (sock >= 0){
                clients_.emplace_back(*this, sock, addr);
                fprintf(stderr, "aoo_server: accepted client (IP: %s, port: %d)\n",
                        inet_ntoa(reinterpret_cast<struct sockaddr_in *>(&addr.addr)->sin_addr),
                        (int)htons(reinterpret_cast<struct sockaddr_in *>(&addr.len)->sin_port));
                fflush(stderr);
            } else {
                int err = socket_errno();
                if (err != EWOULDBLOCK){
                    fprintf(stderr, "aoo_server: couldn't accept client (%d)\n", socket_errno());
                    fflush(stderr);
                }
                break;
            }
        }
    }
    if (fds[udpindex].revents & POLLIN){
        receive_udp();
    }

    for (int i = 0; i < numclients; ++i){
        if (fds[i].revents & POLLIN){
            // receive data from client
            if (!clients_[i].receive_data()){
                clients_[i].close();
                didclose = true;
            }
        }
    }
#endif

    // remove closed clients
    if (didclose){
        auto it = std::remove_if(clients_.begin(), clients_.end(),
                                 [](auto& c){ return !c.valid(); });
        clients_.erase(it, clients_.end());
    }
}

void server::receive_udp(){
    if (udpsocket_ < 0){
        return;
    }
    // read as much data as possible until recv() would block
    while (true){
        char buf[AOO_MAXPACKETSIZE];
        ip_address addr;
        auto result = recvfrom(udpsocket_, buf, sizeof(buf), 0,
                               (struct sockaddr *)&addr.addr, &addr.len);
        if (result > 0){
            try {
                osc::ReceivedPacket packet(buf, result);

                std::function<void(const osc::ReceivedBundle&)> dispatchBundle
                        = [&](const osc::ReceivedBundle& bundle){
                    auto it = bundle.ElementsBegin();
                    while (it != bundle.ElementsEnd()){
                        if (it->IsMessage()){
                            osc::ReceivedMessage msg(*it);
                            handle_udp_message(addr, msg);
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
                    handle_udp_message(addr, msg);
                } else if (packet.IsBundle()){
                    osc::ReceivedBundle bundle(packet);
                    dispatchBundle(bundle);
                } else {
                    // ignore
                }
            } catch (const osc::Exception& e){
                fprintf(stderr, "aoo_server: %s\n", e.what());
                fflush(stderr);
            }
        } else if (result < 0){
            int err = socket_errno();
        #ifdef _WIN32
            if (err == WSAEWOULDBLOCK)
        #else
            if (err == EWOULDBLOCK)
        #endif
            {
            #if 0
                fprintf(stderr, "aoo_server: recv() would block\n");
                fflush(stderr);
            #endif
            }
            else
            {
                // TODO handle error
                fprintf(stderr, "aoo_server: recv() failed (%d)\n", err);
                fflush(stderr);
            }
            return;
        }
    }
}

void server::handle_udp_message(const ip_address& addr,
                                const osc::ReceivedMessage &msg)
{
    if (!strcmp(msg.AddressPattern(),
                AOO_MSG_DOMAIN AOO_MSG_SERVER AOO_MSG_PING))
    {
        // reply with /ping message
        char buf[64];
        osc::OutboundPacketStream reply(buf, sizeof(buf));
        reply << osc::BeginMessage(AOO_MSG_DOMAIN AOO_MSG_CLIENT AOO_MSG_PING)
              << osc::EndMessage;

        int result = ::sendto(udpsocket_, reply.Data(), reply.Size(), 0,
                          (struct sockaddr *)&addr.addr, addr.len);
        if (result < 0){
            int err = socket_errno();
        #ifdef _WIN32
            if (err != WSAEWOULDBLOCK)
        #else
            if (err != EWOULDBLOCK)
        #endif
            {
                // TODO handle error
                fprintf(stderr, "aoo_server: send() failed (%d)\n", err);
                fflush(stderr);
            } else {
                fprintf(stderr, "aoo_server: send() would block\n");
                fflush(stderr);
                // LATER buffer data and send next time
            }
        }
    } else {
        fprintf(stderr, "aoo_server: not an AOO message!\n");
        fflush(stderr);
    }
}

void server::signal(){
#ifdef _WIN32
    SetEvent(waitevent_);
#else
    write(waitpipe_[1], "\n", 1);
#endif
}


/*///////////////////////////// client_endpoint //////////////////////////////////*/

client_endpoint::client_endpoint(server &s, int sock, const ip_address &addr)
    : server_(&s), socket(sock), tcp_addr_(addr)
{
#ifdef _WIN32
    event = WSACreateEvent();
    WSAEventSelect(socket, event, FD_READ | FD_WRITE | FD_CLOSE);
#endif
    sendbuffer_.setup(65536);
    recvbuffer_.setup(65536);
}

client_endpoint::~client_endpoint(){
#ifdef _WIN32
    WSACloseEvent(event);
#endif
    close();
}

void client_endpoint::close(){
    if (socket >= 0){
        socket_close(socket);
        socket = -1;
    }
}

void client_endpoint::set_public_address_udp(const ip_address& addr){
    udp_addr_public_ = addr;
}

void client_endpoint::set_private_address_udp(const ip_address& addr){
    udp_addr_private_ = addr;
}

void client_endpoint::send_message(const char *msg, int32_t size){
    if (sendbuffer_.write_packet((const uint8_t *)msg, size)){
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
                auto res = ::send(socket, (char *)buf + nbytes, total - nbytes, 0);
                if (res >= 0){
                    nbytes += res;
                #if 0
                    fprintf(stderr, "aoo_server: sent %d bytes\n", res);
                    fflush(stderr);
                #endif
                } else {
                    auto err = socket_errno();
                #ifdef _WIN32
                    if (err != WSAEWOULDBLOCK)
                #else
                    if (err != EWOULDBLOCK)
                #endif
                    {
                        // TODO handle error
                        fprintf(stderr, "aoo_server: send() failed (%d)\n", socket_errno());
                        fflush(stderr);
                    } else {
                        // store in pending buffer
                        pending_send_data_.assign(buf + nbytes, buf + total);
                        fprintf(stderr, "aoo_server: send() would block\n");
                        fflush(stderr);
                    }
                    return;
                }
            }
        }
        fprintf(stderr, "aoo_server: sent %s to client\n", msg);
        fflush(stderr);
    } else {
        fprintf(stderr, "aoo_server: couldn't send %s to client\n", msg);
        fflush(stderr);
    }
}

bool client_endpoint::receive_data(){
    // read as much data as possible until recv() would block
    while (true){
        char buffer[AOO_MAXPACKETSIZE];
        auto result = recv(socket, buffer, sizeof(buffer), 0);
        if (result == 0){
            fprintf(stderr, "client_endpoint: connection was closed\n");
            fflush(stderr);
            return false;
        }
        if (result < 0){
            int err = socket_errno();
        #ifdef _WIN32
            if (err == WSAEWOULDBLOCK)
        #else
            if (err == EWOULDBLOCK)
        #endif
            {
            #if 0
                fprintf(stderr, "client_endpoint: recv() would block\n");
                fflush(stderr);
            #endif
                return true;
            }
            else
            {
                // TODO handle error
                fprintf(stderr, "client_endpoint: recv() failed (%d)\n", err);
                fflush(stderr);
                return false;
            }
        }

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
                                handle_message(msg);
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
                        handle_message(msg);
                    } else if (packet.IsBundle()){
                        osc::ReceivedBundle bundle(packet);
                        dispatchBundle(bundle);
                    } else {
                        // ignore
                    }
                } catch (const osc::Exception& e){
                    fprintf(stderr, "aoo_server: %s\n", e.what());
                    fflush(stderr);
                }
            } else {
                break;
            }
        }
    }
    return true;
}

void client_endpoint::handle_message(const osc::ReceivedMessage &msg){
    fprintf(stderr, "aoo_server: got %s message\n", msg.AddressPattern());
    fflush(stderr);
    if (!strcmp(msg.AddressPattern(), AOO_MSG_DOMAIN AOO_MSG_SERVER AOO_MSG_PING)){
        // send /ping reply
        char buf[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream reply(buf, sizeof(buf));
        reply << osc::BeginMessage(AOO_MSG_DOMAIN AOO_MSG_CLIENT AOO_MSG_PING)
              << osc::EndMessage;

        send_message(reply.Data(), reply.Size());
    }
}

} // net
} // aoo
