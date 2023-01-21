#include "detail.hpp"

#include "../binmsg.hpp"
#include "md5/md5.h"

namespace aoo {
namespace net {

//------------------------------ OSC utilities ---------------------------------//

// see comment in "detail.hpp"
osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const AooDataView *md) {
    if (md) {
        msg << md->type << osc::Blob(md->data, md->size);
    } else {
        msg << "" << osc::Blob(msg.Data(), 0); // HACK: do not use nullptr because of memcpy()
    }
    return msg;
}

osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const ip_host& addr) {
    msg << addr.name.c_str() << addr.port;
    return msg;
}

ip_host osc_read_host(osc::ReceivedMessageArgumentIterator& it) {
    auto host = (it++)->AsString();
    auto port = (it++)->AsInt32();
    return net::ip_host { host, port };
}

//------------------------ misc ---------------------------//

std::string encrypt(const std::string& input) {
    // pass empty password unchanged
    if (input.empty()) {
        return "";
    }

    uint8_t result[16];
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, (uint8_t *)input.data(), input.size());
    MD5_Final(result, &ctx);

    char output[33];
    for (int i = 0; i < 16; ++i){
        snprintf(&output[i * 2], 3, "%02X", result[i]);
    }

    return output;
}

// optimized version of aoo_parse_pattern() for client/server
AooError parse_pattern(const AooByte *msg, int32_t n, AooMsgType& type, int32_t& offset)
{
    int32_t count = 0;
    if (aoo::binmsg_check(msg, n))
    {
        type = aoo::binmsg_type(msg, n);
        offset = aoo::binmsg_headersize(msg, n);
        return kAooOk;
    } else if (n >= kAooMsgDomainLen
            && !memcmp(msg, kAooMsgDomain, kAooMsgDomainLen))
    {
        count += kAooMsgDomainLen;
        if (n >= (count + kAooNetMsgServerLen)
            && !memcmp(msg + count, kAooNetMsgServer, kAooNetMsgServerLen))
        {
            type = kAooTypeServer;
            count += kAooNetMsgServerLen;
        }
        else if (n >= (count + kAooNetMsgClientLen)
            && !memcmp(msg + count, kAooNetMsgClient, kAooNetMsgClientLen))
        {
            type = kAooTypeClient;
            count += kAooNetMsgClientLen;
        }
        else if (n >= (count + kAooNetMsgPeerLen)
            && !memcmp(msg + count, kAooNetMsgPeer, kAooNetMsgPeerLen))
        {
            type = kAooTypePeer;
            count += kAooNetMsgPeerLen;
        }
        else if (n >= (count + kAooNetMsgRelayLen)
            && !memcmp(msg + count, kAooNetMsgRelay, kAooNetMsgRelayLen))
        {
            type = kAooTypeRelay;
            count += kAooNetMsgRelayLen;
        } else {
            return kAooErrorUnknown;
        }

        offset = count;

        return kAooOk;
    } else {
        return kAooErrorUnknown; // not an AOO message
    }
}

AooSize write_relay_message(AooByte *buffer, AooSize bufsize,
                            const AooByte *msg, AooSize msgsize,
                            const ip_address& addr) {
    if (aoo::binmsg_check(msg, msgsize)) {
    #if AOO_DEBUG_RELAY && 0
        LOG_DEBUG("write binary relay message");
    #endif
        auto onset = aoo::binmsg_write_relay(buffer, bufsize, addr);
        auto total = msgsize + onset;
        // NB: we do not write the message size itself because it is implicit
        if (bufsize >= total) {
            memcpy(buffer + onset, msg, msgsize);
            return total;
        } else {
            return 0;
        }
    } else {
    #if AOO_DEBUG_RELAY && 0
        LOG_DEBUG("write OSC relay message " << msg);
    #endif
        osc::OutboundPacketStream msg2((char *)buffer, bufsize);
        msg2 << osc::BeginMessage(kAooMsgDomain kAooNetMsgRelay)
             << addr << osc::Blob(msg, msgsize)
             << osc::EndMessage;
        return msg2.Size();
    }
}

} // net
} // aoo
