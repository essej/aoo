#pragma once

#include "aoo_requests.h"
#include "aoo_server.h"


#include "detail.hpp"
#include "osc_stream_receiver.hpp"
#include "ping_timer.hpp"
#include "tcp_server.hpp"
#include "wire_protocol.hpp"

namespace aoo {
namespace net {

class client_endpoint;
class Server;

//--------------------------- user -----------------------------//

class user {
public:
    user(std::string_view name, std::string_view pwd, AooId id,
         AooId group, AooId client, const AooData *md,
         const AooIpEndpoint *relay, AooFlag flags)
        : name_(name), pwd_(pwd), id_(id), group_(group),
          client_(client), flags_(flags), md_(md),
          relay_(relay ? *relay : ip_host{}) {}

    ~user() {}

    const std::string& name() const { return name_; }

    const std::string& pwd() const { return pwd_; }

    // NB: pwd may be NULL
    bool check_pwd(const char *pwd) const {
        return pwd_.empty() || (pwd && (pwd == pwd_));
    }

    AooId id() const { return id_; }

    bool flags() const { return flags_; }

    bool group_creator() const { return flags_ & kAooUserGroupCreator; }

    bool persistent() const { return flags_ & kAooUserPersistent; }

    void set_metadata(const AooData& md) {
        md_ = aoo::metadata(&md);
    }

    const aoo::metadata& metadata() const { return md_; }

    AooId group() const { return group_; }

    bool active() const { return client_ != kAooIdInvalid; }

    void set_client(AooId client) {
        client_ = client;
    }

    void unset() { client_ = kAooIdInvalid; }

    AooId client() const {return client_; }

    void set_relay(const ip_host& relay) {
        relay_ = relay;
    }

    const ip_host& relay_addr() const { return relay_; }
private:
    std::string name_;
    std::string pwd_;
    AooId id_;
    AooId group_;
    AooId client_;
    AooFlag flags_;
    aoo::metadata md_;
    ip_host relay_;
};

inline std::ostream& operator<<(std::ostream& os, const user& u) {
    os << "'" << u.name() << "'(" << u.id() << ")[" << u.client() << "]";
    return os;
}

//--------------------------- group -----------------------------//

using user_list = std::vector<user>;

class group {
public:
    group(std::string_view name, std::string_view pwd, AooId id,
          const AooData *md, const AooIpEndpoint *relay, AooFlag flags,
          bool is_public = false)
        : name_(name), pwd_(pwd), id_(id), flags_(flags), md_(md),
          relay_(relay ? *relay : ip_host{}), public_(is_public) {}

    const std::string& name() const { return name_; }

    const std::string& pwd() const { return pwd_; }

    bool check_pwd(const char *pwd) const {
        return pwd_.empty() || (pwd && (pwd == pwd_));
    }

    AooId id() const { return id_; }

    void set_metadata(const AooData& md) {
        md_ = aoo::metadata(&md);
    }

    const aoo::metadata& metadata() const { return md_; }

    const ip_host& relay_addr() const { return relay_; }

    AooFlag flags() const { return flags_; }

    bool persistent() const { return flags_ & kAooGroupPersistent; }

    bool is_public() const { return public_; }

    bool user_auto_create() const { return user_auto_create_; }

    user* add_user(user&& u);

    user* find_user(std::string_view name);

    user* find_user(AooId id);

    user* find_user(const client_endpoint& client);

    bool remove_user(AooId id);

    int32_t user_count() const { return users_.size(); }

    const user_list& users() const { return users_; }

    AooId get_next_user_id();
private:
    std::string name_;
    std::string pwd_;
    AooId id_;
    AooFlag flags_;
    aoo::metadata md_;
    ip_host relay_;
    bool public_ = false;
    bool user_auto_create_ = AOO_USER_AUTO_CREATE; // TODO
    user_list users_;
    AooId next_user_id_{1};
};

inline std::ostream& operator<<(std::ostream& os, const group& g) {
    os << "'" << g.name() << "'(" << g.id() << ")";
    return os;
}

//--------------------- client_endpoint --------------------------//

class client_endpoint {
public:
    client_endpoint(AooId id, aoo::tcp_server::reply_func fn)
        : id_(id), replyfn_(fn) {}

