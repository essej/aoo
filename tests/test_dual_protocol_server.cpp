#include "aoo.h"
#include "aoo_server.hpp"

#include "aoo/src/detail.hpp"
#include "aoo/src/net/detail.hpp"
#include "aoo/src/net/osc_stream_receiver.hpp"
#include "aoo/src/net/peer.hpp"
#include "aoo/src/net/wire_protocol.hpp"
#include "common/net_utils.hpp"

#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace aoo;
using namespace aoo::net;

namespace {

constexpr const char *legacy_empty_password =
        "D41D8CD98F00B204E9800998ECF8427E";

[[noreturn]] void fail(const char *message) {
    std::cerr << "dual protocol server test failed: " << message << std::endl;
    std::abort();
}

void check(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

template<typename Fn>
std::vector<AooByte> make_osc(Fn&& fn) {
    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream packet(buffer, sizeof(buffer));
    fn(packet);
    return { (const AooByte *)packet.Data(),
             (const AooByte *)packet.Data() + packet.Size() };
}

std::pair<port_type, port_type> unused_tcp_ports() {
    // Keep both reservations open until both ports have been selected.
    tcp_socket first(port_tag{}, 0);
    tcp_socket second(port_tag{}, 0);
    return { first.port(), second.port() };
}

class server_runner {
public:
    server_runner(port_type primary_port, port_type legacy_port,
                  AooServerOptions options = 0, const char *password = nullptr) {
        server_ = AooServer::create();
        check((bool)server_, "could not create server");

        AooServerSettings settings;
        settings.portNumber = primary_port;
        settings.legacyPortNumber = legacy_port;
        settings.socketType = kAooSocketIPv4;
        settings.options = options;
        check(server_->setup(settings) == kAooOk, "could not setup server");
        if (password) {
            check(server_->setPassword(password) == kAooOk,
                  "could not set server password");
        }

        tcp_thread_ = std::thread([this]() {
            while (running_.load()) {
                auto result = server_->run(0.05);
                if (result != kAooOk && result != kAooErrorWouldBlock) {
                    error_.store(result);
                    return;
                }
            }
        });
        udp_thread_ = std::thread([this]() {
            while (running_.load()) {
                auto result = server_->receive(0.05);
                if (result != kAooOk && result != kAooErrorWouldBlock) {
                    error_.store(result);
                    return;
                }
            }
        });
    }

    ~server_runner() {
        running_.store(false);
        server_->stop();
        if (tcp_thread_.joinable()) {
            tcp_thread_.join();
        }
        if (udp_thread_.joinable()) {
            udp_thread_.join();
        }
        check(error_.load() == kAooOk, "server thread failed");
    }

private:
    AooServer::Ptr server_;
    std::atomic<bool> running_ { true };
    std::atomic<AooError> error_ { kAooOk };
    std::thread tcp_thread_;
    std::thread udp_thread_;
};

class stream_client {
public:
    stream_client(port_type port, wire_protocol protocol)
        : socket_(family_tag{}, ip_address::IPv4), protocol_(protocol) {
        socket_.connect(ip_address("127.0.0.1", port, ip_address::IPv4), 2.0);
    }

    void send_osc(const std::vector<AooByte>& packet) {
        std::vector<AooByte> frame;
        if (protocol_ == wire_protocol::legacy) {
            frame = slip_encode(packet.data(), (AooSize)packet.size());
        } else {
            frame.resize(packet.size() + sizeof(int32_t));
            to_bytes<int32_t>((int32_t)packet.size(), (char *)frame.data());
            memcpy(frame.data() + sizeof(int32_t), packet.data(), packet.size());
        }

        size_t sent = 0;
        while (sent < frame.size()) {
            sent += socket_.send(frame.data() + sent, (int)(frame.size() - sent));
        }
    }

    std::vector<AooByte> take(std::string_view pattern) {
        for (int attempt = 0; attempt < 20; ++attempt) {
            for (auto it = packets_.begin(); it != packets_.end(); ++it) {
                osc::ReceivedPacket packet((const char *)it->data(), it->size());
                osc::ReceivedMessage message(packet);
                if (message.AddressPattern() == pattern) {
                    auto result = std::move(*it);
                    packets_.erase(it);
                    return result;
                }
            }

            AooByte buffer[64 * 1024];
            auto [received, size] = socket_.receive(buffer, sizeof(buffer), 0.1);
            if (!received) {
                continue;
            }
            check(size > 0, "TCP connection closed");
            if (protocol_ == wire_protocol::legacy) {
                legacy_receiver_.handle_message(buffer, size,
                    [this](const AooByte *data, AooSize packet_size) {
                        packets_.emplace_back(data, data + packet_size);
                    });
            } else {
                current_receiver_.handle_message((const char *)buffer, size,
                    [this](const osc::ReceivedPacket& packet) {
                        auto data = (const AooByte *)packet.Contents();
                        packets_.emplace_back(data, data + packet.Size());
                    });
            }
        }
        fail("timed out waiting for TCP message");
    }

private:
    tcp_socket socket_;
    wire_protocol protocol_;
    osc_stream_receiver current_receiver_;
    slip_stream_receiver legacy_receiver_;
    std::deque<std::vector<AooByte>> packets_;
};

std::vector<AooByte> receive_udp(udp_socket& socket, port_type source_port) {
    AooByte buffer[AOO_MAX_PACKET_SIZE];
    ip_address source;
    auto [received, size] = socket.receive(buffer, sizeof(buffer), source, 2.0);
    check(received && size > 0, "timed out waiting for UDP reply");
    check(source.port() == source_port, "UDP reply used the wrong listener port");
    return { buffer, buffer + size };
}

void check_pattern(const std::vector<AooByte>& packet, const char *pattern) {
    osc::ReceivedPacket received((const char *)packet.data(), packet.size());
    osc::ReceivedMessage message(received);
    check(!strcmp(message.AddressPattern(), pattern), "unexpected OSC address");
}

void test_legacy_peer_matching() {
    ip_address candidate("127.0.0.1", 19001, ip_address::IPv4);
    peer_args args {
        "group", "user", 1, 2, 3, kAooPeerLegacyProtocol, 42, "legacy",
        nullptr, ip_address::IPv6, true, false, { candidate }, {}, {}
    };
    peer legacy(std::move(args));
    check(legacy.match_legacy_ping(candidate, 0),
          "legacy regular ping did not match an unconnected candidate");
#if AOO_USE_IPV6
    check(legacy.match_legacy_ping(candidate.ipv4_mapped(), 0),
          "IPv4-mapped legacy candidate did not match");
#endif
    check(legacy.match_legacy_ping(ip_address("127.0.0.1", 19002), 42),
          "legacy handshake token did not match");
    check(!legacy.match_legacy_ping(candidate, 43),
          "wrong legacy handshake token was accepted");
}

void test_dual_protocol_server() {
    auto [primary_port, legacy_port] = unused_tcp_ports();

    server_runner server(primary_port, legacy_port);
    udp_socket legacy_udp(ip_address(0, ip_address::IPv4));
    udp_socket current_udp(ip_address(0, ip_address::IPv4));
    ip_address primary("127.0.0.1", primary_port, ip_address::IPv4);
    ip_address legacy("127.0.0.1", legacy_port, ip_address::IPv4);

    auto legacy_request = make_osc([](auto& msg) {
        msg << osc::BeginMessage("/aoo/server/request") << osc::EndMessage;
    });
    legacy_udp.send(legacy_request.data(), legacy_request.size(), legacy);
    auto legacy_reply = receive_udp(legacy_udp, legacy_port);
    check_pattern(legacy_reply, "/aoo/client/reply");
    {
        osc::ReceivedPacket packet((const char *)legacy_reply.data(), legacy_reply.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check(!strcmp((it++)->AsString(), "127.0.0.1"), "wrong legacy public IP");
        check((it++)->AsInt32() == legacy_udp.port(), "wrong legacy public port");
    }

    auto current_query = make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerQuery) << osc::EndMessage;
    });
    current_udp.send(current_query.data(), current_query.size(), primary);
    check_pattern(receive_udp(current_udp, primary_port), kAooMsgClientQuery);

