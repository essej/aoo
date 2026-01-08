/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo_client.hpp"

#include "osc_stream_receiver.hpp"
#include "udp_server.hpp"

#include "common/lockfree.hpp"
#include "common/net_utils.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "detail.hpp"
#include "event.hpp"
#include "peer.hpp"
#include "ping_timer.hpp"
#if AOO_CLIENT_SIMULATE
# include "simulate.hpp"
#endif

#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"

// these settings are used to maintain th TCP connection
// to the AOO server.
#ifndef AOO_CLIENT_SERVER_PING_INTERVAL
# define AOO_CLIENT_SERVER_PING_INTERVAL 5.0
#endif

#ifndef AOO_CLIENT_SERVER_PROBE_TIME
# define AOO_CLIENT_SERVER_PROBE_TIME 10.0
#endif

#ifndef AOO_CLIENT_SERVER_PROBE_INTERVAL
# define AOO_CLIENT_SERVER_PROBE_INTERVAL 1.0
#endif

#ifndef AOO_CLIENT_SERVER_PROBE_COUNT
# define AOO_CLIENT_SERVER_PROBE_COUNT 5
#endif

// these settings are used to establish and maintain
// the UDP connection between peers.
#ifndef AOO_CLIENT_PEER_PING_INTERVAL
# define AOO_CLIENT_PEER_PING_INTERVAL 5.0
#endif

#ifndef AOO_CLIENT_PEER_PROBE_TIME
# define AOO_CLIENT_PEER_PROBE_TIME 10.0
#endif

#ifndef AOO_CLIENT_PEER_PROBE_INTERVAL
# define AOO_CLIENT_PEER_PROBE_INTERVAL 0.25
#endif

#ifndef AOO_CLIENT_PEER_PROBE_COUNT
# define AOO_CLIENT_PEER_PROBE_COUNT 20
#endif

// UDP server ping interval (for maintaining the NAT mapping)
#ifndef AOO_CLIENT_PING_INTERVAL
# define AOO_CLIENT_PING_INTERVAL 5.0
#endif

// UDP server query interval (for obtaining the public IP address)
#ifndef AOO_CLIENT_QUERY_INTERVAL
 #define AOO_CLIENT_QUERY_INTERVAL 0.25
#endif

// Default UDP server query timeout.
// Can be overridden by the 'timeout' member in AooClientConnect.
#ifndef AOO_CLIENT_QUERY_TIMEOUT
# define AOO_CLIENT_QUERY_TIMEOUT 5.0
#endif

#ifndef AOO_CLIENT_SIMULATE
# define AOO_CLIENT_SIMULATE 0
#endif

#include <vector>
#include <unordered_map>

struct AooSource;
struct AooSink;

namespace aoo {
namespace net {

class Client;

//---------------------------- message ---------------------------------//

// peer/group messages
struct message {
    message() = default;

    message(AooId group, AooId user, time_tag tt, const AooData& data, bool reliable)
        : group_(group), user_(user), tt_(tt), data_(&data), reliable_(reliable) {}
    // data
    AooId group_ = kAooIdInvalid;
    AooId user_ = kAooIdInvalid;
    time_tag tt_ = time_tag::immediate();
    metadata data_;
    bool reliable_ = false;
};

//---------------------------- udp_client ---------------------------------//

class udp_client {
public:
    AooError setup(Client& client, AooClientSettings& settings);

    AooError receive(double timeout);

    void stop() {
        udp_server_.stop();
    }

    int port() const { return port_; }

    ip_address::ip_type address_family() const { return address_family_; }

    bool use_ipv4_mapped() const { return use_ipv4_mapped_; }

    AooError handle_osc_message(Client& client, const AooByte *data, int32_t n,
                                const ip_address& addr, int32_t type, AooMsgType onset);

    AooError handle_bin_message(Client& client, const AooByte *data, int32_t n,
                                const ip_address& addr, int32_t type, AooMsgType onset);

    void update(Client& client, const sendfn& fn, time_tag now);

    void start_handshake(const ip_address& remote, AooSeconds timeout);

    void queue_message(message&& msg);

