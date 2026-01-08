#include "peer.hpp"
#include "client.hpp"
#include "client_events.hpp"

#include "../binmsg.hpp"

#include <algorithm>
#include <sstream>

// debugging

// define this to 1 to force server relay (for testing purposes)
#define FORCE_RELAY 0

namespace aoo {
namespace net {

//------------------------- peer ------------------------------//

peer::peer(peer_args&& args)
    : group_name_(args.group_name), user_name_(args.user_name),
      group_id_(args.group_id), user_id_(args.user_id),
      local_id_(args.local_id), flags_(args.flags), version_(args.version_string),
      address_family_(args.address_family), binary_(args.binary),
      use_ipv4_mapped_(args.use_ipv4_mapped), metadata_(args.metadata),
      addrlist_(std::move(args.address_list)), user_relay_(std::move(args.user_relay)),
      relay_list_(std::move(args.relay_list))
{
    LOG_DEBUG("AooClient: create peer " << *this);
    for (auto& addr : addrlist_) {
        LOG_DEBUG("\t" << addr);
    }
}

peer::~peer(){
    LOG_DEBUG("AooClient: destroy peer " << *this);
}

bool peer::match(const ip_address& addr) const {
    if (connected()){
        // NB: 'addr' has been obtained with address(), so we can match as is.
        return real_address_ == addr;
    } else {
        return false;
    }
}

bool peer::match(std::string_view group) const {
    return group_name_ == group; // immutable!
}

bool peer::match(std::string_view group, std::string_view user) const {
    return group_name_ == group && user_name_ == user; // immutable!
}

bool peer::match(AooId group) const {
    return group_id_ == group; // immutable!
}

bool peer::match(AooId group, AooId user) const {
    return group_id_ == group && user_id_ == user;
}

bool peer::match_wildcard(AooId group, AooId user) const {
    return (group_id_ == group || group == kAooIdInvalid) &&
            (user_id_ == user || user == kAooIdInvalid);
}

ip_address peer::address() const {
    if (connected()) {
        // may be IPv4 mapped for peer-to-peer, but always unmapped for relay
        return real_address_;
    } else {
        return ip_address{};
    }
}

void peer::send(Client& client, const sendfn& fn, time_tag now,
                const AooPingSettings& settings) {
    if (connected()) {
        do_send(client, fn, now, settings);
    } else if (!timeout_) {
        // try to establish UDP connection with peer

        if (handshake_deadline_.is_empty()) {
            // initialize timer
            auto timeout = settings.probeInterval * settings.probeCount;
            handshake_deadline_ = now + aoo::time_tag::from_seconds(timeout);
            next_handshake_ = now;
        }

        if (now >= handshake_deadline_) {
            // time out -> try to relay
            if (!relay_list_.empty() && !need_relay()) {
                // for now we just try the first relay address.
                // LATER try all of them.
                relay_address_ = relay_list_.front();
                flags_ |= kAooPeerNeedRelay;
                handshake_deadline_.clear(); // reset timer
                LOG_WARNING("AooClient: UDP handshake with " << *this
                            << " timed out, try to relay over " << relay_address_);
                return;
            }

            // couldn't establish connection!
            const char *what = need_relay() ? "relay" : "peer-to-peer";
            auto timeout = settings.probeInterval * settings.probeCount;
            LOG_ERROR("AooClient: couldn't establish UDP " << what
                      << " connection to " << *this << "; timed out after "
                      << timeout << " seconds");

            auto e = std::make_unique<peer_event>(kAooEventPeerTimeout, *this);
            client.send_event(std::move(e));

            timeout_ = true;

            return;
        }

        // send handshake pings to all addresses until we get a reply
        // from one of them; see handle_message().
        if (now >= next_handshake_) {
            next_handshake_ += aoo::time_tag::from_seconds(settings.probeInterval);

            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            // /aoo/peer/ping <group> <user> nil
            // NB: we send *our* user ID, see above. The protocol asks as to send 'nil'
            // instead of a timetag to dinstinguish a handshake ping from a regular ping!
            //
            // With the group and user ID, peers can identify us even if
            // we're behind a symmetric NAT. This trick doesn't work
            // if both parties are behind a symmetrict NAT; in that case,
            // UDP hole punching simply doesn't work.
            msg << osc::BeginMessage(kAooMsgPeerPing)
                << group_id_ << local_id_ << osc::Nil
                << osc::EndMessage;

            for (auto& addr : addrlist_) {
                if (need_relay()) {
                    send(msg, addr, fn); // always keep IP address as is!
                } else {
                    if (address_family_ == ip_address::IPv6 && addr.type() == ip_address::IPv4) {
                        if (use_ipv4_mapped_) {
                            // map address to IPv4
                            send(msg, addr.ipv4_mapped(), fn);
                        } else {
                            // cannot send to IPv4 endpoint from IPv6-only socket, just ignore.
                            // (We might still use the address later for relaying.)
                            LOG_DEBUG("AooClient: cannot send to " << addr);
                        }
                    } else if (address_family_ == ip_address::IPv4 && addr.type() == ip_address::IPv6) {
                        // cannot send to IPv6 endpoint from IPv4-only socket, just ignore.
                        // (We might still use the address later for relaying.)
                        LOG_DEBUG("AooClient: cannot send to " << addr);
                    } else {
                        send(msg, addr, fn);
                    }
                }
            }

            LOG_DEBUG("AooClient: send handshake ping to " << *this);
        }
    }
}

void peer::do_send(Client& client, const sendfn& fn, time_tag now,
                   const AooPingSettings& settings) {
    // send regular ping or probe ping
    auto result = ping_timer_.update(now, settings, true);
    if (result.ping) {
        // /aoo/peer/ping
        // NB: we send *our* user ID, so that the receiver can easily match the message.
        // We do not have to specifiy the peer's user ID because the group Id is already
        // sufficient. (There can only be one user per client in a group.)
        char buf[64];
        osc::OutboundPacketStream msg(buf, sizeof(buf));
        msg << osc::BeginMessage(kAooMsgPeerPing)
            << group_id_ << local_id_ << osc::TimeTag(now)
            << osc::EndMessage;

        send(msg, fn);

        if (result.state == ping_state::probe) {
            LOG_DEBUG("AooClient: send probe ping to " << *this << " (" << now << ")");
        } else {
            LOG_DEBUG("AooClient: send ping to " << *this << " (" << now << ")");
        }
    }
    if (active_ && result.state == ping_state::inactive) {
        active_ = false; // -> inactive
        // send event
        auto e = std::make_unique<peer_state_event>(*this, true);
        client.send_event(std::move(e));

        LOG_INFO("AooClient: " << *this << " became inactive");
    } else if (!active_ && result.state != ping_state::inactive) {
        active_ = true; // -> active
        // send event
        auto e = std::make_unique<peer_state_event>(*this, false);
        client.send_event(std::move(e));

        LOG_INFO("AooClient: " << *this << " became active again");
    }

    // 2) reply to /ping message
    // NB: we send *our* user ID, see above.
    if (got_ping_.exchange(false, std::memory_order_acquire)) {
        // The empty timetag distinguishes a handshake pong from a regular pong.
        auto tt1 = ping_tt1_;
        auto tt2 = ping_tt2_;
        if (!tt1.is_empty()) {
            // regular pong
            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage(kAooMsgPeerPong)
                << group_id_ << local_id_
                << osc::TimeTag(tt1) << osc::TimeTag(tt2) << osc::TimeTag(now)
                << osc::EndMessage;

            send(msg, fn);

            LOG_DEBUG("AooClient: send pong to " << *this << " (tt1: " << tt1
                      << ", tt2: " << tt2 << ", tt3: " << now << ")");
        } else {
            // handshake pong
            // NB: The protocol asks as to send 'nil' instead of timetags.
            char buf[64];
            osc::OutboundPacketStream msg(buf, sizeof(buf));
            msg << osc::BeginMessage(kAooMsgPeerPong)
                << group_id_ << local_id_
                << osc::Nil << osc::Nil << osc::Nil
                << osc::EndMessage;

            send(msg, fn);

            LOG_DEBUG("AooClient: send handshake pong to " << *this);
        }
    }
    // 3) send outgoing acks
    // LATER send them in batches!
    message_ack ack;
    while (send_acks_.pop(ack)) {
        send_ack(ack, fn);
    }
    // 4) handle incoming acks
    while (received_acks_.pop(ack)) {
        if (auto msg = send_buffer_.find(ack.sequence)) {
            if (ack.frame_index >= 0) {
                msg->ack_frame(ack.frame_index);
            } else {
                // negative -> all frames
                msg->ack_all();
            }
        } else {
        #if AOO_DEBUG_CLIENT_MESSAGE
            LOG_DEBUG("AooClient: got outdated ack (seq: " << ack.sequence
                      << ", frame: " << ack.frame_index << ") from " << *this);
        #endif
        }
    }
    // 5) pop acknowledged messages
    while (!send_buffer_.empty()) {
        auto& msg = send_buffer_.front();
        if (msg.complete()) {
        #if AOO_DEBUG_CLIENT_MESSAGE
            LOG_DEBUG("AooClient: pop acknowledged message (seq: "
                      << msg.sequence_ << ") from " << *this);
        #endif
            send_buffer_.pop();
        } else {
            break;
        }
    }
    // 6) resend messages
    if (!send_buffer_.empty()) {
        bool binary = client.binary();
        for (auto& msg : send_buffer_) {
            if (msg.need_resend(now)) {
                message_packet p;
                p.type = msg.data_.type();
                p.data = nullptr;
                p.size = 0;
                p.tt = msg.tt_;
                p.sequence = msg.sequence_;
                p.total_size = msg.data_.size();
                p.num_frames = msg.num_frames_;
                p.frame_index = 0;
                p.reliable = true;
                for (int i = 0; i < p.num_frames; ++i) {
                    if (!msg.has_frame(i)) {
                        p.frame_index = i;
                        msg.get_frame(i, p.data, p.size);
                    #if AOO_DEBUG_CLIENT_MESSAGE
                        LOG_DEBUG("AooClient: resend message (seq: " << msg.sequence_
                                  << ", frame: " << i << ") to " << *this);
                    #endif
                        if (binary) {
                            send_packet_bin(p, fn);
                        } else {
                            send_packet_osc(p, fn);
                        }
                    }
                }
            }
        }
    }
}

// OSC:
// /aoo/peer/msg <group> <user> <flags> <seq> <total> <nframes> <frame> <tt> <type> <data>
//
// binary:
// header (group + user), flags (uint16), size (uint16), seq (int32),
// [total (int32), nframes (int32), frame (int32)], [tt (uint64)], type (int32), data (bin)
//
// 'total', 'nframes' and 'frame' are omitted for single-frame messages. 'tt' may be omitted if zero.

// if 'flags' contains kAooMessageReliable, the other end sends an ack messages:
//
// OSC:
// /aoo/peer/ack <count> <seq1> <frame1> <seq2> <frame2> etc. // frame < 0 -> all
//
// binary:
// type (int8), cmd (int8), count (int16), <seq1> <frame1> <seq2> <frame2> etc.
//
// LATER: seq1 (int32), offset1 (int16), bitset1 (int16), etc. // offset < 0 -> all

// prevent excessive resending in low-latency networks
#define AOO_CLIENT_MIN_RESEND_TIME 0.02

void peer::send_message(const message& m, const sendfn& fn, int32_t packet_size, bool binary) {
    // LATER make packet size settable at runtime, see AooSource::send_data()
    const int32_t maxsize = packet_size - (binary ? kBinMessageHeaderSize : kMessageHeaderSize);
    auto d = std::div((int32_t)m.data_.size(), maxsize);

    message_packet p;
    p.type = m.data_.type();
    p.size = 0;
    p.data = nullptr;
    p.tt = m.tt_;
    // p.sequence =
    p.total_size = m.data_.size();
    p.num_frames = d.quot + (d.rem != 0);
    p.frame_index = 0;
    p.reliable = m.reliable_;

    if (p.reliable) {
        p.sequence = next_sequence_reliable_++;
        auto framesize = d.quot ? maxsize : d.rem;
        // wait twice the average RTT before resending
        auto rtt = average_rtt_.load(std::memory_order_relaxed);
        auto interval = std::max<float>(rtt * 2, AOO_CLIENT_MIN_RESEND_TIME);
        sent_message sm(m.data_, m.tt_, p.sequence, p.num_frames, framesize, interval);
        send_buffer_.push(std::move(sm));
    } else {
        // NB: use different sequences for reliable and unreliable messages!
        p.sequence = next_sequence_unreliable_++;
    }

    if (p.num_frames > 1) {
        // multi-frame message
        for (int32_t i = 0; i < p.num_frames; ++i) {
            p.frame_index = i;
            if (i == (p.num_frames - 1)) {
                p.size = d.rem;
                p.data = m.data_.data() + m.data_.size() - d.rem;
            } else {
                p.size = maxsize;
                p.data = m.data_.data() + (i * maxsize);
            }
            if (binary) {
                send_packet_bin(p, fn);
            } else {
                send_packet_osc(p, fn);
            }
        }
    } else {
        // single-frame message
        p.data = m.data_.data();
        p.size = m.data_.size();
        if (binary) {
            send_packet_bin(p, fn);
        } else {
            send_packet_osc(p, fn);
        }
    }
}

void peer::send_packet_osc(const message_packet& p, const sendfn& fn) const {
    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));
    AooFlag flags = p.reliable * kAooMessageReliable;
    // NB: we send *our* ID (= the sender)
    msg << osc::BeginMessage(kAooMsgPeerMessage)
        << group_id() << local_id() << (int32_t)flags
        << p.sequence << p.total_size;
    // frames
    if (p.num_frames > 1) {
        msg << p.num_frames << p.frame_index;
    } else {
        msg << osc::Nil << osc::Nil;
    }
    // timetag
    if (!p.tt.is_empty()) {
        msg << osc::TimeTag(p.tt);
    } else {
        msg << osc::Nil;
    }
    msg << p.type << osc::Blob(p.data, p.size)
        << osc::EndMessage;
    send(msg, fn);
}

