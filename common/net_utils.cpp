/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "net_utils.hpp"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#include <cstdio>
#include <cstring>
#include <algorithm>

#define DEBUG_ADDRINFO 0

namespace aoo {

//------------------------ ip_address ------------------------//

ip_address::ip_address() {
    static_assert(sizeof(data_) == max_length,
                  "wrong max_length value");
#if AOO_USE_IPV6
    static_assert(sizeof(data_) >= sizeof(sockaddr_in6),
                  "ip_address can't hold IPv6 sockaddr");
#endif
    clear();
}

ip_address::ip_address(const AooByte *bytes, AooSize size,
                       port_type port, ip_type type) {
    switch (type) {
#if AOO_USE_IPV6
    case IPv6:
    {
        assert(size == 16);
#if 1
        memset(&addr_in6_, 0, sizeof(addr_in6_));
#endif
        // NB: we don't need to set 'sin6_len' (if present)
        addr_in6_.sin6_family = AF_INET6;
        addr_in6_.sin6_port = htons(port);
        memcpy(&addr_in6_.sin6_addr, bytes, size);

        length_ = sizeof(addr_in6_);

        break;
    }
#endif
    case IPv4:
    {
        assert(size == 4);
#if 1
        memset(&addr_in_, 0, sizeof(addr_in_));
#endif
        // NB: we don't need to set 'sin_len' (if present)
        addr_in_.sin_family = AF_INET;
        addr_in_.sin_port = htons(port);
        memcpy(&addr_in_.sin_addr, bytes, size);

        length_ = sizeof(addr_in_);

        break;
    }
    default:
        clear();
        break;
    }
#if 0
    check();
#endif
}

void ip_address::clear() {
#if 1
    memset(data_, 0, sizeof(data_));
#endif
    addr_.sa_family = AF_UNSPEC;
    length_ = 0;
}

void ip_address::reserve() {
    clear();
    length_ = max_length;
}

void ip_address::check() {
    auto f = addr_.sa_family;
#if AOO_USE_IPV6
    bool ok = (f == AF_INET6 || f == AF_INET || f == AF_UNSPEC);
#else
    bool ok = (f == AF_INET || f == AF_UNSPEC);
#endif
    if (!ok) {
        fprintf(stderr, "bad address family: %d\n", f);
        fflush(stderr);
    }
}

std::vector<ip_address> ip_address::resolve(std::string_view host, port_type port,
                                            ip_type type, bool ipv4mapped){
    std::vector<ip_address> result;

    if (host.empty()) {
    #if DEBUG_ADDRINFO
        fprintf(stderr, "don't resolve empty host\n");
        fflush(stderr);
    #endif
#ifdef _WIN32
        throw resolve_error(WSAEINVAL, socket::strerror(WSAEINVAL));
#else
        throw resolve_error(EINVAL, socket::strerror(EINVAL));
#endif
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    // if we have IPv6 support, only get IPv6 addresses
    // (IPv4 addresses will be mapped to IPv6 addresses)
    switch (type){
#if AOO_USE_IPV6
    case IPv6:
        hints.ai_family = AF_INET6;
        break;
#endif
    case IPv4:
        hints.ai_family = AF_INET;
        break;
    default:
        hints.ai_family = AF_UNSPEC;
        break;
    }

    hints.ai_flags =
#if 0
        // NB: not necessary (and possibly harmful) since we
        // explicitly pass the desired socket type.
        AI_ADDRCONFIG | // check if we have a matching adapter
#endif
        AI_NUMERICSERV | // we use a port number
        AI_PASSIVE;      // listen to any addr if hostname is NULL
#if AOO_USE_IPV6
    if (ipv4mapped) {
    #ifdef AI_V4MAPPED
        hints.ai_flags |= AI_V4MAPPED; // fallback to IPv4-mapped IPv6 addresses
    #endif
    #ifdef AI_ALL
        hints.ai_flags |= AI_ALL; // both IPv6 and IPv4-mapped addresses
    #endif
    }
#endif

    char portstr[10]; // largest port is 65535
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo *ailist = nullptr;
    int err = getaddrinfo(!host.empty() ? host.data() : nullptr,
                          portstr, &hints, &ailist);
    if (err == 0) {
        assert(ailist != nullptr);
    #if DEBUG_ADDRINFO
        fprintf(stderr, "resolved '%s' to:\n", host.empty() ? "any" : host.c_str());
    #endif
        for (auto ai = ailist; ai; ai = ai->ai_next){
        #if DEBUG_ADDRINFO
            fprintf(stderr, "\t%s\n", get_name(ai->ai_addr));
        #endif
            // avoid duplicate entries
            ip_address addr(ai->ai_addr, ai->ai_addrlen);
            if (std::find(result.begin(), result.end(), addr) == result.end()){
                result.push_back(addr);
            }
        }
    #if DEBUG_ADDRINFO
        fflush(stderr);
    #endif
        freeaddrinfo(ailist);
    } else {
    #if defined(_WIN32)
        // MS recommends using WSAGetLastError() instead of gai_strerror()!
        // see https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-gai_strerrora
        auto e = WSAGetLastError();
        throw resolve_error(e, socket::strerror(e));
    #elif defined(ESP_PLATFORM)
        // at the time of writing, ESP32 does not have the gai_strerror() function.
        // LATER we could do a feature check with cmake
        auto e = socket::get_last_error();
        throw resolve_error(e, socket::strerror(e));
    #else
        if (err == EAI_SYSTEM) {
            auto e = errno;
            throw resolve_error(e, ::strerror(e));
        } else {
            // TODO: what should we pass as the error code?
            throw resolve_error(HOST_NOT_FOUND, gai_strerror(err));
        }
    #endif
    }

    return result;
}

ip_address::ip_address(port_type port, ip_type type) {
    // also sets address to zeros ('0.0.0.0' resp. '::')
    memset(data_, 0, sizeof(data_));

    if (type == ip_type::IPv6) {
        // IPv6
#if AOO_USE_IPV6
        addr_in6_.sin6_family = AF_INET6;
        addr_in6_.sin6_port = htons(port);
        length_ = sizeof(addr_in6_);
#else
        clear();
#endif
    } else if (type == ip_type::IPv4) {
        // IPv4
        addr_in_.sin_family = AF_INET;
        addr_in_.sin_port = htons(port);
        length_ = sizeof(addr_in_);
    } else {
        // auto
        struct addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        // AI_PASSIVE: nullptr means "any" address
        hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
        hints.ai_family = AF_UNSPEC;

        char portstr[10]; // largest port is 65535
        snprintf(portstr, sizeof(portstr), "%d", port);

        struct addrinfo *ailist;
        int err = getaddrinfo(nullptr, portstr, &hints, &ailist);
        if (err == 0){
            memcpy(data_, ailist->ai_addr, ailist->ai_addrlen);
            length_ = ailist->ai_addrlen;
            freeaddrinfo(ailist);
        } else {
            // fail
            clear();
        }
    }
#if 0
    check();
#endif
}

ip_address::ip_address(std::string_view ip, port_type port, ip_type type,
                       bool ipv4mapped) {
    if (ip.empty() || port == 0) {
        clear();
        return;
    }

#if 1
    memset(data_, 0, sizeof(data_));
#endif

#if AOO_USE_IPV6
    if (ip.find(':') != std::string::npos) {
        // IPv6 address
        if (type != ip_type::IPv4
                && inet_pton(AF_INET6, ip.data(), &addr_in6_.sin6_addr) > 0) {
            addr_in6_.sin6_family = AF_INET6;
            addr_in6_.sin6_port = htons(port);
            length_ = sizeof(addr_in6_);
        } else {
            clear();
        }
    } else if (type == ip_type::IPv6) {
        // IPv4-mapped address
        in_addr addr;
        if (ipv4mapped && inet_pton(AF_INET, ip.data(), &addr) > 0) {
            addr_in6_.sin6_family = AF_INET6;
            addr_in6_.sin6_port = htons(port);
            auto& b = addr_in6_.sin6_addr.s6_addr;
            b[10] = 0xff;
            b[11] = 0xff;
            memcpy(&b[12], &addr, 4);
            length_ = sizeof(addr_in6_);
        } else {
            clear();
        }
    } else
#endif
    {
        // IPv4 address
        if (type != ip_type::IPv6
                && inet_pton(AF_INET, ip.data(), &addr_in_.sin_addr) > 0) {
            addr_in_.sin_family = AF_INET;
            addr_in_.sin_port = htons(port);
            length_ = sizeof(addr_in_);
        } else {
            clear();
        }
    }

#if 0
    check();
#endif
}

bool ip_address::operator==(const ip_address& other) const {
    if (addr_.sa_family == other.addr_.sa_family) {
        switch (addr_.sa_family){
        case AF_INET:
        {
            auto& a = addr_in_;
            auto& b = other.addr_in_;
            return (a.sin_addr.s_addr == b.sin_addr.s_addr)
                    && (a.sin_port == b.sin_port);
        }
    #if AOO_USE_IPV6
        case AF_INET6:
        {
            auto& a = addr_in6_;
            auto& b = other.addr_in6_;
            return !memcmp(a.sin6_addr.s6_addr, b.sin6_addr.s6_addr,
                           sizeof(struct in6_addr))
                    && (a.sin6_port == b.sin6_port);
        }
    #endif
        default:
            break;
        }
    }
    return false;
}

size_t ip_address::hash() const {
    switch (addr_.sa_family) {
#if AOO_USE_IPV6
    case AF_INET6:
    {
        uint32_t w[4];
        memcpy(w, addr_in6_.sin6_addr.s6_addr, 16);
        size_t state = addr_in6_.sin6_port;
        state = (state << 1) ^ w[0];
        state = (state << 1) ^ w[1];
        state = (state << 1) ^ w[2];
        state = (state << 1) ^ w[3];
        return state;
    }
#endif
    case AF_INET:
    {
        size_t state = addr_in_.sin_port;
        return (state << 1) ^ addr_in_.sin_addr.s_addr;
    }
    default:
        return 0;
    }
}

std::ostream& operator<<(std::ostream& os, const ip_address& addr) {
    switch (addr.address()->sa_family) {
#if AOO_USE_IPV6
    case AF_INET6:
        os << "[" << addr.name() << "]:" << addr.port();
        break;
#endif
    case AF_INET:
        os << addr.name() << ":" << addr.port();
        break;
    case AF_UNSPEC:
        os << "[empty]";
        break;
    default:
        os << "[bad address]";
        break;
    }
    return os;
}

const char * ip_address::get_name(const sockaddr *addr){
#if AOO_USE_IPV6
    thread_local char buf[INET6_ADDRSTRLEN];
#else
    thread_local char buf[INET_ADDRSTRLEN];
#endif
    const void *na;
    auto family = addr->sa_family;
    switch (family){
#if AOO_USE_IPV6
    case AF_INET6:
        na = &reinterpret_cast<const sockaddr_in6 *>(addr)->sin6_addr;
        break;
#endif
    case AF_INET:
        na = &reinterpret_cast<const sockaddr_in *>(addr)->sin_addr;
        break;
    default:
        return "";
    }

    if (inet_ntop(family, na, buf, sizeof(buf)) != nullptr) {
        return buf;
    } else {
        return "";
    }
}

const char* ip_address::name() const {
    if (length_ > 0) {
        return get_name(&addr_);
    } else {
        return "";
    }
}

// for backwards compatibility with IPv4 only servers
const char* ip_address::name_unmapped() const {
    auto str = name();
    // strip leading "::ffff:" for mapped IPv4 addresses
    if (!strncmp(str, "::ffff:", 7) ||
        !strncmp(str, "::FFFF:", 7)) {
        return str + 7;
    } else {
        return str;
    }
}

port_type ip_address::port() const {
    switch (addr_.sa_family){
    case AF_INET:
        return ntohs(addr_in_.sin_port);
#if AOO_USE_IPV6
    case AF_INET6:
        return ntohs(addr_in6_.sin6_port);
#endif
    default:
        return 0;
    }
}

const AooByte* ip_address::address_bytes() const {
    switch (addr_.sa_family){
    case AF_INET:
        return (const AooByte *)&addr_in_.sin_addr;
#if AOO_USE_IPV6
    case AF_INET6:
        return (const AooByte *)&addr_in6_.sin6_addr;
#endif
    default:
        return nullptr;
    }
}

size_t ip_address::address_size() const {
    switch(addr_.sa_family){
    case AF_INET:
        return 4;
#if AOO_USE_IPV6
    case AF_INET6:
        return 16;
#endif
    default:
        return 0;
    }
}

bool ip_address::valid() const {
    return address()->sa_family != AF_UNSPEC;
}

ip_address::ip_type ip_address::type() const {
    switch(addr_.sa_family){
    case AF_INET:
        return IPv4;
#if AOO_USE_IPV6
    case AF_INET6:
        return IPv6;
#endif
    default:
        return Unspec;
    }
}

bool ip_address::is_ipv4_mapped() const {
#if AOO_USE_IPV6
    if (addr_.sa_family == AF_INET6) {
        auto w = (uint16_t *)addr_in6_.sin6_addr.s6_addr;
        return (w[0] == 0) && (w[1] == 0) && (w[2] == 0) && (w[3] == 0) &&
               (w[4] == 0) && (w[5] == 0xffff);
    }
#endif
    return false;
}

ip_address ip_address::ipv4_mapped() const {
#if AOO_USE_IPV6
    if (addr_.sa_family == AF_INET) {
        uint16_t w[8] = { 0, 0, 0, 0, 0, 0xffff };
        memcpy(&w[6], &addr_in_.sin_addr.s_addr, 4);
        return ip_address((const AooByte *)&w, 16, port(), IPv6);
    }
#endif
    return *this;
}

ip_address ip_address::unmapped() const {
#if AOO_USE_IPV6
    if (is_ipv4_mapped()) {
        auto w = (uint16_t *)addr_in6_.sin6_addr.s6_addr;
        return ip_address((const AooByte *)&w[6], 4, port(), IPv4);
    }
#endif
    return *this;
}

void ip_address::unmap() {
#if AOO_USE_IPV6
    if (is_ipv4_mapped()){
        auto w = (uint16_t *)addr_in6_.sin6_addr.s6_addr;
        *this = ip_address((const AooByte *)&w[6], 4, port(), IPv4);
    }
#endif
}

//------------------------ socket ----------------------------//

namespace socket {

int init()
{
    static bool initialized = false;
    if (!initialized)
    {
    #ifdef _WIN32
        short version = MAKEWORD(2, 0);
        WSADATA wsadata;
        if (WSAStartup(version, &wsadata))
            return -1;
    #endif

        initialized = true;
    }
    return 0;
}

int get_last_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

void set_last_error(int err) {
#ifdef _WIN32
    WSASetLastError(err);
#else
    errno = err;
#endif
}

int strerror(int err, char *buf, int size) {
#ifdef _WIN32
    wchar_t wbuf[1024];
    auto wlen = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0,
                               err, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), wbuf,
                               sizeof(wbuf), NULL);
    if (wlen <= 0){
        return -1;
    }
    // convert to unicode
    auto len = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, buf, size, NULL, NULL);
    if (len == 0){
        return -1;
    }
    // remove trailing newlines
    auto ptr = buf + (len - 1);
    while (*ptr == '\n' || *ptr == '\r'){
        *ptr-- = '\0';
        len--;
    }
    // add error number (will also null-terminate the string)
    len += snprintf(buf + len, size - len, " [%d]", err);
    return len;
#else
    return snprintf(buf, size, "%s [%d]", ::strerror(err), err);
#endif
}

