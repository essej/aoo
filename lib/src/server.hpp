/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_net.hpp"
#include "aoo/aoo_utils.hpp"

#include "lockfree.hpp"
#include "net_utils.hpp"
#include "SLIP.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

namespace aoo {
namespace net {

class server;

class client_endpoint {
    server *server_;
public:
    client_endpoint(server &s, int sock, const ip_address& addr);
    ~client_endpoint();

    void close();
    bool valid() const { return socket >= 0; }

    void set_public_address_udp(const ip_address& addr);
    void set_private_address_udp(const ip_address& addr);

    void send_message(const char *msg, int32_t);
    bool receive_data();

    int socket;
#ifdef _WIN32
    HANDLE event;
#endif
private:
    ip_address tcp_addr_;
    ip_address udp_addr_public_;
    ip_address udp_addr_private_;

    SLIP sendbuffer_;
    SLIP recvbuffer_;
    std::vector<uint8_t> pending_send_data_;

    void handle_message(const osc::ReceivedMessage& msg);

    void handle_ping();

    void handle_login(const std::string& name, const std::string& pwd,
                      const std::string& public_ip, int32_t public_port,
                      const std::string& local_ip, int32_t local_port);
};

class server final : public iserver {
public:
    struct icommand {
        virtual ~icommand(){}
        virtual void perform(server&) = 0;
    };

    server(int tcpsocket, int udpsocket);
    ~server();

    int32_t run() override;

    int32_t quit() override;

    int32_t events_available() override;

    int32_t handle_events(aoo_eventhandler fn, void *user) override;
private:
    int tcpsocket_;
    int udpsocket_;
#ifdef _WIN32
    HANDLE tcpevent_;
    HANDLE udpevent_;
#endif
    std::vector<client_endpoint> clients_;
    // queue
    lockfree::queue<std::unique_ptr<icommand>> commands_;
    lockfree::queue<aoo_event> events_;
    // signal
    std::atomic<bool> quit_{false};
#ifdef _WIN32
    HANDLE waitevent_ = 0;
#else
    int waitpipe_[2];
#endif

    void wait_for_event();

    void receive_udp();

    void send_udp_message(const char *msg, int32_t size,
                          const ip_address& addr);

    void handle_udp_message(const osc::ReceivedMessage& msg,
                            const ip_address& addr);

    void signal();
};

} // net
} // aoo