void peer::send_packet_bin(const message_packet& p, const sendfn& fn) const {
    AooByte buf[AOO_MAX_PACKET_SIZE];

    AooFlag flags = 0;
    if (p.reliable) {
        flags |= kAooBinMsgMessageReliable;
    }
    if (p.num_frames > 1) {
        flags |= kAooBinMsgMessageFrames;
    }
    if (!p.tt.is_empty()) {
        flags |= kAooBinMsgMessageTimestamp;
    }

    // NB: we send *our* user ID
    auto offset = aoo::binmsg_write_header(
        buf, sizeof(buf), kAooMsgTypePeer, kAooBinMsgCmdMessage, group_id(), local_id());
    auto ptr = buf + offset;
    auto end = buf + sizeof(buf);
    aoo::write_bytes<int32_t>(p.sequence, ptr);
    aoo::write_bytes<uint16_t>(flags, ptr);
    aoo::write_bytes<uint16_t>(p.size, ptr);
    if (flags & kAooBinMsgMessageFrames) {
        aoo::write_bytes<int32_t>(p.total_size, ptr);
        aoo::write_bytes<int32_t>(p.num_frames, ptr);
        aoo::write_bytes<int32_t>(p.frame_index, ptr);
    }
    if (flags & kAooBinMsgMessageTimestamp) {
        aoo::write_bytes<uint64_t>(p.tt, ptr);
    }
    aoo::write_bytes<int32_t>(p.type, ptr);
    // write actual data
    assert((end - ptr) >= p.size);
    memcpy(ptr, p.data, p.size);
    ptr += p.size;

    send(buf, ptr - buf, fn);
}