std::string strerror(int err) {
    char buf[1024];
    if (strerror(err, buf, 1024) > 0){
        return buf;
    } else {
        return std::string {};
    }
}

void print_error(int err, const char *label) {
    if (!err){
        return;
    }

    char str[1024];
    strerror(err, str, sizeof(str));
    if (label){
        fprintf(stderr, "%s: %s (%d)\n", label, str, err);
    } else {
        fprintf(stderr, "%s (%d)\n", str, err);
    }
    fflush(stderr);

    set_last_error(err); // restore errno!
}

void print_last_error(const char *label) {
    print_error(get_last_error());
}

} // namespace "socket"

namespace {

int set_int_option(socket_type socket, int level, int option, int value) {
    return setsockopt(socket, level, option, (const char *)&value, sizeof(value));
}

int get_int_option(socket_type socket, int level, int option, int& value) {
    // On Windows on there is a horrible bug in getsockopt(): for some boolean options
    // it would only write a single byte, even though the documentation clearly
    // states that the type should be DWORD or BOOL (both are 32-bit integers).
    // (To be fair, 'optlen' will be set to 1, but how will expect this?)
    // As a consequence, the upper 3 bytes of 'optval' will contain garbage.
    // A simple workaround is to initialize it with 0; this works because Windows
    // is always little endian.
    value = 0;
    socklen_t len = sizeof(value);
    return getsockopt(socket, level, option, (char *)&value, &len);
}

std::pair<socket_type, ip_address> create_from_port(int protocol, port_type port) {
#if AOO_USE_IPV6
    // prefer IPv6 (dual stack), but fall back to IPv4 if disabled
    ip_address bindaddr;
    socket_type sock = ::socket(AF_INET6, protocol, 0);
    if (sock != invalid_socket) {
        bindaddr = ip_address(port, ip_address::IPv6);
        // make dual stack socket by listening to both IPv4 and IPv6 packets
        if (set_int_option(sock, IPPROTO_IPV6, IPV6_V6ONLY, false) != 0){
            fprintf(stderr, "socket_udp: couldn't set IPV6_V6ONLY\n");
            fflush(stderr);
            // TODO: fall back to IPv4?
        }
    } else {
        sock = ::socket(AF_INET, protocol, 0);
        bindaddr = ip_address(port, ip_address::IPv4);
    }
#else
    socket_type sock = ::socket(AF_INET, protocol, 0);
    ip_address bindaddr(port, ip_address::IPv4);
#endif
    if (sock == invalid_socket) {
        throw socket_error(socket::get_last_error());
    }
    return std::pair(sock, bindaddr);
}

socket_type create_from_family(int protocol, ip_address::ip_type family, bool dualstack) {
    socket_type sock = invalid_socket;
    if (family == ip_address::IPv6) {
#if AOO_USE_IPV6
        sock = ::socket(AF_INET6, protocol, 0);
        if (sock != invalid_socket && dualstack) {
            // make dual stack socket by listening to both IPv4 and IPv6 packets
            if (set_int_option(sock, IPPROTO_IPV6, IPV6_V6ONLY, false) != 0){
                fprintf(stderr, "socket_udp: couldn't set IPV6_V6ONLY\n");
                fflush(stderr);
                // TODO: fall back to IPv4?
            }
        }
#endif
    } else {
        sock = ::socket(AF_INET, protocol, 0);
    }
    if (sock != invalid_socket) {
        return sock;
    } else {
        throw socket_error(socket::get_last_error());
    }
}

// close the socket on failure
void try_bind(socket_type sock, const ip_address& addr, bool reuse_port) {
    if (reuse_port) {
        if (set_int_option(sock, SOL_SOCKET, SO_REUSEADDR, true) != 0) {
            socket::print_last_error("socket: couldn't set SO_REUSEADDR");
        }
    }
    // finally bind the socket
    if (::bind(sock, addr.address(), addr.length()) != 0) {
        auto err = socket::get_last_error(); // cache errno
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        throw socket_error(err);
    }
}

} // namespace

