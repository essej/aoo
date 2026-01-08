/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo_server.hpp"

#include "client_endpoint.hpp"
#include "detail.hpp"
#include "event.hpp"
#include "tcp_server.hpp"
#include "udp_server.hpp"

#include "common/utils.hpp"
#include "common/lockfree.hpp"
#include "common/net_utils.hpp"

#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"

#include <unordered_map>
#include <vector>

// these settings are used to maintain the TCP connection
// to the clients.
#ifndef AOO_SERVER_PING_INTERVAL
# define AOO_SERVER_PING_INTERVAL 5.0
#endif

#ifndef AOO_SERVER_PROBE_TIME
# define AOO_SERVER_PROBE_TIME 10.0
#endif

#ifndef AOO_SERVER_PROBE_INTERVAL
# define AOO_SERVER_PROBE_INTERVAL 1.0
#endif

#ifndef AOO_SERVER_PROBE_COUNT
# define AOO_SERVER_PROBE_COUNT 5
#endif

namespace aoo {
namespace net {

//------------------------- Server -------------------------------//

class Server final : public AooServer {
public:
    Server();

    ~Server();

    AooError AOO_CALL setup(AooServerSettings& settings) override;

    AooError AOO_CALL run(AooSeconds timeout) override;

    AooError AOO_CALL receive(AooSeconds timeout) override;

    AooError AOO_CALL handlePacket(
            const AooByte *data, AooInt32 size,
            const void *address, AooAddrSize addrlen) override;

    AooError AOO_CALL stop() override;

    AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) override;

    AooBool AOO_CALL eventsAvailable() override;

    AooError AOO_CALL pollEvents() override;

    AooError AOO_CALL setRequestHandler(
            AooRequestHandler cb, void *user, AooFlag flags) override;

    AooError AOO_CALL handleRequest(
            AooId client, AooId token, const AooRequest *request,
            AooError result, AooResponse *response) override;

    AooError AOO_CALL notifyClient(
            AooId client, const AooData &data) override;

    AooError AOO_CALL notifyGroup(
            AooId group, AooId user, const AooData &data) override;

    AooError AOO_CALL findGroup(const AooChar *name, AooId *id) override;

    AooError AOO_CALL addGroup(
            const AooChar *name, const AooChar *password, const AooData *metadata,
            const AooIpEndpoint *relayAddress, AooFlag flags, AooId *groupId) override;

    AooError AOO_CALL removeGroup(AooId group) override;

    AooError AOO_CALL findUserInGroup(
            AooId group, const AooChar *userName, AooId *userId) override;

    AooError AOO_CALL addUserToGroup(
            AooId group, const AooChar *userName, const AooChar *userPwd,
            const AooData *metadata, AooFlag flags, AooId *userId) override;

    AooError AOO_CALL removeUserFromGroup(
            AooId group, AooId user) override;

    AooError AOO_CALL groupControl(
            AooId group, AooCtl ctl, AooIntPtr index,
            void *data, AooSize size) override;

    AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) override;

    //-----------------------------------------------------------------//

    client_endpoint * find_client(AooId id);

    client_endpoint * find_client(const user& usr) {
        return find_client(usr.client());
    }

    client_endpoint * find_client(const ip_address& addr);

    group* find_group(AooId id);

    group* find_group(std::string_view name);

    group* add_group(group&& g);

    bool remove_group(AooId id);

    void update_group(group& grp, const AooData& md);

    void update_user(const group& grp, user& usr, const AooData& md);

    void on_user_joined_group(const group& grp, const user& usr,
                              const client_endpoint& client);

    void on_user_left_group(const group& grp, const user& usr);

    void do_remove_user_from_group(group& grp, user& usr);

    osc::OutboundPacketStream start_message(size_t extra_size = 0);

    void handle_message(client_endpoint& client, const osc::ReceivedMessage& msg, int32_t size);
private:
    // UDP
    void handle_udp_packet(const AooByte *data, AooInt32 size,
                           const aoo::ip_address& addr);

    void handle_udp_message(const AooByte *data, AooSize size, int onset,
                            const ip_address& addr);

    void handle_relay(const AooByte *data, AooSize size, const aoo::ip_address& addr);

    void handle_ping(const osc::ReceivedMessage& msg, const ip_address& addr);

    void handle_query(const osc::ReceivedMessage& msg, const ip_address& addr);

    void send_udp(const ip_address& addr, const AooByte *data, AooSize size) {
        udp_sendfn_(data, size, addr);
    }

    // TCP
    AooId accept_client(const aoo::ip_address& addr, aoo::tcp_server::reply_func fn);

    bool remove_client(AooId client, AooError err, std::string_view msg = {});

    void handle_client_data(AooId id, int error, const AooByte *data,
                            AooInt32 size, const aoo::ip_address& addr);

    void handle_ping(client_endpoint& client, const osc::ReceivedMessage& msg);

