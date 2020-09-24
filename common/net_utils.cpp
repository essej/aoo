/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "net_utils.hpp"

#include "aoo/aoo_types.h"

#ifndef _WIN32
#include <sys/select.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#include <stdio.h>
#include <cstring>

namespace aoo {

/*///////////////////////// ip_address /////////////////////////////*/

ip_address::ip_address(){
    memset(&address_, 0, sizeof(address_));
    length_ = sizeof(address_); // e.g. for recvfrom()
}

ip_address::ip_address(const struct sockaddr *sa, socklen_t len){
    memcpy(&address_, sa, len);
    length_ = len;
}

ip_address::ip_address(uint32_t ipv4, int port){
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(ipv4);
    sa.sin_port = htons(port);
    memcpy(&address_, &sa, sizeof(sa));
    length_ = sizeof(sa);
}

ip_address::ip_address(const std::string& host, int port){
    memset(&address_, 0, sizeof(address_));

    auto he = gethostbyname(host.c_str());
    if (he){
        auto addr = (struct sockaddr_in *)&address_;
        addr->sin_family = AF_INET;
        addr->sin_port = htons(port);
        memcpy(&addr->sin_addr, he->h_addr_list[0], he->h_length);
        length_ = sizeof(sockaddr_in);
    } else {
        length_ = sizeof(address_);
    }
}

ip_address::ip_address(const ip_address& other){
    memcpy(&address_, &other.address_, other.length_);
    length_ = other.length_;
}

ip_address& ip_address::operator=(const ip_address& other){
    memcpy(&address_, &other.address_, other.length_);
    length_ = other.length_;
    return *this;
}

bool ip_address::operator==(const ip_address& other) const {
    if (address_.ss_family == other.address_.ss_family){
    #if 1
        if (address_.ss_family == AF_INET){
            auto a = (const struct sockaddr_in *)&address_;
            auto b = (const struct sockaddr_in *)&other.address_;
            return (a->sin_addr.s_addr == b->sin_addr.s_addr)
                    && (a->sin_port == b->sin_port);
        } else  {
            // IPv6 not supported yet
            return false;
        }
    #else
        // doesn't work reliable on BSDs if sin_len is not set
        return !memcmp(&address_, &other.address_, length_);
    #endif
    } else {
        return false;
    }
}

std::string ip_address::name() const {
    if (address_.ss_family == AF_INET){
        auto sin = (const struct sockaddr_in *)&address_;
        return inet_ntoa(sin->sin_addr);
    } else {
        return std::string {};
    }
}

int ip_address::port() const {
    if (address_.ss_family == AF_INET){
        auto sin = (const struct sockaddr_in *)&address_;
        return ntohs(sin->sin_port);
    } else {
        return -1;
    }
}

bool ip_address::valid() const {
    if (address_.ss_family == AF_INET){
        auto sin = (const struct sockaddr_in *)&address_;
        return sin->sin_addr.s_addr != 0;
    } else {
        return false;
    }
}

/*///////////////////////// socket /////////////////////////////////*/

int socket_errno()
{
#ifdef _WIN32
    int err = WSAGetLastError();
    // UDP only?
    if (err == WSAECONNRESET){
        return 0; // ignore
    }
#else
    int err = errno;
#endif
    return err;
}

int socket_strerror(int err, char *buf, int size)
{
#ifdef _WIN32
    buf[0] = 0;
    return FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0,
                          err, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), buf,
                          size, NULL);
#else
    return snprintf(buf, size, "%s", strerror(err));
#endif
}

std::string socket_strerror(int err){
    char buf[1024];
    if (socket_strerror(err, buf, 1024) > 0){
        return buf;
    } else {
        return std::string {};
    }
}

void socket_error_print(const char *label)
{
    char str[1024];

    int err = socket_errno();
    if (!err){
        return;
    }

    socket_strerror(err, str, sizeof(str));
    if (label){
        fprintf(stderr, "%s: %s (%d)\n", label, str, err);
    } else {
        fprintf(stderr, "%s (%d)\n", str, err);
    }
    fflush(stderr);
}

int socket_udp(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0){
        int val = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&val, sizeof(val))){
            fprintf(stderr, "socket_udp: couldn't set SO_BROADCAST");
            fflush(stderr);
        }
    } else {
        socket_error_print("socket_udp");
    }
    return sock;
}

int socket_bind(int socket, int port)
{
    // bind to 'any' address
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = INADDR_ANY;
    sa.sin_port = htons(port);
    return bind(socket, (const struct sockaddr *)&sa, sizeof(sa));
}

int socket_close(int socket)
{
#ifdef _WIN32
    return closesocket(socket);
#else
    return close(socket);
#endif
}

int socket_sendto(int socket, const char *buf, int size, const ip_address& addr)
{
    return sendto(socket, buf, size, 0, addr.address(), addr.length());
}