// if the message is reliable, the other end sends an ack messages:
// /aoo/peer/ack <group> <user> <count> <seq1> <frame1> <seq2> <frame2> etc.
// resp.
// header, count (int32), seq1 (int32), frame1 (int32), seq2 (int32), frame2 (int32), etc. // frame < 0 -> all
void peer::send_ack(const message_ack &ack, const sendfn& fn) {
#if AOO_DEBUG_CLIENT_MESSAGE
    LOG_DEBUG("AooClient: send ack (seq: " << ack.sequence << ", frame: " << ack.frame_index
              << ") to " << *this);
#endif
    if (binary_ack_.load(std::memory_order_relaxed)) {
        AooByte buf[64];
        auto onset = binmsg_write_header(buf, sizeof(buf), kAooMsgTypePeer,
                                         kAooBinMsgCmdAck, group_id_, local_id_);
        auto ptr = buf + onset;
        aoo::write_bytes<int32_t>(1, ptr);
        aoo::write_bytes<int32_t>(ack.sequence, ptr);
        aoo::write_bytes<int32_t>(ack.frame_index, ptr);

        send(buf, ptr - buf, fn);
    } else {
        char buf[64];
        osc::OutboundPacketStream msg(buf, sizeof(buf));
        msg << osc::BeginMessage(kAooMsgPeerAck)
            << group_id_ << local_id_
            << ack.sequence << ack.frame_index
            << osc::EndMessage;

        send(msg, fn);
    }
}

