#pragma once

#include <functional>
#include <vector>
#include <atomic>

#ifndef _WIN32
# include <sys/poll.h>
# include <unistd.h>
# include <netdb.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <errno.h>
#endif

#include "common/net_utils.hpp"

namespace aoo {

class tcp_error : public socket_error {
public:
    using socket_error::socket_error;

    tcp_error(const socket_error& e)
        : socket_error(e) {}
};

class tcp_server
{
public:
    tcp_server() {}
    ~tcp_server() { do_close(); }

    tcp_server(const tcp_server&) = delete;
    tcp_server& operator=(const tcp_server&) = delete;

    using reply_func = std::function<int(const AooByte *data, AooSize size)>;

    // returns client ID
    using accept_handler = std::function<AooId(const aoo::ip_address& address, reply_func)>;

    using receive_handler = std::function<void(AooId client, int errorcode, const AooByte *data,
                                               AooSize size, const ip_address& address)>;

    void start(int port, accept_handler accept, receive_handler receive);
    void start(const std::vector<int>& ports, accept_handler accept, receive_handler receive);
    bool run(double timeout = -1.0);
    bool running() const { return running_.load(std::memory_order_relaxed); }
    void stop();
    void notify();

    int send(AooId client, const AooByte *data, AooSize size);
    bool close(AooId client);
    int client_count() const { return client_count_; }
private:
    struct client {
        ip_address address;
        tcp_socket socket;
        AooId id;
    };

    bool do_run(double timeout);
    void accept_client(size_t listen_index);
    void handle_accept_error(const accept_error& e);
    void receive_from_clients();
    void handle_client_error(const client& c, int code) {
        receive_handler_(c.id, code, nullptr, 0, c.address);
    }
    void close_and_remove_client(int index);
    void do_close();

    std::vector<tcp_socket> listen_sockets_;
    udp_socket event_socket_;
    int last_error_ = 0;
    std::atomic<bool> running_{false};

    std::vector<pollfd> poll_array_;
    size_t event_index() const { return listen_sockets_.size(); }
    size_t client_index() const { return event_index() + 1; }

    accept_handler accept_handler_;
    receive_handler receive_handler_;

    std::vector<client> clients_;
    std::vector<size_t> stale_clients_;
    int32_t client_count_ = 0;
    static const int max_stale_clients = 100;
};

} // aoo
