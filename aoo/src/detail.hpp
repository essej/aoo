#pragma once

#include "aoo/aoo.h"
#if USE_AOO_NET
# include "aoo/aoo_net.h"
#endif
#include "aoo/aoo_codec.h"

#include "memory.hpp"

#include "common/net_utils.hpp"
#include "common/lockfree.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "oscpack/osc/OscReceivedElements.h"
#include "oscpack/osc/OscOutboundPacketStream.h"

#include <stdint.h>
#include <cstring>
#include <utility>
#include <vector>
#include <string>
#include <type_traits>

namespace aoo {

template<typename T>
using parameter = sync::relaxed_atomic<T>;

//------------------ OSC ---------------------------//

osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const ip_address& addr);

ip_address osc_read_address(osc::ReceivedMessageArgumentIterator& it, ip_address::ip_type type = ip_address::Unspec);

//---------------- codec ---------------------------//

const struct AooCodecInterface * find_codec(const char * name);

//--------------- helper functions ----------------//

AooId get_random_id();

uint32_t make_version();

bool check_version(uint32_t version);

//-------------------- sendfn-------------------------//

struct sendfn {
    sendfn(AooSendFunc fn = nullptr, void *user = nullptr)
        : fn_(fn), user_(user) {}

    void operator() (const AooByte *data, AooInt32 size,
                     const ip_address& addr, AooFlag flags = 0) const {
        fn_(user_, data, size, addr.address(), addr.length(), flags);
    }

    AooSendFunc fn() const { return fn_; }

    void * user() const { return user_; }
private:
    AooSendFunc fn_;
    void *user_;
};

//---------------- endpoint ------------------------//

struct endpoint {
    endpoint() = default;
    endpoint(const ip_address& _address, int32_t _id)
        : address(_address), id(_id) {}
#if USE_AOO_NET
    endpoint(const ip_address& _address, int32_t _id, const ip_address& _relay)
        : address(_address), relay(_relay), id(_id) {}
#endif
    // data
    ip_address address;
#if USE_AOO_NET
    ip_address relay;
#endif
    AooId id = 0;

    void send(const osc::OutboundPacketStream& msg, const sendfn& fn) const {
        send((const AooByte *)msg.Data(), msg.Size(), fn);
    }
#if USE_AOO_NET
    void send(const AooByte *data, AooSize size, const sendfn& fn) const;
#else
    void send(const AooByte *data, AooSize size, const sendfn& fn) const {
        fn(data, size, address, 0);
    }
#endif
};

inline std::ostream& operator<<(std::ostream& os, const endpoint& ep){
    os << ep.address << "|" << ep.id;
    return os;
}

#if USE_AOO_NET
namespace net {
AooSize write_relay_message(AooByte *buffer, AooSize bufsize,
                            const AooByte *msg, AooSize msgsize,
                            const ip_address& addr);
} // net

inline void endpoint::send(const AooByte *data, AooSize size, const sendfn& fn) const {
    if (relay.valid()) {
    #if AOO_DEBUG_RELAY
        LOG_DEBUG("relay message to " << *this << " via " << relay);
    #endif
        AooByte buffer[AOO_MAX_PACKET_SIZE];
        auto result = net::write_relay_message(buffer, sizeof(buffer),
                                               data, size, address);
        if (result > 0) {
            fn(buffer, result, relay, 0);
        } else {
            LOG_ERROR("can't relay binary message: buffer too small");
        }
    } else {
        fn(data, size, address, 0);
    }
}
#endif



//------------- common data structures ------------//

template<typename T>
using vector = std::vector<T, aoo::allocator<T>>;

using string = std::basic_string<char, std::char_traits<char>, aoo::allocator<char>>;

template<typename T>
using spsc_queue = lockfree::spsc_queue<T, aoo::allocator<T>>;

template<typename T>
using unbounded_mpsc_queue = lockfree::unbounded_mpsc_queue<T, aoo::allocator<T>>;

template<typename T>
using rcu_list = lockfree::rcu_list<T, aoo::allocator<T>>;

//------------------- format -------------------------//

struct format_deleter {
    void operator() (void *x) const {
        auto f = static_cast<AooFormat *>(x);
        aoo::deallocate(x, f->size);
    }
};

//------------------- codec -----------------------//

struct encoder_deleter {
    void operator() (void *x) const {
        auto c = (AooCodec *)x;
        c->cls->encoderFree(c);
    }
};

struct decoder_deleter {
    void operator() (void *x) const {
        auto c = (AooCodec *)x;
        c->cls->decoderFree(c);
    }
};

//---------------- metadata -----------------------//

struct metadata {
    metadata() = default;
    metadata(const AooDataView* md) {
        if (md) {
            type_ = md->type;
            data_.assign(md->data, md->data + md->size);
        }
    }
    const char *type() const { return type_.c_str(); }
    const AooByte *data() const { return data_.data(); }
    AooSize size() const { return data_.size(); }
private:
    std::string type_;
    std::vector<AooByte> data_;
};

// HACK: declare the AooDataView overload in "net" namespace and then import into "aoo"
// namespace to prevent the compiler from picking OutboundPacketStream::operator<<bool
namespace net {
osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const AooDataView *md);
} // net
osc::OutboundPacketStream& net::operator<<(osc::OutboundPacketStream& msg, const AooDataView *md);

osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const aoo::metadata& md);

AooDataView osc_read_metadata(osc::ReceivedMessageArgumentIterator& it);

inline AooSize flat_metadata_maxsize(int32_t size) {
    return sizeof(AooDataView) + size + kAooDataTypeMaxLen + 1;
}

inline AooSize flat_metadata_size(const AooDataView& data){
    return sizeof(data) + data.size + strlen(data.type) + 1;
}

struct flat_metadata_deleter {
    void operator() (void *x) const {
        auto md = static_cast<AooDataView *>(x);
        auto mdsize = flat_metadata_size(*md);
        aoo::deallocate(x, mdsize);
    }
};

inline void flat_metadata_copy(const AooDataView& src, AooDataView& dst) {
    auto data = (AooByte *)(&dst) + sizeof(AooDataView);
    memcpy(data, src.data, src.size);

    auto type = (AooChar *)(data + src.size);
    memcpy(type, src.type, strlen(src.type) + 1);

    dst.type = type;
    dst.data = data;
    dst.size = src.size;
}

} // aoo
