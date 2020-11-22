/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_net.hpp"

#include "common/utils.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/lockfree.hpp"
#include "common/net_utils.hpp"

#include "commands.hpp"
#include "SLIP.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <vector>
#include <functional>

#define AOO_NET_CLIENT_PING_INTERVAL 5000
#define AOO_NET_CLIENT_REQUEST_INTERVAL 100
#define AOO_NET_CLIENT_REQUEST_TIMEOUT 5000

namespace aoo {
namespace net {

class client;

class peer {
public:
    peer(client& client, int32_t id,
         const std::string& group, const std::string& user,
         std::vector<ip_address>&& addrlist);

    ~peer();

    bool connected() const {
        return connected_.load();
    }

    bool match(const ip_address& addr) const;

    bool match(const std::string& group, const std::string& user,
               int32_t id);

    int32_t id() const { return id_; }

    const std::string& group() const { return group_; }

    const std::string& user() const { return user_; }

    const ip_address& address() const {
        return real_address_;
    }

    void send(time_tag now);

    void handle_message(const osc::ReceivedMessage& msg, int onset,
                        const ip_address& addr);

    bool handle_first_message(const osc::ReceivedMessage& msg, int onset,
                              const ip_address& addr);

    friend std::ostream& operator << (std::ostream& os, const peer& p);
private:
    client *client_;
    int32_t id_;
    std::string group_;
    std::string user_;
    std::vector<ip_address> addresses_;
    ip_address real_address_;
    time_tag start_time_;
    double last_pingtime_ = 0;
    std::atomic<bool> connected_{false};
    std::atomic<bool> send_reply_{false};
    bool timeout_ = false;
};

enum class client_state {
    disconnected,
    connecting,
    handshake,
    login,
    connected
};

class udp_client {
public:
    udp_client(client& c, int socket, int port)
        : client_(&c), socket_(socket), port_(port) {}

    int port() const { return port_; }

    void send(time_tag now);

    int32_t handle_message(const char *data, int32_t n, const ip_address& addr);

    void send_message(const char *data, int32_t size, const ip_address& addr);

    void start_handshake(const ip_address& local, std::vector<ip_address>&& remote);
private:
    client *client_;
    int socket_;
    int port_;
    ip_address local_address_;
    std::vector<ip_address> server_addrlist_;
    std::vector<ip_address> public_addrlist_;
    shared_mutex mutex_;

    double last_ping_time_ = 0;
    std::atomic<double> first_ping_time_{0};

    void send_ping();

    void send_server_message(const char *data, int32_t size);

    void handle_server_message(const osc::ReceivedMessage& msg, int onset);

    bool is_server_address(const ip_address& addr);
};

class client final : public iclient
{
public:
    struct icommand {
        virtual ~icommand(){}
        virtual void perform(client&) = 0;
    };

    struct ievent {
        virtual ~ievent(){}

        union {
            aoo_event event_;
            aoo_net_error_event error_event_;
            aoo_net_ping_event ping_event_;
            aoo_net_peer_event peer_event_;
            aoo_net_message_event message_event_;
        };
    };

    client(int udpsocket);
    ~client();

    int32_t run() override;

    int32_t quit() override;

    int32_t send_request(aoo_net_request_type request, void *data,
                         aoo_net_callback callback, void *user) override;

    int32_t send_message(const char *data, int32_t n,
                         const void *addr, int32_t len, int32_t flags) override;

    void do_send_message(const char *data, int32_t size, int32_t flags,
                         const ip_address* vec, int32_t n);

    void perform_send_message(const char *data, int32_t n, int32_t flags);

    void perform_send_message(const char *data, int32_t n,
                              const ip_address& address, int32_t flags);

    void perform_send_message(const char *data, int32_t n,
                              const std::string& group, int32_t flags);

    int32_t handle_message(const char *data, int32_t n,
                           const void *addr, int32_t len) override;

    bool handle_peer_message(const osc::ReceivedMessage& msg, int onset,
                             const ip_address& addr);