void peer::handle_osc_message(Client& client, std::string_view pattern,
                              osc::ReceivedMessageArgumentIterator it,
                              int remaining, const ip_address& addr) {
    LOG_DEBUG("AooClient: got OSC message " << pattern << " from " << *this);

    if (pattern == kAooMsgPing) {
        handle_ping(client, it, addr);
    } else if (pattern == kAooMsgPong) {
        handle_pong(client, it, addr);
    } else if (pattern == kAooMsgMessage) {
        handle_client_message(client, it);
    } else if (pattern == kAooMsgAck) {
        handle_ack(client, it, remaining);
    } else {
        LOG_WARNING("AooClient: got unknown message "
                    << pattern << " from " << *this);
    }
}

void peer::handle_bin_message(Client& client, const AooByte *data, AooSize size,
                              int onset, const ip_address& addr) {
    LOG_DEBUG("AooClient: got binary message from " << *this);

    switch (binmsg_cmd(data, size)) {
    case kAooBinMsgCmdMessage:
        handle_client_message(client, data + onset, size - onset);
        break;
    case kAooBinMsgCmdAck:
        handle_ack(client, data + onset, size - onset);
        break;
    default:
        LOG_WARNING("AooClient: got unknown binary message from " << *this);
    }
}

void peer::handle_first_ping(Client &client, const aoo::ip_address& addr) {
    // first ping
#if FORCE_RELAY
    // force relay
    if (!need_relay()){
        return;
    }
#endif
    // Try to find matching address.
    // If we receive a message from a peer behind a symmetric NAT, its IP address
    // will be different from the one we obtained from the server, that's why we're
    // sending and checking the group/user ID in the first place.
    // NB: this only works if *we* are behind a full cone or restricted cone NAT.
    // NB: the address list contains *unmapped* IP addresses.
    if (std::find(addrlist_.begin(), addrlist_.end(), addr.unmapped()) == addrlist_.end()) {
        LOG_WARNING("AooClient: peer " << *this << " is located behind a symmetric NAT!");
    }

    real_address_ = addr;

    connected_.store(true, std::memory_order_release);

    // push event
    auto e = std::make_unique<peer_event>(kAooEventPeerJoin, *this);
    client.send_event(std::move(e));

    LOG_INFO("AooClient: successfully established connection with "
             << *this << " " << addr << (need_relay() ? " (relayed)" : ""));
}