    static int send(void *user, const AooByte *data, AooInt32 size,
                    const void *address, AooAddrSize addrlen, AooFlag) {
        aoo::ip_address addr((const struct sockaddr *)address, addrlen);
        auto& server = static_cast<udp_client *>(user)->udp_server_;
        try {
            return server.send(addr, data, size);
        } catch (const socket_error& e) {
            socket::set_last_error(e.code());
            return -1;
        }
    }
private:
    void send_server_message(const osc::OutboundPacketStream& msg, const sendfn& fn);

    void handle_server_message(Client& client, const osc::ReceivedMessage& msg, int onset);

    void handle_query(Client& client, const osc::ReceivedMessage& msg);

    bool is_server_address(const ip_address& addr);

    using unique_lock = sync::unique_lock<sync::shared_spinlock>;
    using shared_lock = sync::shared_lock<sync::shared_spinlock>;
    using scoped_lock = sync::scoped_lock<sync::shared_spinlock>;
    using scoped_shared_lock = sync::scoped_shared_lock<sync::shared_spinlock>;

    constexpr static float max_query_timeout = 30.0;

    udp_server udp_server_;
    int port_ = 0;
#if 0
    AooSocketFlags socket_flags_ = 0;
#endif
    ip_address::ip_type address_family_ = ip_address::Unspec;
    bool use_ipv4_mapped_ = false;
    sync::shared_spinlock addr_lock_; // LATER replace with seqlock?
    std::atomic<bool> start_handshake_{false};
    bool got_address_ = false;
    ip_address remote_addr_;

    aoo::time_tag next_ping_time_;
    aoo::time_tag query_deadline_;
    std::atomic<float> query_timeout_{max_query_timeout};

    using message_queue = lockfree::concurrent_queue<message>;
    message_queue messages_;
};

//------------------------- Client ----------------------------//

enum class client_state {
    disconnected,
    handshake,
    connecting,
    connected
};

class Client final : public AooClient
{
public:
    struct icommand {
        virtual ~icommand(){}
        virtual void perform(Client&) = 0;
    };

    using command_ptr = std::unique_ptr<icommand>;

    // pending request
    struct callback_cmd : icommand
    {
        callback_cmd(AooResponseHandler cb, void *user)
            : cb_(cb), user_(user) {}

        virtual void handle_response(Client& client, const osc::ReceivedMessage& msg) = 0;

        void reply(const AooResponse& response) const {
            do_reply(kAooErrorNone, response);
        }

        void reply_error(AooError result, int32_t code = 0, const char *msg = "") const {
            AooResponseError response;
            response.type = kAooRequestError;
            response.errorCode = code;
            response.errorMessage = msg;
            do_reply(result, reinterpret_cast<AooResponse&>(response));
        }
    protected:
        virtual void do_reply(AooError result, const AooResponse& response) const = 0;

        void callback(const AooRequest& request, AooError result, const AooResponse& response) const {
            if (cb_) {
                cb_(user_, &request, result, &response);
            }
        }
    private:
        AooResponseHandler cb_;
        void *user_;
    };

    //----------------------------------------------------------//

    Client();

    ~Client();

    AooError AOO_CALL setup(AooClientSettings& settings) override;

    AooError AOO_CALL run(AooSeconds timeout) override;

    AooError AOO_CALL stop() override;

    AooError AOO_CALL send(AooSeconds timeout) override;

    AooError AOO_CALL receive(AooSeconds timeout) override;

    AooError AOO_CALL notify() override;

    AooError AOO_CALL handlePacket(
            const AooByte *data, AooInt32 n,
            const void *addr, AooAddrSize len) override;

    AooError AOO_CALL sendPacket(
        const AooByte *data, AooInt32 n,
        const void *addr, AooAddrSize len) override;

    AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) override;

    AooBool AOO_CALL eventsAvailable() override;

    AooError AOO_CALL pollEvents() override;

    AooError AOO_CALL addSource(AooSource *src) override;

    AooError AOO_CALL removeSource(AooSource *src) override;

    AooError AOO_CALL removeAllSources() override;

    AooError AOO_CALL addSink(AooSink *sink) override;

    AooError AOO_CALL removeSink(AooSink *sink) override;

    AooError AOO_CALL removeAllSinks() override;

    AooError AOO_CALL connect(const AooClientConnect& args,
            AooResponseHandler cb, void *context) override;