    auto ping = make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerPing) << osc::EndMessage;
    });
    legacy_udp.send(ping.data(), ping.size(), legacy);
    check_pattern(receive_udp(legacy_udp, legacy_port), kAooMsgClientPing);
    current_udp.send(ping.data(), ping.size(), primary);
    check_pattern(receive_udp(current_udp, primary_port), kAooMsgClientPong);

    stream_client legacy_client(legacy_port, wire_protocol::legacy);
    stream_client current_client(primary_port, wire_protocol::current);

    legacy_client.send_osc(make_osc([&](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerLogin)
            << "legacy-user" << legacy_empty_password
            << "127.0.0.1" << (int32_t)legacy_udp.port()
            << "127.0.0.1" << (int32_t)legacy_udp.port()
            << (int64_t)101 << osc::EndMessage;
    }));
    auto login_reply = legacy_client.take(kAooMsgClientLogin);
    {
        osc::ReceivedPacket packet((const char *)login_reply.data(), login_reply.size());
        osc::ReceivedMessage message(packet);
        check(message.ArgumentsBegin()->AsInt32() == 1, "legacy login failed");
    }

    current_client.send_osc(make_osc([&](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerLogin)
            << (int32_t)201 << aoo_getVersionString() << ""
            << osc::Nil << osc::Nil
            << (int32_t)1 << "127.0.0.1" << (int32_t)current_udp.port()
            << osc::EndMessage;
    }));
    login_reply = current_client.take(kAooMsgClientLogin);
    {
        osc::ReceivedPacket packet((const char *)login_reply.data(), login_reply.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check((it++)->AsInt32() == 201, "wrong current login token");
        check((it++)->AsInt32() == kAooErrorNone, "current login failed");
    }

    legacy_client.send_osc(make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerGroupJoin)
            << "mixed" << legacy_empty_password << false << osc::EndMessage;
    }));
    auto join_reply = legacy_client.take(kAooMsgClientGroupJoin);
    {
        osc::ReceivedPacket packet((const char *)join_reply.data(), join_reply.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check(!strcmp((it++)->AsString(), "mixed"), "wrong legacy group name");
        check((it++)->AsInt32() == 1, "legacy group join failed");
    }

    current_client.send_osc(make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerGroupJoin)
            << (int32_t)202 << "mixed" << ""
            << osc::Nil << osc::Nil
            << "current-user" << ""
            << osc::Nil << osc::Nil
            << osc::Nil << osc::Nil
            << osc::EndMessage;
    }));
    join_reply = current_client.take(kAooMsgClientGroupJoin);
    AooId group_id;
    AooId current_user_id;
    {
        osc::ReceivedPacket packet((const char *)join_reply.data(), join_reply.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check((it++)->AsInt32() == 202, "wrong current join token");
        check((it++)->AsInt32() == kAooErrorNone, "current group join failed");
        group_id = (it++)->AsInt32();
        (it++)->AsInt32();
        osc_read_metadata(it);
        current_user_id = (it++)->AsInt32();
    }

    auto peer_join = legacy_client.take(kAooMsgClientPeerJoin);
    {
        osc::ReceivedPacket packet((const char *)peer_join.data(), peer_join.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check(!strcmp((it++)->AsString(), "mixed"), "wrong legacy peer group");
        check(!strcmp((it++)->AsString(), "current-user"), "wrong legacy peer user");
        check(!strcmp((it++)->AsString(), "127.0.0.1"), "wrong legacy peer public IP");
        check((it++)->AsInt32() == current_udp.port(), "wrong legacy peer public port");
        check(!strcmp((it++)->AsString(), "127.0.0.1"), "wrong legacy peer local IP");
        check((it++)->AsInt32() == current_udp.port(), "wrong legacy peer local port");
        check((it++)->AsInt64() == current_user_id, "legacy peer token lost current user ID");
    }

    peer_join = current_client.take(kAooMsgClientPeerJoin);
    {
        osc::ReceivedPacket packet((const char *)peer_join.data(), peer_join.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check(!strcmp((it++)->AsString(), "mixed"), "wrong current peer group");
        check((it++)->AsInt32() == group_id, "wrong current peer group ID");
        check(!strcmp((it++)->AsString(), "legacy-user"), "wrong current peer user");
        (it++)->AsInt32();
        check(!strcmp((it++)->AsString(), "legacy"), "legacy generation was lost");
        auto flags = (AooFlag)(it++)->AsInt32();
        check(flags & kAooPeerLegacyProtocol, "legacy peer flag was not set");
        osc_read_metadata(it);
        osc_read_host(it);
        auto address_count = (it++)->AsInt32();
        for (int32_t i = 0; i < address_count; ++i) {
            osc_read_address(it);
        }
        check((it++)->AsInt64() == 101, "current peer lost legacy NAT token");
    }

    current_client.send_osc(make_osc([&](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerGroupLeave)
            << (int32_t)203 << group_id << osc::EndMessage;
    }));
    auto peer_leave = legacy_client.take(kAooMsgClientPeerLeave);
    {
        osc::ReceivedPacket packet((const char *)peer_leave.data(), peer_leave.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check(!strcmp((it++)->AsString(), "mixed"), "wrong legacy leave group");
        check(!strcmp((it++)->AsString(), "current-user"), "wrong legacy leave user");
    }

    legacy_client.send_osc(make_osc([](auto& msg) {
        msg << osc::BeginMessage("/aoo/server/group/public")
            << true << osc::EndMessage;
    }));
    check_pattern(legacy_client.take("/aoo/client/group/public"),
                  "/aoo/client/group/public");

    const std::string subscribe_json =
            "{\"type\":\"public_group_subscribe\",\"subscribe\":true}";
    current_client.send_osc(make_osc([&](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerRequest)
            << (int32_t)204 << (int32_t)0 << (int32_t)kAooDataJSON
            << osc::Blob(subscribe_json.data(), subscribe_json.size())
            << osc::EndMessage;
    }));
    check_pattern(current_client.take(kAooMsgClientRequest), kAooMsgClientRequest);

    const std::string group_json =
            "{\"type\":\"group\",\"name\":\"public\",\"isPublic\":true}";
    current_client.send_osc(make_osc([&](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerGroupJoin)
            << (int32_t)205 << "public" << ""
            << (int32_t)kAooDataJSON
            << osc::Blob(group_json.data(), group_json.size())
            << "current-public-user" << ""
            << osc::Nil << osc::Nil
            << osc::Nil << osc::Nil
            << osc::EndMessage;
    }));
    join_reply = current_client.take(kAooMsgClientGroupJoin);
    AooId public_group_id;
    {
        osc::ReceivedPacket packet((const char *)join_reply.data(), join_reply.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check((it++)->AsInt32() == 205, "wrong public join token");
        check((it++)->AsInt32() == kAooErrorNone, "current public join failed");
        public_group_id = (it++)->AsInt32();
    }

    auto public_add = legacy_client.take("/aoo/client/group/public/add");
    {
        osc::ReceivedPacket packet((const char *)public_add.data(), public_add.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check(!strcmp((it++)->AsString(), "public"), "wrong public group name");
        check((it++)->AsInt32() == 1, "wrong public group size");
    }
    auto current_public_update = current_client.take(kAooMsgClientMessage);
    {
        osc::ReceivedPacket packet((const char *)current_public_update.data(),
                                   current_public_update.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check((it++)->AsInt32() == kAooDataJSON, "wrong public metadata type");
        const void *data;
        osc::osc_bundle_element_size_t size;
        (it++)->AsBlob(data, size);
        std::string_view json((const char *)data, size);
        check(json.find("\"groupName\":\"public\"") != std::string_view::npos,
              "current public update lost the group name");
    }

    legacy_client.send_osc(make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerGroupJoin)
            << "public" << legacy_empty_password << true << osc::EndMessage;
    }));
    legacy_client.take(kAooMsgClientGroupJoin);
    public_add = legacy_client.take("/aoo/client/group/public/add");
    {
        osc::ReceivedPacket packet((const char *)public_add.data(), public_add.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        check(!strcmp((it++)->AsString(), "public"), "wrong public group name");
        check((it++)->AsInt32() == 2, "wrong mixed public group size");
    }

    legacy_client.send_osc(make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerGroupLeave)
            << "public" << osc::EndMessage;
    }));
    public_add = legacy_client.take("/aoo/client/group/public/add");
    {
        osc::ReceivedPacket packet((const char *)public_add.data(), public_add.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        (it++)->AsString();
        check((it++)->AsInt32() == 1, "public group did not retain current user");
    }

    current_client.send_osc(make_osc([&](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerGroupLeave)
            << (int32_t)206 << public_group_id << osc::EndMessage;
    }));
    public_add = legacy_client.take("/aoo/client/group/public/add");
    {
        osc::ReceivedPacket packet((const char *)public_add.data(), public_add.size());
        osc::ReceivedMessage message(packet);
        auto it = message.ArgumentsBegin();
        (it++)->AsString();
        check((it++)->AsInt32() == 0, "public group did not reach zero users");
    }
    check_pattern(legacy_client.take("/aoo/client/group/public/del"),
                  "/aoo/client/group/public/del");
}

