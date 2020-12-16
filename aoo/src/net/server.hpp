/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_net.hpp"

#include "common/utils.hpp"
#include "common/lockfree.hpp"
#include "common/net_utils.hpp"

#include "commands.hpp"
#include "SLIP.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <memory.h>
#include <unordered_map>
#include <vector>

namespace aoo {
namespace net {

class server;

struct user;
using user_list = std::vector<std::shared_ptr<user>>;

struct group;
using group_list = std::vector<std::shared_ptr<group>>;


class client_endpoint {
public:
    client_endpoint(server &s, int socket, const ip_address& addr);

    ~client_endpoint();

    const ip_address& local_address() const { return addr_; }

    const std::vector<ip_address>& public_addresses() const {
        return public_addresses_;
    }

    bool match(const ip_address& addr) const;

    int socket() const { return socket_; }

    void close();

    bool is_active() const { return socket_ >= 0; }

    void send_message(const char *msg, int32_t);

    bool receive_data();
private:
    server *server_;
    int socket_;
#ifdef _WIN32
    HANDLE event_;
#endif
    std::vector<ip_address> public_addresses_;
    std::shared_ptr<user> user_;
    ip_address addr_;

    SLIP sendbuffer_;
    SLIP recvbuffer_;

    bool handle_message(const char *data, int32_t n);

    bool handle_bundle(const osc::ReceivedBundle& bundle);

    void handle_ping(const osc::ReceivedMessage& msg);

    void handle_login(const osc::ReceivedMessage& msg);

    void handle_group_join(const osc::ReceivedMessage& msg);

    void handle_group_leave(const osc::ReceivedMessage& msg);
};

struct user {
    user(const std::string& _name, const std::string& _pwd,
         int32_t _id, uint32_t _version)
        : name(_name), password(_pwd), id(_id), version(_version) {}

    ~user() { LOG_VERBOSE("removed user " << name); }

    bool is_active() const { return endpoint_ != nullptr; }

    void on_close(server& s);

    bool add_group(std::shared_ptr<group> grp);

    bool remove_group(const group& grp);

    int32_t num_groups() const { return groups_.size(); }

    const group_list& groups() { return groups_; }

    client_endpoint * endpoint() {
        return endpoint_;
    }

    void set_endpoint(client_endpoint *ep){
        endpoint_ = ep;
    }

    // data
    const std::string name;
    const std::string password;
    const int32_t id;
    const uint32_t version;
private:
    group_list groups_;
    client_endpoint *endpoint_ = nullptr;
};

struct group {
    group(const std::string& _name, const std::string& _pwd)
        : name(_name), password(_pwd){}
    ~group() { LOG_VERBOSE("removed group " << name); }

    bool add_user(std::shared_ptr<user> usr);

    bool remove_user(const user& usr);

    int32_t num_users() const { return users_.size(); }

    const user_list& users() { return users_; }

    // data
    const std::string name;
    const std::string password;
private:
    user_list users_;
};

class server final : public iserver {
public:
    enum class error {
        none,
        wrong_password,
        permission_denied,
        access_denied
    };

    static std::string error_to_string(error e);

    struct icommand {
        virtual ~icommand(){}
        virtual void perform(server&) = 0;
    };

    struct ievent {
        virtual ~ievent(){}

        union {
            aoo_event event_;
            aoo_net_error_event error_event_;
            aoo_net_user_event user_event_;
            aoo_net_group_event group_event_;
        };
    };

    server(int tcpsocket, int udpsocket);
    ~server();

    ip_address::ip_type type() const { return type_; }

    aoo_error run() override;

    aoo_error quit() override;

    aoo_error events_available() override;

    aoo_error poll_events(aoo_eventhandler fn, void *user) override;

    std::shared_ptr<user> get_user(const std::string& name,
                                   const std::string& pwd,
                                   uint32_t version, error& e);

    std::shared_ptr<user> find_user(const std::string& name);

    std::shared_ptr<group> get_group(const std::string& name,
                                     const std::string& pwd, error& e);

    std::shared_ptr<group> find_group(const std::string& name);

    void on_user_joined(user& usr);

    void on_user_left(user& usr);

    void on_user_joined_group(user& usr, group& grp);

    void on_user_left_group(user& usr, group& grp);

    void handle_relay_message(const osc::ReceivedMessage& msg,
                              const ip_address& src);
private:
    int tcpsocket_;
    int udpsocket_;
    ip_address::ip_type type_;
    std::vector<std::unique_ptr<client_endpoint>> clients_;
    int32_t next_user_id_ = 0;
    user_list users_;
    group_list groups_;
    // commands
    lockfree::unbounded_mpsc_queue<std::unique_ptr<icommand>> commands_;
    // events
    lockfree::unbounded_mpsc_queue<std::unique_ptr<ievent>> events_;
    void push_event(std::unique_ptr<ievent> e){
        events_.push(std::move(e));
    }
    // signal
    std::atomic<bool> quit_{false};

    bool wait_for_event();

    void update();

    void receive_udp();

    void send_udp_message(const char *msg, int32_t size,
                          const ip_address& addr);

    void handle_udp_message(const osc::ReceivedMessage& msg, int onset,
                            const ip_address& addr);

    bool signal();

    /*/////////////////// events //////////////////////*/

    struct error_event : ievent
    {
        error_event(int32_t type, int32_t code, const char * msg = 0);
        ~error_event();
    };

    struct user_event : ievent
    {
        user_event(int32_t type, const char *name, int32_t id,
                   const ip_address& address);
        ~user_event();
    };

    struct group_event : ievent
    {
        group_event(int32_t type, const char *group,
                    const char *user, int32_t id);
        ~group_event();
    };
};

} // net
} // aoo