    AooError AOO_CALL disconnect(AooResponseHandler cb, void *context) override;

    AooError AOO_CALL joinGroup(const AooClientJoinGroup& args,
            AooResponseHandler cb, void *context) override;

    AooError AOO_CALL leaveGroup(AooId group, AooResponseHandler cb, void *context) override;

    AooError AOO_CALL updateGroup(AooId groupId, const AooData& groupMetadata,
                                  AooResponseHandler cb, void *context) override;

    AooError AOO_CALL updateUser(AooId groupId, const AooData& userMetadata,
                                 AooResponseHandler cb, void *context) override;

    AooError AOO_CALL customRequest(const AooData& data, AooFlag flags,
                                    AooResponseHandler cb, void *context) override;

    AooError AOO_CALL findGroupByName(const AooChar *name, AooId *id) override;

    AooError AOO_CALL getGroupName(AooId group, AooChar *buffer, AooSize *size) override;

    AooError AOO_CALL findPeerByName(
            const AooChar *group, const AooChar *user, AooId *groupId,
            AooId *userId, void *address, AooAddrSize *addrlen) override;

    AooError AOO_CALL findPeerByAddress(const void *address, AooAddrSize addrlen,
                                        AooId *groupId, AooId *userId) override;

    AooError AOO_CALL getPeerName(AooId group, AooId user,
                                  AooChar *groupNameBuffer, AooSize *groupNameSize,
                                  AooChar *userNameBuffer, AooSize *userNameSize) override;

    AooError AOO_CALL sendMessage(AooId group, AooId user, const AooData& msg,
                                  AooNtpTime timeStamp, AooFlag flags) override;

    AooError AOO_CALL sendRequest(const AooRequest& request,
                                  AooResponseHandler callback, void *user) override;

    AooError AOO_CALL control(
            AooCtl ctl, intptr_t index, void *ptr, size_t size) override;

    //---------------------------------------------------------------------//

    bool handle_peer_osc_message(const osc::ReceivedMessage& msg, int onset,
                                 const ip_address& addr);

    bool handle_peer_bin_message(const AooByte *data, AooSize size, int onset,
                                 const ip_address& addr);

    struct connect_cmd;
    void perform(const connect_cmd& cmd);

    void do_connect(const ip_host& server, AooSeconds timeout);

    struct login_cmd;
    void perform(const login_cmd& cmd);

    struct timeout_cmd;
    void perform(const timeout_cmd& cmd);

    struct disconnect_cmd;
    void perform(const disconnect_cmd& cmd);

    struct group_join_cmd;
    void perform(const group_join_cmd& cmd);

    void handle_response(const group_join_cmd& cmd, const osc::ReceivedMessage& msg);

    struct group_leave_cmd;
    void perform(const group_leave_cmd& cmd);

    void handle_response(const group_leave_cmd& cmd, const osc::ReceivedMessage& msg);

    struct group_update_cmd;
    void perform(const group_update_cmd& cmd);

    void handle_response(const group_update_cmd& cmd, const osc::ReceivedMessage& msg);

    struct user_update_cmd;
    void perform(const user_update_cmd& cmd);

    void handle_response(const user_update_cmd& cmd, const osc::ReceivedMessage& msg);

    struct custom_request_cmd;
    void perform(const custom_request_cmd& cmd);

    void handle_response(const custom_request_cmd& cmd, const osc::ReceivedMessage& msg);

    void perform(const message& msg, const sendfn& fn);
    // interval between UDP server pings to maintain the NAT mapping.
    // TODO: make a control?
    double ping_interval() const { return AOO_CLIENT_PEER_PING_INTERVAL; }
    // interval between UDP server queries to obtain public IP address.
    // TODO: make a control?
    double query_interval() const { return AOO_CLIENT_QUERY_INTERVAL; }

    int32_t packet_size() const { return packet_size_.load(); }

    bool binary() const { return binary_.load(); }

    void send_event(event_ptr e);

    void push_command(command_ptr cmd);