    ~client_endpoint() {}

    client_endpoint(client_endpoint&&) = default;
    client_endpoint& operator=(client_endpoint&&) = default;

    AooId id() const { return id_; }

    const std::string& version() const { return version_; }

    void activate(std::string version) {
        version_ = std::move(version);
    }

    void activate_legacy(std::string name, std::string password,
                         const ip_address& public_address,
                         const ip_address& local_address, int64_t token) {
        version_ = "legacy";
        legacy_name_ = std::move(name);
        legacy_password_ = std::move(password);
        legacy_public_address_ = public_address;
        legacy_local_address_ = local_address;
        legacy_token_ = token;
        public_addresses_.clear();
        if (public_address.valid()) {
            public_addresses_.push_back(public_address);
        }
        if (local_address.valid() && local_address != public_address) {
            public_addresses_.push_back(local_address);
        }
    }

    bool active() const {
        return !version_.empty();
    }

    wire_protocol protocol() const { return protocol_; }

    const std::string& legacy_name() const { return legacy_name_; }
    const std::string& legacy_password() const { return legacy_password_; }
    int64_t legacy_token() const { return legacy_token_; }

    void set_observed_address(const ip_address& address) {
        observed_address_ = address;
    }

    ip_address legacy_public_address() const {
        if (legacy_public_address_.valid()) {
            return legacy_public_address_;
        }
        if (observed_address_.valid()) {
            return observed_address_;
        }
        return public_addresses_.empty() ? ip_address{} : public_addresses_.back();
    }

    ip_address legacy_local_address() const {
        if (legacy_local_address_.valid()) {
            return legacy_local_address_;
        }
        return public_addresses_.empty() ? ip_address{} : public_addresses_.front();
    }

    bool watches_public_groups() const { return watches_public_groups_; }
    void set_watches_public_groups(bool value) { watches_public_groups_ = value; }

    void add_public_address(const ip_address& addr) {
        public_addresses_.push_back(addr);
    }

    const ip_address_list& public_addresses() const {
        return public_addresses_;
    }

    bool match(const ip_address& addr) const;

    void send_message(const osc::OutboundPacketStream& msg) const;

    void send_error(Server& server, AooId token, AooRequestType type,
                    AooError result, const AooResponseError *response = nullptr);

    void send_notification(Server& server, const AooData& data) const;

    void send_peer_join(Server& server, const group& grp, const user& usr,
                        const client_endpoint& client) const;

    void send_peer_leave(Server& server, const group& grp, const user& usr) const;

    void send_group_update(Server& server, const group& grp, AooId usr);

    void send_user_update(Server& server, const user& usr);

    void send_peer_update(Server& server, const user& peer);

    void on_group_join(Server& server, const group& grp, const user& usr);

    void on_group_leave(Server& server, const group& grp, const user& usr, bool eject);

    void on_close(Server& server);

    void handle_data(Server& server, const AooByte *data, int32_t n);

    std::pair<bool, double> update(Server& server, aoo::time_tag now,
                                   const AooPingSettings& settings);

    void handle_pong() {
        ping_timer_.pong();
    }
private:
    AooId id_;
    aoo::tcp_server::reply_func replyfn_;
    std::string version_;
    std::string legacy_name_;
    std::string legacy_password_;
    ip_address legacy_public_address_;
    ip_address legacy_local_address_;
    ip_address observed_address_;
    int64_t legacy_token_ = 0;
    bool watches_public_groups_ = false;
    wire_protocol protocol_ = wire_protocol::unknown;
    std::vector<AooByte> protocol_probe_;
    osc_stream_receiver receiver_;
    slip_stream_receiver legacy_receiver_;
    ip_address_list public_addresses_;
    struct group_user {
        AooId group;
        AooId user;
    };
    std::vector<group_user> group_users_;
    ping_timer ping_timer_;
    bool active_ = true;
};

} // net
} // aoo
