#pragma once

#include <stdint.h>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#endif

namespace aoo {

/*///////////// IP address ////////*/

class ip_address {
public:
    ip_address();
    ip_address(const struct sockaddr *sa, socklen_t len);
    ip_address(uint32_t ipv4, int port);
    ip_address(const std::string& host, int port);

    ip_address(const ip_address& other);
    ip_address& operator=(const ip_address& other);

    bool operator==(const ip_address& other) const;

    std::string name() const;

    int port() const;

    bool valid() const;

    const struct sockaddr *address() const {
        return (const struct sockaddr *)&address_;
    }
    struct sockaddr *address_ptr() {
        return (struct sockaddr *)&address_;
    }
    socklen_t length() const {
        return length_;
    }
    socklen_t *length_ptr() {
        return &length_;
    }
private:
    struct sockaddr_storage address_;
    socklen_t length_;
};


/*///////////// socket //////////////////*/

int socket_udp(void);

int socket_close(int socket);

int socket_connect(int socket, const ip_address& addr, float timeout);

int socket_bind(int socket, int port);

int socket_sendto(int socket, const char *buf, int size,
                  const ip_address& address);

int socket_receive(int socket, char *buf, int size,
                   ip_address* addr, int32_t timeout);

int socket_setsendbufsize(int socket, int bufsize);

int socket_setrecvbufsize(int socket, int bufsize);

int socket_set_nonblocking(int socket, bool nonblocking);

bool socket_signal(int socket, int port);

int socket_errno();

int socket_strerror(int err, char *buf, int size);

std::string socket_strerror(int err);

void socket_error_print(const char *label);

/*/////////////////// t_endpoint /////////////////*/

struct endpoint {
public:
    endpoint(int socket, const ip_address& address)
        : socket_(socket), address_(address) {}

    int send(const char *data, int size) const {
        return socket_sendto(socket_, data, size, address_);
    }

    const ip_address& address() const {
        return address_;
    }
private:
    int socket_;
    ip_address address_;
};

static int32_t endpoint_send(void *x, const char *data, int32_t size)
{
    return static_cast<endpoint *>(x)->send(data, size);
}

} // aoo