void peer::handle_ping(Client& client, osc::ReceivedMessageArgumentIterator it,
                       const ip_address& addr) {
    if (!connected()) {
        handle_first_ping(client, addr);
    }

    ping_timer_.pong();

    // NB: 'nil' indicates a handshake ping; internally we use an empty timetag as sentinel.
    time_tag tt1 = !it->IsNil() ? it->AsTimeTag() : 0;
    // reply to both handshake and regular pings!!!
    // otherwise, the handshake might fail on the other side.
    // This is not 100% threadsafe, but regular pings will never
    // be sent fast enough to actually cause a race condition.
    // TODO: maybe use a queue instead?
    ping_tt1_ = tt1;
    ping_tt2_ = time_tag::now(); // local receive time
    got_ping_.store(true, std::memory_order_release);
    if (!tt1.is_empty()) {
        LOG_DEBUG("AooClient: got ping from " << *this
                  << " (tt1: " << tt1 << ", tt2: " << ping_tt2_ << ")");
    } else {
        // handshake ping
        LOG_DEBUG("AooClient: got handshake ping from " << *this);
    }
}

void peer::handle_pong(Client& client, osc::ReceivedMessageArgumentIterator it,
                       const ip_address& addr) {
    if (!connected()) {
        handle_first_ping(client, addr);
    }

    ping_timer_.pong();

    // 'nil' indicates a handshake pong
    if (it->IsNil()) {
        // handshake pong
        LOG_DEBUG("AooClient: got handshake pong from " << *this);
    } else {
        // regular pong
        time_tag tt1 = (it++)->AsTimeTag(); // local send time
        time_tag tt2 = (it++)->AsTimeTag(); // remote receive time
        time_tag tt3 = (it++)->AsTimeTag(); // remote send time
        time_tag tt4 = time_tag::now(); // local receive time

        auto rtt = time_tag::duration(tt1, tt4) - time_tag::duration(tt2, tt3);
    #if 1
        // NB: we are the only thread writing to average_rtt_, so we don't need a CAS loop!
        auto avg = average_rtt_.load(std::memory_order_relaxed);
        if (avg > 0) {
            // simple low-pass filtering; maybe use cumulative average instead?
            const float coeff = 0.5;
            auto newval = avg * coeff + rtt * (1.0 - coeff);
            average_rtt_.store(newval);
        } else {
            // first ping
            average_rtt_.store(rtt);
        }
    #else
        average_rtt_.store(rtt);
    #endif

        // only send event for regular pong!
        auto e = std::make_unique<peer_ping_event>(*this, tt1, tt2, tt3, tt4);
        client.send_event(std::move(e));

        LOG_DEBUG("AooClient: got pong from " << *this << " (tt1: " << tt1
                  << ", tt2: " << tt2 << ", tt3: " << tt3 << ", tt4: " << tt4
                  << ", rtt: " << rtt << ", average: " << average_rtt_.load() << ")");
    }
}