int socket_receive(int socket, char *buf, int size,
                   ip_address* addr, int32_t timeout)
{
    if (timeout >= 0){
        // non-blocking receive via poll()
        struct pollfd p;
        p.fd = socket;
        p.revents = 0;
        p.events = POLLIN;
    #ifdef _WIN32
        int result = WSAPoll(&p, 1, timeout / 1000);
    #else
        int result = poll(&p, 1, timeout / 1000);
    #endif
        if (result < 0){
            socket_error_print("poll");
            return -1; // poll failed
        }
        if (!(result > 0 && (p.revents & POLLIN))){
            return 0; // timeout
        }
    }
    if (addr){
        return recvfrom(socket, buf, size, 0,
                        addr->address_ptr(), addr->length_ptr());
    } else {
        return recv(socket, buf, size, 0);
    }
}

int socket_setsendbufsize(int socket, int bufsize)
{
    int val = 0;
    socklen_t len;
    len = sizeof(val);
    getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&val, &len);
#if 0
    fprintf(stderr, "old recvbufsize: %d\n", val);
    fflush(stderr);
#endif
    if (val > bufsize){
        return 0;
    }
    val = bufsize;
    int result = setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&val, sizeof(val));
#if 0
    if (result == 0){
        len = sizeof(val);
        getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (void *)&val, &len);
        fprintf(stderr, "new recvbufsize: %d\n", val);
        fflush(stderr);
    }
#endif
    return result;
}

int socket_setrecvbufsize(int socket, int bufsize)
{
    int val = 0;
    socklen_t len;
    len = sizeof(val);
    getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&val, &len);
#if 0
    fprintf(stderr, "old recvbufsize: %d\n", val);
    fflush(stderr);
#endif
    if (val > bufsize){
        return 0;
    }
    val = bufsize;
    int result = setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&val, sizeof(val));
#if 0
    if (result == 0){
        len = sizeof(val);
        getsockopt(socket, SOL_SOCKET, SO_RCVBUF, (void *)&val, &len);
        fprintf(stderr, "new recvbufsize: %d\n", val);
        fflush(stderr);
    }
#endif
    return result;
}

bool socket_signal(int socket, int port)
{
    // wake up blocking recv() by sending an empty packet
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(0x7f000001); // localhost
    sa.sin_port = htons(port);
    if (sendto(socket, 0, 0, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0){
        socket_error_print("sendto");
        return false;
    } else {
        return true;
    }
}

int socket_set_nonblocking(int socket, bool nonblocking)
{
#ifdef _WIN32
    u_long modearg = nonblocking;
    if (ioctlsocket(socket, FIONBIO, &modearg) != NO_ERROR)
        return -1;
#else
    int sockflags = fcntl(socket, F_GETFL, 0);
    if (nonblocking)
        sockflags |= O_NONBLOCK;
    else
        sockflags &= ~O_NONBLOCK;
    if (fcntl(socket, F_SETFL, sockflags) < 0)
        return -1;
#endif
    return 0;
}

// kudos to https://stackoverflow.com/a/46062474/6063908
int socket_connect(int socket, const ip_address& addr, float timeout)
{
    // set nonblocking and connect
    socket_set_nonblocking(socket, true);

    if (connect(socket, addr.address(), addr.length()) < 0)
    {
        int status;
        struct timeval timeoutval;
        fd_set writefds, errfds;
    #ifdef _WIN32
        if (socket_errno() != WSAEWOULDBLOCK)
    #else
        if (socket_errno() != EINPROGRESS)
    #endif
            return -1; // break on "real" error

        // block with select using timeout
        if (timeout < 0) timeout = 0;
        timeoutval.tv_sec = (int)timeout;
        timeoutval.tv_usec = (timeout - timeoutval.tv_sec) * 1000000;
        FD_ZERO(&writefds);
        FD_SET(socket, &writefds); // socket is connected when writable
        FD_ZERO(&errfds);
        FD_SET(socket, &errfds); // catch exceptions

        status = select(socket+1, NULL, &writefds, &errfds, &timeoutval);
        if (status < 0) // select failed
        {
            fprintf(stderr, "socket_connect: select failed");
            return -1;
        }
        else if (status == 0) // connection timed out
        {
        #ifdef _WIN32
            WSASetLastError(WSAETIMEDOUT);
        #else
            errno = ETIMEDOUT;
        #endif
            return -1;
        }

        if (FD_ISSET(socket, &errfds)) // connection failed
        {
            int err; socklen_t len = sizeof(err);
            getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
        #ifdef _WIN32
            WSASetLastError(err);
        #else
            errno = err;
        #endif
            return -1;
        }
    }
    // done, set blocking again
    socket_set_nonblocking(socket, false);
    return 0;
}

bool socket_getaddr(const char *hostname, int port,
                   struct sockaddr_storage &sa, socklen_t &len)
{
    auto he = gethostbyname(hostname);
    if (he){
        auto addr = (sockaddr_in *)&sa;
        // zero out to make sure that memcmp() works! see socket_match()
        memset(addr, 0, sizeof(*addr));
        addr->sin_family = AF_INET;
        addr->sin_port = htons(port);
        memcpy(&addr->sin_addr, he->h_addr_list[0], he->h_length);
        len = sizeof(sockaddr_in);
        return true;
    } else {
        return false;
    }
}

} // aoo