    int32_t send() override;

    int32_t events_available() override;

    int32_t poll_events(aoo_eventhandler fn, void *user) override;

    void do_connect(const char *host, int port,
                    const char *name, const char *pwd,
                    aoo_net_callback cb, void *user);

    void perform_connect(const std::string& host, int port,
                         const std::string& name, const std::string& pwd,
                         aoo_net_callback cb, void *user);

    int try_connect(const std::string& host, int port);

    void perform_login(const std::vector<ip_address>& addrlist);

    void perform_timeout();

    void do_disconnect(aoo_net_callback cb, void *user);

    void perform_disconnect(aoo_net_callback cb, void *user);

    void do_join_group(const char *name, const char *pwd,
                       aoo_net_callback cb, void *user);

    void perform_join_group(const std::string& group, const std::string& pwd,
                            aoo_net_callback cb, void *user);

    void do_leave_group(const char *name, aoo_net_callback cb, void *user);

    void perform_leave_group(const std::string& group,
                             aoo_net_callback cb, void *user);

    double ping_interval() const { return ping_interval_.load(std::memory_order_relaxed); }

    double request_interval() const { return request_interval_.load(std::memory_order_relaxed); }

    double request_timeout() const { return request_timeout_.load(std::memory_order_relaxed); }

    void push_event(std::unique_ptr<ievent> e);

    void push_command(std::unique_ptr<icommand>&& cmd);

    void send_udp_message(const char *data, int32_t size, const ip_address& addr);

    ip_address::ip_type type() const { return type_; }

    double elapsed_time_since(time_tag now) const {
        return time_tag::duration(start_time_, now);
    }

    client_state current_state() const { return state_.load(); }
private:
    std::unique_ptr<udp_client> udp_client_;
    int socket_ = -1;
    ip_address::ip_type type_ = ip_address::Unspec;

    // ip_address remote_addr_;
    // ip_address local_addr_;
    // SLIP buffers
    SLIP sendbuffer_;
    SLIP recvbuffer_;
    // event
    std::atomic<bool> quit_{false};
    int eventsocket_;
    // peers
    std::vector<std::shared_ptr<peer>> peers_;
    aoo::shared_mutex peer_lock_;
    // time
    time_tag start_time_;
    double last_ping_time_ = 0;
    // handshake
    std::atomic<client_state> state_{client_state::disconnected};
    std::string username_;
    std::string password_;
    aoo_net_callback callback_;
    void *userdata_;
    // commands
    lockfree::unbounded_mpsc_queue<std::unique_ptr<icommand>> commands_;
    // peer/group messages
    lockfree::unbounded_mpsc_queue<std::unique_ptr<icommand>> messages_;
    // pending request
    using request = std::function<bool(const char *pattern, const osc::ReceivedMessage& msg)>;
    std::vector<request> pending_requests_;
    // events
    lockfree::unbounded_mpsc_queue<std::unique_ptr<ievent>> events_;
    // options
    std::atomic<float> ping_interval_{AOO_NET_CLIENT_PING_INTERVAL * 0.001};
    std::atomic<float> request_interval_{AOO_NET_CLIENT_REQUEST_INTERVAL * 0.001};
    std::atomic<float> request_timeout_{AOO_NET_CLIENT_REQUEST_TIMEOUT * 0.001};

    // methods
    bool wait_for_event(float timeout);

    void receive_data();

    bool signal();

    void send_ping();

    void send_server_message(const char *data, int32_t size);

    void handle_server_bundle(const osc::ReceivedBundle& bundle);

    void handle_server_message(const osc::ReceivedMessage& msg);

    void handle_login(const osc::ReceivedMessage& msg);

    void handle_peer_add(const osc::ReceivedMessage& msg);

    void handle_peer_remove(const osc::ReceivedMessage& msg);

    void on_socket_error(int err);

    void on_exception(const char *what, const osc::Exception& err,
                      const char *pattern = nullptr);

    void close(bool manual = false);