void peer::handle_client_message(Client &client, osc::ReceivedMessageArgumentIterator it) {
    auto flags = (AooFlag)(it++)->AsInt32();

    message_packet p;
    p.sequence = (it++)->AsInt32();
    p.total_size = (it++)->AsInt32();
    // frames
    if (!it->IsNil()) {
        p.num_frames = (it++)->AsInt32();
        p.frame_index = (it++)->AsInt32();
    } else {
        p.num_frames = 1; it++;
        p.frame_index = 0; it++;
    }
    if (!it->IsNil()) {
        p.tt = (it++)->AsTimeTag();
    } else {
        p.tt = 0; it++;
    }
    auto data = osc_read_metadata(it);
    if (data) {
        p.type = data->type;
        p.data = data->data;
        p.size = data->size;
    } else {
        throw osc::MalformedMessageException("missing data");
    }

    // tell the send thread that it should answer with OSC messages
    // TODO: should we just use binary_ instead?
    binary_ack_.store(false, std::memory_order_relaxed);

    do_handle_client_message(client, p, flags);
}

void peer::handle_client_message(Client &client, const AooByte *data, AooSize size) {
    auto ptr = data;
    int32_t remaining = size;
    AooFlag flags = 0;
    message_packet p;

    if (remaining < 8) {
        goto bad_message;
    }
    p.sequence = aoo::read_bytes<int32_t>(ptr);
    flags = aoo::read_bytes<uint16_t>(ptr);
    p.size = aoo::read_bytes<uint16_t>(ptr);
    remaining -= 8;

    if (flags & kAooBinMsgMessageFrames) {
        if (remaining < 12) {
            goto bad_message;
        }
        p.total_size = aoo::read_bytes<int32_t>(ptr);
        p.num_frames = aoo::read_bytes<int32_t>(ptr);
        p.frame_index = aoo::read_bytes<int32_t>(ptr);
        remaining -= 12;
    } else {
        p.total_size = p.size;
        p.num_frames = 1;
        p.frame_index = 0;
    }
    if (flags & kAooBinMsgMessageTimestamp) {
        if (remaining < 8) {
            goto bad_message;
        }
        p.tt = aoo::read_bytes<uint64_t>(ptr);
        remaining -= 8;
    } else {
        p.tt = 0;
    }
    p.type = aoo::read_bytes<int32_t>(ptr);
    remaining -= 4;

    if (remaining < p.size) {
        goto bad_message;
    }
    p.data = ptr;

    // tell the send thread that it should answer with binary messages
    // TODO: should we just use binary_ instead?
    binary_ack_.store(true, std::memory_order_relaxed);

    do_handle_client_message(client, p, flags);

    return;

bad_message:
    LOG_ERROR("AooClient: received malformed binary message from " << *this);
}

