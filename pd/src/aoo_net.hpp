/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#endif

#include "m_pd.h"

/*///////////// socket ////////////*/

int socket_udp(void);

int socket_close(int socket);

int socket_bind(int socket, int port);

int socket_sendto(int socket, const char *buf, int size, const struct sockaddr *addr);

int socket_receive(int socket, char *buf, int size,
                   struct sockaddr_storage *sa, socklen_t *len,
                   int32_t timeout);

int socket_setsendbufsize(int socket, int bufsize);

int socket_setrecvbufsize(int socket, int bufsize);

int socket_signal(int socket, int port);

int socket_getaddr(const char *hostname, int port,
                   struct sockaddr_storage *sa, socklen_t *len);

int socket_errno();

int socket_strerror(int err, char *buf, int size);

void socket_error_print(const char *label);

int sockaddr_to_atoms(const struct sockaddr *sa, socklen_t len, t_atom *a);

/*/////////////////// t_endpoint /////////////////*/

struct t_endpoint {
    t_endpoint(void *owner, const struct sockaddr_storage *sa, socklen_t len);

    void *e_owner;
    struct sockaddr_storage e_addr;
    socklen_t e_addrlen;

    int send(const char *data, int size) const;

    bool get_address(t_symbol **hostname, int *port) const;

    bool match(const struct sockaddr_storage *sa) const;

    bool to_atoms(int32_t id, t_atom *argv) const;
};

static int32_t endpoint_send(void *x, const char *data, int32_t size)
{
    return static_cast<t_endpoint *>(x)->send(data, size);
}
