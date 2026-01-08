#pragma once

#include <functional>
#include <atomic>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/poll.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#endif

#include "common/net_utils.hpp"
#include "common/lockfree.hpp"
#include "common/sync.hpp"

namespace aoo {

class udp_error : public socket_error {
public:
    using socket_error::socket_error;

    udp_error(const socket_error& e)
        : socket_error(e) {}
};

// TODO: make 'threaded' a compile-time option
class udp_server
{
public:
    static const size_t max_udp_packet_size = 65536;

    using receive_handler = std::function<void(const AooByte *data, AooSize size,
                                               const aoo::ip_address& addr)>;

    udp_server() : buffer_(max_udp_packet_size) {}
    ~udp_server();

    int port() const { return bind_addr_.port(); }
    const udp_socket& socket() const { return socket_; }
    aoo::ip_address::ip_type type() const { return bind_addr_.type(); }

    udp_server(const udp_server&) = delete;
    udp_server& operator=(const udp_server&) = delete;

    // call before start()
    void set_send_buffer_size(int size) {
        send_buffer_size_ = size;
    }
    void set_receive_buffer_size(int size) {
        receive_buffer_size_ = size;
    }

    void start(int port, receive_handler receive, bool threaded = false);
    bool run(double timeout = -1);
    bool running() const { return running_.load(std::memory_order_relaxed); }
    void stop();
    void notify();

    int send(const aoo::ip_address& addr, const AooByte *data, AooSize size) {
        return socket_.send(data, size, addr);
    }
private:
    bool receive(double timeout);
    void do_close();

    udp_socket socket_;
    aoo::ip_address bind_addr_;
    int send_buffer_size_ = 0;
    int receive_buffer_size_ = 0;
    std::atomic<bool> running_{false};
    bool threaded_ = false;

    std::vector<AooByte> buffer_;

    struct udp_packet {
        std::vector<AooByte> data;
        ip_address address;
    };
    using packet_queue = lockfree::concurrent_queue<udp_packet, false>;
    packet_queue packet_queue_;

    std::thread thread_;
    aoo::sync::event event_;

    receive_handler receive_handler_;
};

} // aoo