void peer::do_handle_client_message(Client& client, const message_packet& p, AooFlag flags) {
    if (flags & kAooMessageReliable) {
        // *** reliable message ***
        auto last_pushed = receive_buffer_.last_pushed();
        auto last_popped = receive_buffer_.last_popped();
        if (p.sequence <= last_popped) {
            // outdated message
        #if AOO_DEBUG_CLIENT_MESSAGE
            LOG_DEBUG("AooClient: ignore outdated message (seq: "
                      << p.sequence << ", frame: " << p.frame_index  << ") from " << *this);
        #endif
            // don't forget to acknowledge!
            send_acks_.emplace(p.sequence, p.frame_index);
            return;
        }
        if (p.sequence > last_pushed) {
            // check if we have skipped messages
            // (sequence always starts at 0)
            int32_t skipped = (last_pushed >= 0) ? p.sequence - last_pushed - 1 : p.sequence;
            if (skipped > 0) {
            #if AOO_DEBUG_CLIENT_MESSAGE
                LOG_DEBUG("AooClient: skipped " << skipped << " messages from " << *this);
            #endif
                // insert empty messages
                int32_t onset = (last_pushed >= 0) ? last_pushed + 1 : 0;
                for (int i = 0; i < skipped; ++i) {
                    receive_buffer_.push(received_message(onset + i));
                }
            }
            // add new message
        #if AOO_DEBUG_CLIENT_MESSAGE
            LOG_DEBUG("AooClient: add new message (seq: " << p.sequence
                      << ", frame: " << p.frame_index << ") from " << *this);
        #endif
            auto& msg = receive_buffer_.push(received_message(p.sequence));
            msg.init(p.type, p.tt, p.num_frames, p.total_size);
            msg.add_frame(p.frame_index, p.data, p.size);
        } else {
            // add to existing message
        #if AOO_DEBUG_CLIENT_MESSAGE
            LOG_DEBUG("AooClient: add to existing message (seq: " << p.sequence
                      << ", frame: " << p.frame_index << ") from " << *this);
        #endif
            if (auto msg = receive_buffer_.find(p.sequence)) {
                if (msg->placeholder()) {
                    msg->init(p.type, p.tt, p.num_frames, p.total_size);
                }
                if (!msg->has_frame(p.frame_index)) {
                    msg->add_frame(p.frame_index, p.data, p.size);
                } else {
                #if AOO_DEBUG_CLIENT_MESSAGE
                    LOG_DEBUG("AooClient: ignore duplicate message (seq: " << p.sequence
                              << ", frame: " << p.frame_index << ") from " << *this);
                #endif
                }
            } else {
                LOG_ERROR("AooClient: could not find message (seq: "
                          << p.sequence << ") from " << *this);
            }
        }
        // check for completed messages
        while (!receive_buffer_.empty()) {
            auto& msg = receive_buffer_.front();
            if (msg.complete()) {
                AooData md { msg.type_, msg.data_, (AooSize)msg.size_ };
                auto e = std::make_unique<peer_message_event>(
                            group_id(), user_id(), msg.tt_, md);
                client.send_event(std::move(e));

                receive_buffer_.pop();
            } else {
                break;
            }
        }
        // schedule acknowledgement
        send_acks_.emplace(p.sequence, p.frame_index);
    } else {
        // *** unreliable message ***
        if (p.num_frames > 1) {
            // try to reassemble message
            if (current_msg_.sequence_ != p.sequence) {
            #if AOO_DEBUG_CLIENT_MESSAGE
                LOG_DEBUG("AooClient: new multi-frame message from " << *this);
            #endif
                // start new message (any incomplete previous message is discarded!)
                current_msg_ = received_message(p.sequence);
                current_msg_.init(p.type, p.tt, p.num_frames, p.total_size);
            }
            current_msg_.add_frame(p.frame_index, p.data, p.size);
            if (current_msg_.complete()) {
                AooData d { current_msg_.type_, current_msg_.data_, (AooSize)current_msg_.size_ };
                auto e = std::make_unique<peer_message_event>(
                            group_id(), user_id(), p.tt, d);
                client.send_event(std::move(e));
            }
        } else {
            // output immediately
            AooData d { p.type, p.data, (AooSize)p.size };
            auto e = std::make_unique<peer_message_event>(
                        group_id(), user_id(), p.tt, d);
            client.send_event(std::move(e));
        }
    }
}

