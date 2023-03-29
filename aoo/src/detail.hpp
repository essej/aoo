#pragma once

#include "aoo/aoo_codec.h"
#include "aoo/aoo_config.h"
#include "aoo/aoo_defines.h"
#if AOO_NET
# include "aoo/aoo_requests.h"
#endif
#include "aoo/aoo_types.h"

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

//--------------- helper functions ----------------//

AooId get_random_id();

uint32_t make_version();

bool check_version(uint32_t version);

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

//---------------- IP address ----------------------//

inline osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const ip_address& addr) {
    // send *unmapped* addresses in case the client is IPv4 only
    if (addr.valid()) {
        msg << addr.name_unmapped() << (int32_t)addr.port();
    } else {
        msg << "" << (int32_t)0;
    }
    return msg;
}

inline ip_address osc_read_address(osc::ReceivedMessageArgumentIterator& it,
                                   ip_address::ip_type type = ip_address::Unspec) {
    auto host = (it++)->AsString();
    auto port = (it++)->AsInt32();
    return ip_address(host, port, type);
}

//---------------- endpoint ------------------------//

struct endpoint {
    endpoint() = default;
    endpoint(const ip_address& _address, int32_t _id)
        : address(_address), id(_id) {}
#if AOO_NET
    endpoint(const ip_address& _address, int32_t _id, const ip_address& _relay)
        : address(_address), relay(_relay), id(_id) {}
#endif
    // data
    ip_address address;
#if AOO_NET
    ip_address relay;
#endif
    AooId id = 0;

    void send(const osc::OutboundPacketStream& msg, const sendfn& fn) const {
        send((const AooByte *)msg.Data(), msg.Size(), fn);
    }
#if AOO_NET
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

#if AOO_NET
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

//---------------- codec ---------------------------//

const struct AooCodecInterface * find_codec(const char * name);

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

//------------------- format -------------------------//

struct format_deleter {
    void operator() (void *x) const {
        auto f = static_cast<AooFormat *>(x);
        aoo::deallocate(x, f->structSize);
    }
};

struct rt_format_deleter {
    void operator() (void *x) const {
        auto f = static_cast<AooFormat *>(x);
        aoo::rt_deallocate(x, f->structSize);
    }
};

//---------------- metadata -----------------------//

struct metadata {
    metadata() = default;
    metadata(const AooData* md) {
        if (md) {
            type_ = md->type;
            data_.assign(md->data, md->data + md->size);
        }
    }
    AooDataType type() const { return type_; }
    const AooByte *data() const { return data_.data(); }
    AooSize size() const { return data_.size(); }
private:
    AooDataType type_ = kAooDataUnspecified;
    std::vector<AooByte> data_;
};


struct metadata_view {
    metadata_view() :
        type(kAooDataUnspecified), data(nullptr), size(0) {}
    metadata_view(const aoo::metadata& md)
        : type(md.type()), data(md.data()), size(md.size()) {}
    metadata_view (const AooData *md)
        : type(md ? md->type : kAooDataUnspecified),
          data(md ? md->data : nullptr),
          size(md ? md->size : 0) {}

    AooDataType type;
    const AooByte *data;
    AooSize size;
};

inline osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const metadata_view& md) {
    if (md.size > 0) {
        msg << md.type << osc::Blob(md.data, md.size);
    } else {
        msg << kAooDataUnspecified << osc::Blob(nullptr, 0);
    }
    return msg;
}

inline AooData osc_read_metadata(osc::ReceivedMessageArgumentIterator& it) {
    auto type = (it++)->AsInt32();
    const void *blobdata;
    osc::osc_bundle_element_size_t blobsize;
    (it++)->AsBlob(blobdata, blobsize);
    if (blobsize) {
        return AooData { type, (const AooByte *)blobdata, (AooSize)blobsize };
    } else {
        return AooData { type, nullptr, 0 };
    }
}

inline AooSize flat_metadata_size(const AooData& data){
    return sizeof(data) + data.size;
}

struct flat_metadata_deleter {
    void operator() (void *x) const {
        auto md = static_cast<AooData *>(x);
        auto size = flat_metadata_size(*md);
        aoo::deallocate(x, size);
    }
};

using metadata_ptr = std::unique_ptr<AooData, flat_metadata_deleter>;

struct rt_flat_metadata_deleter {
    void operator() (void *x) const {
        auto md = static_cast<AooData *>(x);
        auto size = flat_metadata_size(*md);
        aoo::rt_deallocate(x, size);
    }
};

using rt_metadata_ptr = std::unique_ptr<AooData, rt_flat_metadata_deleter>;

inline void flat_metadata_copy(const AooData& src, AooData& dst) {
    auto data = reinterpret_cast<AooByte *>(&dst) + sizeof(dst);
    memcpy(data, src.data, src.size);
    dst.type = src.type;
    dst.size = src.size;
    dst.data = data;
}

} // aoo