//------------------- base_socket ---------------------//

ip_address base_socket::address() const {
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (::getsockname(socket_, (sockaddr *)&ss, &len) < 0) {
        throw socket_error(socket::get_last_error());
    } else {
        return ip_address((sockaddr *)&ss, len);
    }
}

ip_address base_socket::peer() const {
    sockaddr_storage ss;
    socklen_t len = sizeof(ss);
    if (::getpeername(socket_, (sockaddr *)&ss, &len) < 0) {
        auto e = socket::get_last_error();
#ifdef _WIN32
        if (e == WSAENOTCONN) {
#else
        if (e == ENOTCONN) {
#endif
            return ip_address{};
        } else {
            throw socket_error(e);
        }
    } else {
        return ip_address((sockaddr *)&ss, len);
    }
}

port_type base_socket::port() const {
    return address().port();
}

ip_address::ip_type base_socket::family() const {
    return address().type();
}

bool base_socket::shutdown(shutdown_method how) noexcept {
    return ::shutdown(socket_, how) != 0;
}

void base_socket::close() noexcept {
    if (socket_ != invalid_socket) {
    #ifdef _WIN32
        closesocket(socket_);
    #else
        ::close(socket_);
    #endif
        socket_ = invalid_socket;
    }
}

void base_socket::bind(const ip_address& addr) {
    // finally bind the socket
    if (::bind(socket_, addr.address(), addr.length()) != 0) {
        throw socket_error(socket::get_last_error());
    }
}

int base_socket::send(const void *buf, int size) {
    auto ret = ::send(socket_, (const char *)buf, size, 0);
    if (ret >= 0) {
        return ret;
    } else {
        throw socket_error(socket::get_last_error());
    }
}

std::pair<bool, int> base_socket::do_receive(void *buf, int size,
                                             ip_address* addr, double timeout) {
    if (timeout >= 0) {
        // non-blocking receive via poll()
        struct pollfd p;
        p.fd = socket_;
        p.revents = 0;
        p.events = POLLIN;
#ifdef _WIN32
        int result = WSAPoll(&p, 1, timeout * 1000);
#else
        int result = poll(&p, 1, timeout * 1000);
#endif
        if (result < 0) {
            // poll() failed
            throw socket_error(socket::get_last_error());
        } else if (result == 0) {
            // timeout
            return { false, 0 };
        }
    }
    int ret = 0;
    if (addr) {
        addr->reserve();
        ret = ::recvfrom(socket_, (char *)buf, size, 0,
                         addr->address_ptr(), addr->length_ptr());
    } else {
        ret = ::recv(socket_, (char *)buf, size, 0);
    }
    if (ret >= 0) {
        return { true, ret };
    } else {
        throw socket_error(socket::get_last_error());
    }
}

#define DEBUG_SOCKET_BUFFER 0

void base_socket::set_send_buffer_size(int bufsize) {
    auto oldsize = send_buffer_size();
#if DEBUG_SOCKET_BUFFER
    fprintf(stderr, "old send buffer size: %d\n", oldsize);
    fflush(stderr);
#endif
    // don't set a smaller buffer size than the default
    if (bufsize < oldsize){
        return;
    }
    int result = set_int_option(socket_, SOL_SOCKET, SO_SNDBUF, bufsize);
    if (result != 0) {
        throw socket_error(socket::get_last_error());
    }
#if DEBUG_SOCKET_BUFFER
    fprintf(stderr, "new send buffer size: %d\n", send_buffer_size());
    fflush(stderr);
#endif
}

int base_socket::send_buffer_size() const {
    int oldsize = 0;
    if (get_int_option(socket_, SOL_SOCKET, SO_SNDBUF, oldsize) == 0) {
        return oldsize;
    } else {
        throw socket_error(socket::get_last_error());
    }
}

void base_socket::set_receive_buffer_size(int bufsize) {
    int oldsize = receive_buffer_size();
#if DEBUG_SOCKET_BUFFER
    fprintf(stderr, "old receive buffer size: %d\n", oldsize);
    fflush(stderr);
#endif
    // don't set a smaller buffer size than the default
    if (bufsize < oldsize){
        return;
    }
#if DEBUG_SOCKET_BUFFER
    fprintf(stderr, "new receive buffer size: %d\n", receive_buffer_size());
    fflush(stderr);
#endif
}

int base_socket::receive_buffer_size() const {
    int oldsize = 0;
    if (get_int_option(socket_, SOL_SOCKET, SO_RCVBUF, oldsize) == 0) {
        return oldsize;
    } else {
        throw socket_error(socket::get_last_error());
    }
}


void base_socket::set_non_blocking(bool b) {
#ifdef _WIN32
    u_long modearg = b;
    if (ioctlsocket(socket_, FIONBIO, &modearg) != NO_ERROR)
        throw socket_error(socket::get_last_error());
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (b)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl(socket_, F_SETFL, flags) < 0)
        throw socket_error(socket::get_last_error());
#endif
}

