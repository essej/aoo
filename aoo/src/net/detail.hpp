#pragma once

#include "../detail.hpp"

// OSC address patterns

#define kAooNetMsgPingReply \
    kAooNetMsgPing kAooNetMsgReply

#define kAooNetMsgClientPingReply \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgPingReply

#define kAooNetMsgClientQuery \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgQuery

#define kAooNetMsgClientLogin \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgLogin

#define kAooNetMsgGroupJoin \
    kAooNetMsgGroup kAooNetMsgJoin

#define kAooNetMsgGroupLeave \
    kAooNetMsgGroup kAooNetMsgLeave

#define kAooNetMsgGroupUpdate \
    kAooNetMsgGroup kAooNetMsgUpdate

#define kAooNetMsgGroupChanged \
    kAooNetMsgGroup kAooNetMsgChanged

#define kAooNetMsgUserUpdate \
    kAooNetMsgUser kAooNetMsgUpdate

#define kAooNetMsgUserChanged \
    kAooNetMsgUser kAooNetMsgChanged

#define kAooNetMsgPeerJoin \
    kAooNetMsgPeer kAooNetMsgJoin

#define kAooNetMsgPeerLeave \
    kAooNetMsgPeer kAooNetMsgLeave

#define kAooNetMsgPeerChanged \
    kAooNetMsgPeer kAooNetMsgChanged

// peer messages

#define kAooNetMsgPeerPing \
    kAooMsgDomain kAooNetMsgPeer kAooNetMsgPing

#define kAooNetMsgPeerPingReply \
    kAooMsgDomain kAooNetMsgPeer kAooNetMsgPingReply

#define kAooNetMsgPeerMessage \
    kAooMsgDomain kAooNetMsgPeer kAooNetMsgMessage

#define kAooNetMsgPeerAck \
    kAooMsgDomain kAooNetMsgPeer kAooNetMsgAck

// client messages

#define kAooNetMsgClientQuery \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgQuery

#define kAooNetMsgClientGroupJoin \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgGroupJoin

#define kAooNetMsgClientGroupLeave \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgGroupLeave

#define kAooNetMsgClientGroupUpdate \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgGroupUpdate

#define kAooNetMsgClientUserUpdate \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgUserUpdate

#define kAooNetMsgClientGroupChanged \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgGroupChanged

#define kAooNetMsgClientUserChanged \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgUserChanged

#define kAooNetMsgClientPeerChanged \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgPeerChanged

#define kAooNetMsgClientRequest \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgRequest

#define kAooNetMsgClientPeerJoin \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgPeerJoin

#define kAooNetMsgClientPeerLeave \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgPeerLeave

#define kAooNetMsgClientMessage \
    kAooMsgDomain kAooNetMsgClient kAooNetMsgMessage

// server messages

#define kAooNetMsgServerLogin \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgLogin

#define kAooNetMsgServerQuery \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgQuery

#define kAooNetMsgServerPing \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgPing

#define kAooNetMsgServerGroupJoin \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgGroupJoin

#define kAooNetMsgServerGroupLeave \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgGroupLeave

#define kAooNetMsgServerGroupUpdate \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgGroupUpdate

#define kAooNetMsgServerUserUpdate \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgUserUpdate

#define kAooNetMsgServerRequest \
    kAooMsgDomain kAooNetMsgServer kAooNetMsgRequest

namespace aoo {
namespace net {

AooError parse_pattern(const AooByte *msg, int32_t n,
                       AooMsgType& type, int32_t& offset);

AooSize write_relay_message(AooByte *buffer, AooSize bufsize,
                            const AooByte *msg, AooSize msgsize,
                            const ip_address& addr);

std::string encrypt(const std::string& input);

struct ip_host {
    ip_host() = default;
    ip_host(const std::string& _name, int _port)
        : name(_name), port(_port) {}
    ip_host(const AooIpEndpoint& ep)
        : name(ep.hostName), port(ep.port) {}

    std::string name;
    int port = 0;

    bool valid() const {
        return !name.empty() && port > 0;
    }

    bool operator==(const ip_host& other) const {
        return name == other.name && port == other.port;
    }

    bool operator!=(const ip_host& other) const {
        return !operator==(other);
    }
};

#if 0
using ip_address_list = std::vector<ip_address, aoo::allocator<ip_address>>;
#else
using ip_address_list = std::vector<ip_address>;
#endif

osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const ip_host& addr);

ip_host osc_read_host(osc::ReceivedMessageArgumentIterator& it);

} // namespace net
} // namespace aoo
