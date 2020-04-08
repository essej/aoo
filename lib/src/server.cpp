/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "server.hpp"

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <cstring>

void socket_close(int sock){
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

int32_t socket_errno(){
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

/*//////////////////// AoO server /////////////////////*/

aoonet_server * aoonet_server_new(int port, int32_t *err) {
    // bind to 'any' address
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(port);

    // create and bind TCP socket
    int tcpsocket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpsocket < 0){
        *err = socket_errno();
        socket_close(tcpsocket);
        return nullptr;
    }
    if (bind(tcpsocket, (sockaddr *)&sa, sizeof(sa)) < 0){
        *err = socket_errno();

        return nullptr;
    }
    // set TCP_NODELAY
    int val = 1;
    setsockopt(tcpsocket, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));

    // create and bind UDP socket
    int udpsocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpsocket < 0){
        *err = socket_errno();
        socket_close(tcpsocket);
        return nullptr;
    }
    if (bind(udpsocket, (sockaddr *)&sa, sizeof(sa)) < 0){
        *err = socket_errno();
        socket_close(tcpsocket);
        socket_close(udpsocket);
        return nullptr;
    }

    return new aoo::net::server(tcpsocket, udpsocket);
}

aoo::net::server::server(int tcpsocket, int udpsocket)
    : tcpsocket_(tcpsocket), udpsocket_(udpsocket){}

void aoonet_server_free(aoonet_server *server){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::net::server *>(server);
}

aoo::net::server::~server() {
    socket_close(tcpsocket_);
    socket_close(udpsocket_);
}

int32_t aoonet_server_run(aoonet_server *server){
    return server->run();
}

int32_t aoo::net::server::run(){
    return 0;
}

int32_t aoonet_server_quit(aoonet_server *server){
    return server->quit();
}

int32_t aoo::net::server::quit(){
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



} // aoo