    client_state current_state() const { return state_.load(); }
private:
    // networking
    tcp_socket tcp_socket_;
    udp_client udp_client_;
    udp_socket event_socket_;
    std::atomic<bool> quit_{false};
    sendfn udp_sendfn_;
    AooReceiveFunc message_handler_ = nullptr;
    void *user_data_ = nullptr;
    osc_stream_receiver receiver_;
    ip_address local_ipv4_addr_;
#if AOO_USE_IPV6
    ip_address global_ipv6_addr_;
#endif
    std::vector<std::string> interfaces_;
    sync::mutex interface_mutex_; // TODO: replace with seqlock?
    std::vector<char> sendbuffer_;
    aoo::sync::event send_event_;
    // dependants
    struct source_desc {
        AooSource *source;
        AooId id;
    };
    aoo::vector<source_desc> sources_;
    struct sink_desc {
        AooSink *sink;
        AooId id;
    };
    aoo::vector<sink_desc> sinks_;
    sync::shared_mutex source_sink_mutex_;
    // peers
    using peer_list = lockfree::concurrent_list<peer>;
    using peer_lock = std::unique_lock<peer_list>;
    peer_list peers_;
    // connect/login
    std::atomic<client_state> state_{client_state::disconnected};
    std::unique_ptr<connect_cmd> connection_;
    struct group_membership {
        std::string group_name;
        std::string user_name;
        AooId group_id;
        AooId user_id;
        ip_address_list relay_list;
    };
    std::vector<group_membership> groups_;
    sync::mutex group_mutex_;
    ping_timer server_ping_timer_;
    // commands
    using command_queue = lockfree::concurrent_queue<command_ptr>;
    command_queue commands_;
    // pending requests
    using callback_cmd_ptr = std::unique_ptr<callback_cmd>;
    using request_map = std::unordered_map<AooId, callback_cmd_ptr>;
    request_map pending_requests_;
    AooId next_token_ = 0;
    // events
    using event_queue = lockfree::concurrent_queue<event_ptr>;
    event_queue event_queue_;
    AooEventHandler event_handler_ = nullptr;
    void *event_context_ = nullptr;
    AooEventMode event_mode_ = kAooEventModeNone;
    // options
    AooPingSettings server_ping_settings_ {
        AOO_CLIENT_SERVER_PING_INTERVAL,
        AOO_CLIENT_SERVER_PROBE_TIME,
        AOO_CLIENT_SERVER_PROBE_INTERVAL,
        AOO_CLIENT_SERVER_PROBE_COUNT
    };
    sync::spinlock server_settings_lock_; // LATER use seqlock?
    AooPingSettings peer_ping_settings_ {
        AOO_CLIENT_PEER_PING_INTERVAL,
        AOO_CLIENT_PEER_PROBE_TIME,
        AOO_CLIENT_PEER_PROBE_INTERVAL,
        AOO_CLIENT_PEER_PROBE_COUNT
    };
    sync::spinlock peer_settings_lock_; // LATER use seqlock?
    parameter<int32_t> packet_size_{AOO_PACKET_SIZE};
    parameter<bool> binary_{AOO_BINARY_FORMAT};
#if AOO_CLIENT_SIMULATE
    network_simulator simulate_;
#endif

    // methods
    bool wait_for_event(double timeout);

    void receive_data();

    bool signal();

    osc::OutboundPacketStream start_server_message(size_t extra = 0);

    void send_server_message(const osc::OutboundPacketStream& msg);

    void handle_server_message(const osc::ReceivedMessage& msg, int32_t n);

    void handle_login(const osc::ReceivedMessage& msg);

    void handle_server_notification(const osc::ReceivedMessage& msg);

    void handle_group_eject(const osc::ReceivedMessage& msg);

    void handle_group_changed(const osc::ReceivedMessage& msg);

    void handle_user_changed(const osc::ReceivedMessage& msg);

    void handle_peer_changed(const osc::ReceivedMessage& msg);

    void handle_peer_join(const osc::ReceivedMessage& msg);

    void handle_peer_leave(const osc::ReceivedMessage& msg);

    void handle_ping(const osc::ReceivedMessage& msg);

    void handle_pong(const osc::ReceivedMessage& msg);

    void close_with_error(int err);

    void close();

    group_membership * find_group_membership(std::string_view name);

    group_membership * find_group_membership(AooId id);

public:
    //---------------------- commands ------------------------//

