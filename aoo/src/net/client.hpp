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
        return address_.load() != nullptr;
    }

    bool match(const ip_address& addr) const;

    bool match(const std::string& group, const std::string& user,
               int32_t id);

    int32_t id() const { return id_; }

    const std::string& group() const { return group_; }

    const std::string& user() const { return user_; }

    const ip_address& address() const {
        auto addr = address_.load();
        if (addr){
            return *addr;
        } else {
            return real_address_;
        }
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
    std::atomic<ip_address *> address_{nullptr};
    time_tag start_time_;
    double last_pingtime_ = 0;
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

class client final : public iclient {
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

    client(void *udpsocket, aoo_sendfn fn, int port);
    ~client();

    int32_t run() override;

    int32_t quit() override;

    int32_t send_request(aoo_net_request_type request, void *data,
                         aoo_net_callback callback, void *user) override;

    int32_t send_message(const char *data, int32_t n,
                         const void *addr, int32_t len, int32_t flags) override;

    void do_send_message(const char *data, int32_t size, int32_t flags,
                         const ip_address * vec, int32_t n);

    void perform_send_message(const char *data, int32_t n, int32_t flags);

    void perform_send_message(const char *data, int32_t n,
                              const ip_address& address, int32_t flags);

    void perform_send_message(const char *data, int32_t n,
                              const std::string& group, int32_t flags);

    int32_t handle_message(const char *data, int32_t n,
                           const void *addr, int32_t len) override;

    int32_t send() override;

    int32_t events_available() override;

    int32_t poll_events(aoo_eventhandler fn, void *user) override;

    void do_connect(const char *host, int port,
                    const char *name, const char *pwd,
                    aoo_net_callback cb, void *user);

    void perform_connect(const std::string& host, int port,
                         aoo_net_callback cb, void *user);

    int try_connect(const std::string& host, int port);

    void perform_login();

    void do_disconnect(aoo_net_callback cb, void *user);

    void perform_disconnect(aoo_net_callback cb, void *user);

    void do_join_group(const char *name, const char *pwd,
                       aoo_net_callback cb, void *user);

    void perform_join_group(const std::string& group, const std::string& pwd,
                            aoo_net_callback cb, void *user);

    void do_leave_group(const char *name, aoo_net_callback cb, void *user);

    void perform_leave_group(const std::string& group,
                             aoo_net_callback cb, void *user);

    double ping_interval() const { return ping_interval_.load(); }

    double request_interval() const { return request_interval_.load(); }

    double request_timeout() const { return request_timeout_.load(); }

    void send_message_udp(const char *data, int32_t size, const ip_address& addr);

    void push_event(std::unique_ptr<ievent> e);
private:
    void *udpsocket_;
    aoo_sendfn sendfn_;
    int udpport_;
    int tcpsocket_ = -1;
    shared_mutex socket_lock_;

    std::vector<ip_address> remote_addr_;
    std::vector<ip_address> public_addr_;
    ip_address local_addr_;

    SLIP sendbuffer_;
    std::vector<uint8_t> pending_send_data_;
    SLIP recvbuffer_;
    shared_mutex clientlock_;
    // peers
    std::vector<std::shared_ptr<peer>> peers_;
    aoo::shared_mutex peer_lock_;
    // user
    std::string username_;
    std::string password_;
    // time
    time_tag start_time_;
    double last_tcp_ping_time_ = 0;
    // handshake
    std::atomic<client_state> state_{client_state::disconnected};
    aoo_net_callback connect_callback_ = nullptr;
    void *connect_userdata_ = nullptr;
    double last_udp_ping_time_ = 0;
    double first_udp_ping_time_ = 0;
    // commands
    lockfree::queue<std::unique_ptr<icommand>> commands_;
    spinlock command_lock_;
    void push_command(std::unique_ptr<icommand>&& cmd){
        _scoped_lock<spinlock> lock(command_lock_);
        if (commands_.write_available()){
            commands_.write(std::move(cmd));
        }
    }
    // peer/group messages
    lockfree::queue<std::unique_ptr<icommand>> messages_;
    // pending request
    using request = std::function<bool(const char *pattern, const osc::ReceivedMessage& msg)>;
    std::vector<request> pending_requests_;
    // events
    lockfree::queue<std::unique_ptr<ievent>> events_;
    spinlock event_lock_;
    // signal
    std::atomic<bool> quit_{false};
#ifdef _WIN32
    HANDLE waitevent_ = 0;
    HANDLE sockevent_ = 0;
#else
    int waitpipe_[2];
#endif
    // options
    std::atomic<float> ping_interval_{AOO_NET_CLIENT_PING_INTERVAL * 0.001};
    std::atomic<float> request_interval_{AOO_NET_CLIENT_REQUEST_INTERVAL * 0.001};
    std::atomic<float> request_timeout_{AOO_NET_CLIENT_REQUEST_TIMEOUT * 0.001};

    void send_ping_tcp();

    void send_ping_udp();

    void wait_for_event(float timeout);

    void receive_data();

    void send_server_message_tcp(const char *data, int32_t size);

    void send_server_message_udp(const char *data, int32_t size);

    void handle_server_message_tcp(const osc::ReceivedMessage& msg);

    void handle_server_bundle_tcp(const osc::ReceivedBundle& bundle);

    void handle_server_message_udp(const osc::ReceivedMessage& msg, int onset);

    void handle_login(const osc::ReceivedMessage& msg);

    void handle_peer_add(const osc::ReceivedMessage& msg);

    void handle_peer_remove(const osc::ReceivedMessage& msg);

    void signal();

    void close(bool manual = false);

    void on_socket_error(int err);

    void on_exception(const char *what, const osc::Exception& err,
                      const char *pattern = nullptr);

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
private:
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
                    const std::string &host, int port)
            : request_cmd(cb, user), host_(host), port_(port){}

        void perform(client &obj) override {
            obj.perform_connect(host_, port_, cb_, user_);
        }
    private:
        std::string host_;
        int port_;
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
        void perform(client& obj) override {
            obj.perform_login();
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