#ifndef _WIN32
bool base_socket::non_blocking() const {
#ifdef _WIN32
    // TODO: Windows does not let us query the socket flags...
    return false;
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    return flags & O_NONBLOCK;
#endif
}
#endif

// kudos to https://stackoverflow.com/a/46062474/6063908
void base_socket::connect(const ip_address& addr, double timeout) {
    if (timeout < 0) {
        if (::connect(socket_, addr.address(), addr.length()) < 0) {
            throw socket_error(socket::get_last_error());
        }
    } else {
        // set nonblocking and connect
        // NB: non_blocking() doesn't work on Windows, so we just
        // assume that our socket is always blocking.
#if 1
        bool was_blocking = true;
#else
        bool was_blocking = !non_blocking();
#endif
        if (was_blocking) {
            set_non_blocking(true);
        }

        if (::connect(socket_, addr.address(), addr.length()) < 0) {
            int status;
            struct timeval timeoutval;
            fd_set writefds, errfds;
    #ifdef _WIN32
            if (socket::get_last_error() != WSAEWOULDBLOCK) {
    #else
            if (socket::get_last_error() != EINPROGRESS) {
    #endif
                throw socket_error(socket::get_last_error());
            }

            // block with select using timeout
            if (timeout < 0) timeout = 0;
            timeoutval.tv_sec = (int)timeout;
            timeoutval.tv_usec = (timeout - timeoutval.tv_sec) * 1000000;
            FD_ZERO(&writefds);
            FD_SET(socket_, &writefds); // socket is connected when writable
            FD_ZERO(&errfds);
            FD_SET(socket_, &errfds); // catch exceptions

            status = select(socket_ + 1, NULL, &writefds, &errfds, &timeoutval);
            if (status < 0)  {
                // select failed
                throw socket_error(socket::get_last_error());
            } else if (status == 0) {
                // connection timed out
                throw socket_error(socket_error::timeout);
            }

            if (FD_ISSET(socket_, &errfds)) {
                // connection failed
                throw socket_error(error());
            }
        }
        // done, set blocking again
        if (was_blocking) {
            set_non_blocking(false);
        }
    }
}

AooSocketFlags base_socket::flags() const {
    ip_address addr = address();
    if (addr.type() == ip_address::ip_type::IPv6) {
#if AOO_USE_IPV6
        int ipv6only = 0;
        if (get_int_option(socket_, IPPROTO_IPV6, IPV6_V6ONLY, ipv6only) != 0) {
            socket::print_last_error("base_socket::flags: couldn't get IPV6ONLY");
            return 0;
        }
        if (ipv6only) {
            return kAooSocketIPv6;
        } else {
            return kAooSocketDualStack;
        }
#else
        return 0; // shouldn't happen
#endif
    } else {
        return kAooSocketIPv4;
    }
}

int base_socket::error() const {
    int error = 0;
    socklen_t errlen = sizeof(error);
    ::getsockopt(socket_, SOL_SOCKET, SO_ERROR, (char *)&error, &errlen);
    return error;
}

void base_socket::set_reuse_port(bool b) {
    if (set_int_option(socket_, SOL_SOCKET, SO_REUSEADDR, true) != 0) {
        throw socket_error(socket::get_last_error());
    }
}

bool base_socket::reuse_port() const {
    int result = 0;
    if (get_int_option(socket_, SOL_SOCKET, SO_REUSEADDR, result) != 0) {
        throw socket_error(socket::get_last_error());
    }
    return result;
}

//------------------------ udp_socket -------------------------//

udp_socket::udp_socket(family_tag, ip_address::ip_type family, bool dualstack) {
    socket_ = create_from_family(SOCK_DGRAM, family, dualstack);
}

udp_socket::udp_socket(const ip_address& addr, bool reuse_port) {
    auto sock = create_from_family(SOCK_DGRAM, addr.type(), true);
    try_bind(sock, addr, reuse_port);
    socket_ = sock;
}

udp_socket::udp_socket(port_tag, port_type port, bool reuse_port) {
    auto [sock, bindaddr] = create_from_port(SOCK_DGRAM, port);
    try_bind(sock, bindaddr, reuse_port);
    socket_ = sock;
}

int udp_socket::send(const void *buf, int size, const ip_address& addr) {
    auto ret = ::sendto(socket_, (const char *)buf, size, 0,
                        addr.address(), addr.length());
    if (ret >= 0) {
        return ret;
    } else {
        throw socket_error(socket::get_last_error());
    }
}

bool udp_socket::signal() noexcept {
    // wake up blocking recv() by sending an empty packet to itself
    try {
        ip_address addr = address();
        if (addr.type() == ip_address::ip_type::IPv6){
            try {
                send(nullptr, 0, ip_address("::1", addr.port(), addr.type()));
                return true;
            } catch (const socket_error&) {
                // Some containers expose a dual-stack socket without ::1.
                addr = ip_address("127.0.0.1", addr.port(), addr.type());
            }
        } else {
            addr = ip_address("127.0.0.1", addr.port(), addr.type());
        }
        send(nullptr, 0, addr);
        return true;
    } catch (const socket_error& e) {
        socket::print_error(e.code(), "udp_socket: could not signal");
        return false;
    }
}

//------------------------ tcp_socket -------------------------//

namespace {

static void enable_nodelay(socket_type sock) {
    // disable Nagle's algorithm
    if (set_int_option(sock, IPPROTO_TCP, TCP_NODELAY, true) != 0) {
        socket::print_last_error("tcp_socket: could not set TCP_NODELAY");
    }
}

} // namespace

tcp_socket::tcp_socket(family_tag, ip_address::ip_type family, bool dualstack) {
    auto sock = create_from_family(SOCK_STREAM, family, dualstack);
    enable_nodelay(sock);
    socket_ = sock;
}

tcp_socket::tcp_socket(const ip_address& addr, bool reuse_port) {
    auto sock = create_from_family(SOCK_STREAM, addr.type(), true);
    try_bind(sock, addr, reuse_port);
    enable_nodelay(sock);
    socket_ = sock;
}

tcp_socket::tcp_socket(port_tag, port_type port, bool reuse_port) {
    auto [sock, bindaddr] = create_from_port(SOCK_STREAM, port);
    try_bind(sock, bindaddr, reuse_port);
    enable_nodelay(sock);
    socket_ = sock;
}

void tcp_socket::listen(int backlog) {
    if (::listen(socket_, backlog) != 0) {
        throw socket_error(socket::get_last_error());
    }
}

void tcp_socket::set_nodelay(bool b) {
    if (set_int_option(socket_, IPPROTO_TCP, TCP_NODELAY, b) != 0) {
        throw socket_error(socket::get_last_error());
    }
}

bool tcp_socket::nodelay() const {
    int b;
    if (get_int_option(socket_, IPPROTO_TCP, TCP_NODELAY, b) != 0) {
        throw socket_error(socket::get_last_error());
    }
    return b;
}

std::pair<tcp_socket, ip_address> tcp_socket::accept() {
    ip_address addr;
    addr.reserve();
    auto sock = ::accept(socket_, addr.address_ptr(), addr.length_ptr());
    if (sock != invalid_socket) {
#if 1
        // disable Nagle's algorithm
        enable_nodelay(sock);
#endif
        return { tcp_socket(socket_tag{}, sock), addr };
    } else {
        // address might be valid!
        throw accept_error(socket::get_last_error(), addr);
    }
}

} // aoo