    void handle_pong(client_endpoint& client, const osc::ReceivedMessage& msg);

    void handle_login(client_endpoint& client, const osc::ReceivedMessage& msg);

    AooError do_login(client_endpoint& client, AooId token,
                      const AooRequestLogin& request,
                      AooResponseLogin& response);

    void handle_group_join(client_endpoint& client, const osc::ReceivedMessage& msg);

    AooError do_group_join(client_endpoint& client, AooId token,
                           const AooRequestGroupJoin& request,
                           AooResponseGroupJoin& response);

    void handle_group_leave(client_endpoint& client, const osc::ReceivedMessage& msg);

    AooError do_group_leave(client_endpoint& client, AooId token,
                            const AooRequestGroupLeave& request,
                            AooResponseGroupLeave& response);

    void handle_group_update(client_endpoint& client, const osc::ReceivedMessage& msg);

    AooError do_group_update(client_endpoint& client, AooId token,
                             const AooRequestGroupUpdate& request,
                             AooResponseGroupUpdate& response);

    void handle_user_update(client_endpoint& client, const osc::ReceivedMessage& msg);

    AooError do_user_update(client_endpoint& client, AooId token,
                            const AooRequestUserUpdate& request,
                            AooResponseUserUpdate& response);

    void handle_custom_request(client_endpoint& client, const osc::ReceivedMessage& msg);

    AooError do_custom_request(client_endpoint& client, AooId token,
                               const AooRequestCustom& request,
                               AooResponseCustom& response);

    AooId get_next_client_id();

    AooId get_next_group_id();

    template<typename Request>
    bool handle_request(const Request& request) {
        return request_handler_ &&
                request_handler_(request_context_, kAooIdInvalid,
                                 kAooIdInvalid, (const AooRequest *)&request);
    }

    template<typename Request>
    bool handle_request(const client_endpoint& client, AooId token, const Request& request) {
        return request_handler_ &&
                request_handler_(request_context_, client.id(), token, (const AooRequest *)&request);
    }

    void send_event(event_ptr event);

    void close();

    //----------------------------------------------------------------//

    // UDP server
    sendfn udp_sendfn_;
    int port_ = 0; // unused
    ip_address::ip_type address_family_ = ip_address::Unspec;
    bool use_ipv4_mapped_ = false;
    // clients
    using client_map = std::unordered_map<AooId, client_endpoint>;
    client_map clients_;
    AooId next_client_id_{0};
    // groups
    using group_map = std::unordered_map<AooId, group>;
    group_map groups_;
    AooId next_group_id_{0};
    // networking
    aoo::udp_server udp_server_;
    aoo::tcp_server tcp_server_;
    std::vector<char> sendbuffer_;
    // message queue
    struct message {
        AooId group;
        AooId user;
        AooDataType type;
        std::vector<AooByte> data;
    };
    using message_queue = lockfree::concurrent_queue<message>;
    message_queue message_queue_;

    void push_message(AooId group, AooId user, const AooData& data);

    void dispatch_message(const message& msg);
    // mutex for protecting the client and group list
    //
    // NB: shared_recursive_mutex only supports recursive shared
    // locks if the top-level lock is exclusive! AFAICT, this
    // should be ok since all API methods that may cause callback
    // invocations (events or requests) take a writer lock.
    //
    // (Yes, I know that recursive locks are evil, but it's
    // the best solution I could come up with that allows
    // API methods to be called safely both from the outside
    // and from within event handlers and request handlers.
    sync::shared_recursive_mutex mutex_;
    // request handler
    AooRequestHandler request_handler_{nullptr};
    void *request_context_{nullptr};
    // event handler
    using event_queue = lockfree::concurrent_queue<event_ptr>;
    event_queue event_queue_;
    AooEventHandler event_handler_ = nullptr;
    void *event_context_ = nullptr;
    AooEventMode event_mode_ = kAooEventModeNone;
    // options
    ip_host global_relay_addr_;
    std::string password_;
    parameter<bool> internal_relay_{AOO_SERVER_INTERNAL_RELAY};
    parameter<bool> group_auto_create_{AOO_GROUP_AUTO_CREATE};
    AooPingSettings ping_settings_ {
        AOO_SERVER_PING_INTERVAL,
        AOO_SERVER_PROBE_TIME,
        AOO_SERVER_PROBE_INTERVAL,
        AOO_SERVER_PROBE_COUNT
    };
    sync::spinlock settings_lock_;

    static int send(void *user, const AooByte *data, AooInt32 size,
                    const void *address, AooAddrSize addrlen, AooFlag) {
        aoo::ip_address addr((const struct sockaddr *)address, addrlen);
        auto& server = static_cast<Server *>(user)->udp_server_;
        try {
            return server.send(addr, data, size);
        } catch (const socket_error& e) {
            socket::set_last_error(e.code());
            return -1;
        }
    }
};

} // net
} // aoo