    /*////////////////////// events /////////////////////*/
public:
    struct event : ievent
    {
        event(int32_t type){
            event_.type = type;
        }
    };

    struct error_event : ievent
    {
        error_event(int32_t code, const char *msg);
        ~error_event();
    };

    struct ping_event : ievent
    {
        ping_event(const ip_address& addr, uint64_t tt1,
                   uint64_t tt2, uint64_t tt3);
        ~ping_event();
    };

    struct peer_event : ievent
    {
        peer_event(int32_t type, const ip_address& addr,
                   const char *group, const char *user, int32_t id);
        ~peer_event();
    };

    struct message_event : ievent
    {
        message_event(const char *data, int32_t size,
                      const ip_address& addr);
        ~message_event();
    };

    /*////////////////////// commands ///////////////////*/
    struct message_cmd : icommand {
        message_cmd(const char *data, int32_t size, int32_t flags)
            : data_(data, size), flags_(flags) {}

        void perform(client &obj) override {
            obj.perform_send_message(data_.data(), data_.size(), flags_);
        }
    protected:
        std::string data_;
        int32_t flags_;
    };

    struct peer_message_cmd : message_cmd {
        peer_message_cmd(const char *data, int32_t size,
                         const sockaddr *addr, int32_t len, int32_t flags)
            : message_cmd(data, size, flags), address_(addr, len) {}

        void perform(client &obj) override {
            obj.perform_send_message(data_.data(), data_.size(),
                                     address_, flags_);
        }
    protected:
        ip_address address_;
    };

    struct group_message_cmd : message_cmd {
        group_message_cmd(const char *data, int32_t size,
                         const char *group, int32_t flags)
            : message_cmd(data, size, flags), group_(group) {}

        void perform(client &obj) override {
            obj.perform_send_message(data_.data(), data_.size(),
                                     group_, flags_);
        }
    protected:
        std::string group_;
    };

    struct request_cmd : icommand
    {
        request_cmd(aoo_net_callback cb, void *user)
            : cb_(cb), user_(user){}
    protected:
        aoo_net_callback cb_;
        void *user_;
    };

    struct connect_cmd : request_cmd
    {
        connect_cmd(aoo_net_callback cb, void *user,
                    const std::string &host, int port,
                    const std::string& name, const std::string& pwd)
            : request_cmd(cb, user), host_(host), port_(port),
              name_(name), pwd_(pwd) {}

        void perform(client &obj) override {
            obj.perform_connect(host_, port_, name_, pwd_, cb_, user_);
        }
    private:
        std::string host_;
        int port_;
        std::string name_;
        std::string pwd_;
    };

    struct disconnect_cmd : request_cmd
    {
        disconnect_cmd(aoo_net_callback cb, void *user)
            : request_cmd(cb, user) {}

        void perform(client &obj) override {
            obj.perform_disconnect(cb_, user_);
        }
    };

    struct login_cmd : icommand
    {
        login_cmd(std::vector<ip_address>&& addrlist)
            : addrlist_(std::move(addrlist)) {}

        void perform(client& obj) override {
            obj.perform_login(addrlist_);
        }
    private:
        std::vector<ip_address> addrlist_;
    };

    struct timeout_cmd : icommand
    {
        void perform(client &obj) override {
            obj.perform_timeout();
        }
    };

    struct group_join_cmd : request_cmd
    {
        group_join_cmd(aoo_net_callback cb, void *user,
                       const std::string& group, const std::string& pwd)
            : request_cmd(cb, user), group_(group), password_(pwd){}

        void perform(client &obj) override {
            obj.perform_join_group(group_, password_, cb_, user_);
        }
    private:
        std::string group_;
        std::string password_;
    };

    struct group_leave_cmd : request_cmd
    {
        group_leave_cmd(aoo_net_callback cb, void *user,
                        const std::string& group)
            : request_cmd(cb, user), group_(group){}

        void perform(client &obj) override {
            obj.perform_leave_group(group_, cb_, user_);
        }
    private:
        std::string group_;
    };
};

} // net
} // aoo