    struct connect_cmd : callback_cmd
    {
        // NB: pwd may be NULL!
        connect_cmd(const AooClientConnect& args, AooResponseHandler cb, void *user)
            : callback_cmd(cb, user),
              host_(args.hostName, args.port), pwd_(args.password ? args.password : ""),
              metadata_(args.metadata) {
            // 0: use default
            timeout_ = args.timeout != 0.0 ? args.timeout : AOO_CLIENT_QUERY_TIMEOUT;
        }

        void perform(Client& obj) override {
            obj.perform(*this);
        }

        void handle_response(Client &client, const osc::ReceivedMessage &msg) override {
            // dummy
        }

        void do_reply(AooError result, const AooResponse& response) const override {
            AooData md { metadata_.type(), metadata_.data(), metadata_.size() };

            AooRequestConnect request {
                AOO_REQUEST_INIT(Connect, metadata),
                { host_.name.c_str(), (AooUInt16)host_.port },
                (!pwd_.empty() ? pwd_.c_str() : nullptr),
                (md.size > 0 ? &md : nullptr)
            };

            callback((AooRequest&)request, result, response);
        }
        ip_host host_;
        std::string pwd_;
        aoo::metadata metadata_;
        AooSeconds timeout_;
    };

    struct disconnect_cmd : callback_cmd
    {
        disconnect_cmd(AooResponseHandler cb, void *user)
            : callback_cmd(cb, user) {}

        void perform(Client& obj) override {
            obj.perform(*this);
        }

        void handle_response(Client &client, const osc::ReceivedMessage &msg) override {
            // dummy
        }

        void do_reply(AooError result, const AooResponse& response) const override {
            AooRequestDisconnect request { AOO_REQUEST_INIT(Disconnect, structSize) };

            callback((AooRequest&)request, result, response);
        }
    };

    struct login_cmd : icommand
    {
        login_cmd(const ip_address& public_ip)
            : public_ip_(public_ip) {}

        void perform(Client& obj) override {
            obj.perform(*this);
        }
        ip_address public_ip_;
    };

    struct timeout_cmd : icommand
    {
        void perform(Client& obj) override {
            obj.perform(*this);
        }
    };

    struct group_join_cmd : callback_cmd
    {
        // NB: group_pwd and user_pwd my be NULL!
        group_join_cmd(const AooClientJoinGroup& args, AooResponseHandler cb, void *user)
            : callback_cmd(cb, user),
              group_name_(args.groupName), group_pwd_(args.groupPassword ? args.groupPassword : ""),
              group_md_(args.groupMetadata),
              user_name_(args.userName), user_pwd_(args.userPassword ? args.userPassword : ""),
              user_md_(args.userMetadata),
              relay_(args.relayAddress ? *args.relayAddress : ip_host{}) {}

        void perform(Client& obj) override {
            obj.perform(*this);
        }

        void handle_response(Client &client, const osc::ReceivedMessage &msg) override {
            client.handle_response(*this, msg);
        }

        void do_reply(AooError result, const AooResponse& response) const override {
            AooData group_md { group_md_.type(), group_md_.data(), group_md_.size() };
            AooData user_md { user_md_.type(), user_md_.data(), user_md_.size() };
            AooIpEndpoint relay { relay_.name.c_str(), (AooUInt16)relay_.port };

            AooRequestGroupJoin request {
                AOO_REQUEST_INIT(GroupJoin, relayAddress),
                group_name_.c_str(), (group_pwd_.empty() ? nullptr : group_pwd_.c_str()),
                kAooIdInvalid, ((group_md.size > 0) ? &group_md : nullptr),
                user_name_.c_str(), (user_pwd_.empty() ? nullptr : user_pwd_.c_str()),
                kAooIdInvalid, ((user_md.size > 0) ? &user_md : nullptr),
                (relay_.valid() ? &relay : nullptr)
            };

            callback((AooRequest&)request, result, response);
        }
        std::string group_name_;
        std::string group_pwd_;
        aoo::metadata group_md_;
        std::string user_name_;
        std::string user_pwd_;
        aoo::metadata user_md_;
        ip_host relay_;
    };

    struct group_leave_cmd : callback_cmd
    {
        group_leave_cmd(AooId group, AooResponseHandler cb, void *user)
            : callback_cmd(cb, user), group_(group) {}