void test_force_legacy_fallback() {
    auto [primary_port, legacy_port] = unused_tcp_ports();
    server_runner server(primary_port, legacy_port,
                         kAooServerForceLegacyProtocol);
    stream_client client(primary_port, wire_protocol::current);

    client.send_osc(make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerLogin)
            << (int32_t)301 << aoo_getVersionString() << ""
            << osc::Nil << osc::Nil << (int32_t)0
            << osc::EndMessage;
    }));
    auto reply = client.take(kAooMsgClientLogin);
    osc::ReceivedPacket packet((const char *)reply.data(), reply.size());
    osc::ReceivedMessage message(packet);
    auto it = message.ArgumentsBegin();
    check((it++)->AsInt32() == 301, "wrong fallback login token");
    check((it++)->AsInt32() == kAooErrorVersionNotSupported,
          "compatibility mode did not request legacy fallback");
}

void test_legacy_cannot_bypass_server_password() {
    auto [primary_port, legacy_port] = unused_tcp_ports();
    server_runner server(primary_port, legacy_port, 0, "secret");
    stream_client client(legacy_port, wire_protocol::legacy);
    client.send_osc(make_osc([](auto& msg) {
        msg << osc::BeginMessage(kAooMsgServerLogin)
            << "legacy-user" << legacy_empty_password
            << "127.0.0.1" << (int32_t)12345
            << "127.0.0.1" << (int32_t)12345
            << (int64_t)101 << osc::EndMessage;
    }));
    auto reply = client.take(kAooMsgClientLogin);
    osc::ReceivedPacket packet((const char *)reply.data(), reply.size());
    osc::ReceivedMessage message(packet);
    check(message.ArgumentsBegin()->AsInt32() == 0,
          "legacy login bypassed the server password");
}

} // namespace

int main() {
    check(aoo_initialize(nullptr) == kAooOk, "could not initialize AOO");
    test_legacy_peer_matching();
    test_dual_protocol_server();
    test_force_legacy_fallback();
    test_legacy_cannot_bypass_server_password();
    aoo_terminate();
    std::cout << "dual protocol server test succeeded!" << std::endl;
    return EXIT_SUCCESS;
}
