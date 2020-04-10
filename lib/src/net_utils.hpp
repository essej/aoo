#pragma once

#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#include <cstring>

namespace aoo {
namespace net {

struct ip_address {
    ip_address(){
        memset(&addr, 0, sizeof(addr));
        len = sizeof(addr);
    }
    ip_address(const ip_address& other){
        memcpy(&addr, &other.addr, other.len);
        len = other.len;
    }
    ip_address& operator=(const ip_address& other){
        memcpy(&addr, &other.addr, other.len);
        len = other.len;
        return *this;
    }

    bool operator==(const ip_address& other) const {
        if (addr.ss_family == other.addr.ss_family){
            return memcmp(&addr, &other.addr, len) == 0;
        } else {
            return false;
        }
    }

    struct sockaddr_storage addr;
    socklen_t len;
};

void socket_close(int sock);

int socket_errno();

int socket_set_nonblocking(int socket, int nonblocking);

int socket_connect(int socket, const ip_address& addr, float timeout);

} // net
} // aoo