        void perform(Client& obj) override {
            obj.perform(*this);
        }

        void handle_response(Client &client, const osc::ReceivedMessage &msg) override {
            client.handle_response(*this, msg);
        }

        void do_reply(AooError result, const AooResponse& response) const override {
            AooRequestGroupLeave request { AOO_REQUEST_INIT(GroupLeave, group), group_ };

            callback((AooRequest&)request, result, response);
        }
        AooId group_;
    };

    struct group_update_cmd : callback_cmd
    {
        group_update_cmd(AooId group, const AooData &md, AooResponseHandler cb, void *context)
            : callback_cmd(cb, context),
              group_(group), md_(&md) {}

        void perform(Client& obj) override {
            obj.perform(*this);
        }

        void handle_response(Client &client, const osc::ReceivedMessage &msg) override {
            client.handle_response(*this, msg);
        }

        void do_reply(AooError result, const AooResponse& response) const override {
            AooRequestGroupUpdate request {
                AOO_REQUEST_INIT(GroupUpdate, groupMetadata),
                group_, { md_.type(), md_.data(), md_.size() }
            };

            callback((AooRequest&)request, result, response);
        }
        AooId group_;
        aoo::metadata md_;
    };

    struct user_update_cmd : callback_cmd
    {
        user_update_cmd(AooId group, const AooData &md,
                        AooResponseHandler cb, void *context)
            : callback_cmd(cb, context),
              group_(group), md_(&md) {}

        void perform(Client& obj) override {
            obj.perform(*this);
        }

        void handle_response(Client &client, const osc::ReceivedMessage &msg) override {
            client.handle_response(*this, msg);
        }

        void do_reply(AooError result, const AooResponse& response) const override {
            AooRequestUserUpdate request {
                AOO_REQUEST_INIT(UserUpdate, userMetadata),
                group_, kAooIdInvalid, { md_.type(), md_.data(), md_.size() }
            };

            callback((AooRequest&)request, result, response);
        }
        AooId group_;
        aoo::metadata md_;
    };

    struct custom_request_cmd : callback_cmd
    {
        custom_request_cmd(const AooData& data, AooFlag flags, AooResponseHandler cb, void *user)
            : callback_cmd(cb, user), data_(&data), flags_(flags) {}

        void perform(Client& obj) override {
            obj.perform(*this);
        }

        void handle_response(Client &client, const osc::ReceivedMessage &msg) override {
            auto it = msg.ArgumentsBegin();
            auto token = (it++)->AsInt32(); // skip
            auto result = (it++)->AsInt32();
            if (result == kAooErrorNone) {
                AooResponseCustom response; // default constructor
                response.flags = (AooFlag)(it++)->AsInt32();
                auto data = osc_read_metadata(it);
                if (data) {
                    response.data = *data;
                } else {
                    throw osc::MalformedMessageException("missing data");
                }

                reply((AooResponse&)response);
            } else {
                auto code = (it++)->AsInt32();
                auto msg = (it++)->AsString();
                reply_error(result, code, msg);
            }
        }

        void do_reply(AooError result, const AooResponse& response) const override {
            AooRequestCustom request {
                AOO_REQUEST_INIT(Custom, flags),
                { data_.type(), data_.data(), data_.size() }, flags_
            };

            callback((AooRequest&)request, result, response);
        }
        aoo::metadata data_;
        AooFlag flags_;
    };

    struct packet_cmd : icommand
    {
        packet_cmd(const AooByte *data, size_t size,
                   const ip_address& addr) {
            // NB: the packet may be empty!
            if (size > 0) {
                data_ = (AooByte*)aoo::allocate(size);
                memcpy(data_, data, size);
                size_ = size;
            }
            addr_ = addr;
        }

        packet_cmd(const packet_cmd&) = delete;

        ~packet_cmd() {
            if (data_) {
                aoo::deallocate(data_, size_);
            }
        }

        void perform(Client& c) override {
            c.udp_sendfn_(data_, size_, addr_);
        }
    private:
        ip_address addr_;
        AooByte *data_ = nullptr;
        size_t size_ = 0;
    };
};

} // net
} // aoo