void peer::handle_ack(Client &client, osc::ReceivedMessageArgumentIterator it, int remaining) {
    for (; remaining >= 2; remaining -= 2) {
        auto seq = (it++)->AsInt32();
        auto frame = (it++)->AsInt32();
    #if AOO_DEBUG_CLIENT_MESSAGE
        LOG_DEBUG("AooClient: got ack (seq: " << seq
                  << ", frame: " << frame << ") from " << *this);
    #endif
        received_acks_.emplace(seq, frame);
    }
}

void peer::handle_ack(Client &client, const AooByte *data, AooSize size) {
    auto ptr = data;
    if (size >= 4) {
        auto count = aoo::read_bytes<int32_t>(ptr);
        if ((size - 4) >= (count * 8)) {
            while (count--) {
                auto seq = aoo::read_bytes<int32_t>(ptr);
                auto frame = aoo::read_bytes<int32_t>(ptr);
            #if AOO_DEBUG_CLIENT_MESSAGE
                LOG_DEBUG("AooClient: got ack (seq: " << seq
                          << ", frame: " << frame << ") from " << *this);
            #endif
                received_acks_.emplace(seq, frame);
            }
            return; // done
        }
    }
    LOG_ERROR("AooClient: got malformed binary ack message from " << *this);
}

void peer::send(const AooByte *data, AooSize size,
                const ip_address &addr, const sendfn &fn) const {
    if (need_relay()) {
    #if AOO_DEBUG_RELAY
        if (binmsg_check(data, size)) {
            LOG_DEBUG("AooClient: relay binary message to peer " << *this);
        } else {
            LOG_DEBUG("AooClient: relay message " << (const char *)data << " to peer " << *this);
        }
    #endif
        if (binary_) {
            AooByte buf[AOO_MAX_PACKET_SIZE];
            auto result = write_relay_message(buf, sizeof(buf), data, size, addr, binary_);

            fn(buf, result, relay_address_);
        } else {
            // don't prepend size for UDP message!
            char buf[AOO_MAX_PACKET_SIZE];
            osc::OutboundPacketStream out(buf, sizeof(buf));
            out << osc::BeginMessage(kAooMsgDomain kAooMsgRelay)
                << addr << osc::Blob(data, size)
                << osc::EndMessage;

            fn((const AooByte*)out.Data(), out.Size(), relay_address_);
        }
    } else {
        fn(data, size, addr);
    }
}

} // net
} // aoo
