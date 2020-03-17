#pragma once

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#endif

#include "m_pd.h"

int socket_udp(void);

int socket_close(int socket);

int socket_bind(int socket, int port);

int socket_receive(int socket, char *buf, int size, int nonblocking);

int socket_signal(int socket, int port);

void socket_error_print(const char *label);

// use linked list for persistent memory
typedef struct _endpoint {
    int socket;
    struct sockaddr_storage addr;
    int addrlen;
    struct _endpoint *next;
} t_endpoint;

t_endpoint * endpoint_new(const char *host, int port, int socket);

void endpoint_free(t_endpoint *e);

int endpoint_send(t_endpoint *e, const char *data, int size);

int endpoint_getaddress(const t_endpoint *e, t_atom *hostname, t_atom *port);

int endpoint_match(const t_endpoint *e, const struct sockaddr *sa);
