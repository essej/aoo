/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_net.hpp"
#include "lockfree.hpp"
#include "common.hpp"
#include "net_utils.hpp"
#include "SLIP.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#define AOO_NET_CLIENT_PING_INTERVAL 1000

namespace aoo {
namespace net {

class client final : public iclient {
public:
    struct icommand {
        virtual void perform(client&) = 0;
    };

    client(void *udpsocket, aoo_sendfn fn);
    ~client();

    int32_t run() override;

    int32_t quit() override;

    int32_t connect(const char *host, int port,
                    const char *username, const char *pwd) override;

    int32_t disconnect() override;

    int32_t group_join(const char *group, const char *pwd) override;

    int32_t group_leave(const char *group) override;

    int32_t handle_message(const char *data, int32_t n, void *addr) override;

    int32_t send() override;

    int32_t events_available() override;

    int32_t handle_events(aoo_eventhandler fn, void *user) override;

    void do_connect(const std::string& host, int port,
                    const std::string& user, const std::string& pwd);

    void do_disconnect();

    void do_group_join(const std::string& group, const std::string& pwd);

    void do_group_leave(const std::string& group);
private:
    void *udpsocket_;
    aoo_sendfn sendfn_;
    int tcpsocket_ = -1;
    int remote_port_ = 0;
    ip_address remote_addr_;
    ip_address local_addr_;
    SLIP sendbuffer_;
    std::vector<uint8_t> pending_send_data_;
    SLIP recvbuffer_;
    // time
    time_tag start_time_;
    double elapsed_time_ = 0;
    double last_ping_time_ = 0;
    std::atomic<float> ping_interval_{AOO_NET_CLIENT_PING_INTERVAL * 0.001};
    // queue
    lockfree::queue<std::unique_ptr<icommand>> commands_;
    lockfree::queue<aoo_event> events_;
    // signal
    std::atomic<bool> quit_{false};
#ifdef _WIN32
    HANDLE waitevent_ = 0;
    HANDLE sockevent_ = 0;
#else
    int waitpipe_[2];
#endif

    void send_ping();

    void wait_for_event(float timeout);

    void receive_data();

    void send_server_message(const char *data, int32_t size);

    void handle_server_message(const osc::ReceivedMessage& msg);

    void signal();
};

struct connect_cmd : client::icommand
{
    void perform(client &obj) override {
        obj.do_connect(host, port, user, password);
    }
    std::string host;
    int port;
    std::string user;
    std::string password;
};

struct disconnect_cmd : client::icommand
{
    void perform(client &obj) override {
        obj.do_disconnect();
    }
};

struct group_join_cmd : client::icommand
{
    void perform(client &obj) override {
        obj.do_group_join(group, password);
    }
    std::string group;
    std::string password;
};

struct group_leave_cmd : client::icommand
{
    void perform(client &obj) override {
        obj.do_group_leave(group);
    }
    std::string group;
};

} // net
} // aoo
