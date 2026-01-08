#pragma once

#include "aoo_codec.h"
#include "aoo_config.h"
#include "aoo_defines.h"
#include "aoo_types.h"

#include "memory.hpp"

#include "common/net_utils.hpp"
#include "common/lockfree.hpp"
#include "common/log.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "osc/OscReceivedElements.h"
#include "osc/OscOutboundPacketStream.h"

#include <array>
#include <cstring>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <optional>
#include <vector>
#include <utility>

namespace aoo {

template<typename T>
using parameter = sync::relaxed_atomic<T>;

//--------------- helper functions ----------------//

AooId get_random_id();

uint32_t make_version();

AooError check_version(const char *version);

//------------- common data structures ------------//

template<typename T>
using vector = std::vector<T, aoo::allocator<T>>;

using string = std::basic_string<char, std::char_traits<char>, aoo::allocator<char>>;

template<typename T>
using spsc_queue = lockfree::spsc_queue<T, aoo::allocator<T>>;

template<typename T, bool multi_producer=true>
using concurrent_queue = lockfree::concurrent_queue<T, multi_producer, aoo::allocator<T>>;

template<typename T>
using concurrent_list = lockfree::concurrent_list<T, aoo::allocator<T>>;

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

//--------------- ip_address -----------------------//

#if 0
using ip_address_list = std::vector<ip_address, aoo::allocator<ip_address>>;
#else
using ip_address_list = std::vector<ip_address>;
#endif

inline osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const ip_address& addr) {
    msg << addr.name() << (int32_t)addr.port();
    return msg;
}

inline ip_address osc_read_address(osc::ReceivedMessageArgumentIterator& it) {
    auto hostname = (it++)->AsString();
    auto port = (it++)->AsInt32();
    return ip_address(hostname, port);
}

//---------------- endpoint ------------------------//

struct endpoint {
    endpoint() = default;
    endpoint(const ip_address& _address, int32_t _id, bool _binary)
        : address(_address), id(_id), binary(_binary) {}
#if AOO_NET
    endpoint(const ip_address& _address, const ip_address& _relay,
             int32_t _id, bool _binary)
        : address(_address), relay(_relay), id(_id), binary(_binary) {}
#endif
    // data
    ip_address address;
#if AOO_NET
    ip_address relay;
#endif
    AooId id = 0;
    bool binary = false;

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
AooSize write_relay_message(AooByte *buffer, AooSize bufsize, const AooByte *msg,
                            AooSize msgsize, const ip_address& addr, bool binary);
} // net

inline void endpoint::send(const AooByte *data, AooSize size, const sendfn& fn) const {
    if (relay.valid()) {
    #if AOO_DEBUG_RELAY
        LOG_DEBUG("relay message to " << *this << " via " << relay);
    #endif
        AooByte buffer[AOO_MAX_PACKET_SIZE];
        auto result = net::write_relay_message(buffer, sizeof(buffer),
                                               data, size, address, binary);
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
    metadata(AooDataType type, const AooByte *data, AooSize size)
        : type_(type), data_(data, data + size) {}
    metadata(const AooData* md) {
        if (md) {
            type_ = md->type;
            data_.assign(md->data, md->data + md->size);
        }
    }

    AooDataType type() const { return type_; }
    const AooByte *data() const { return data_.data(); }
    AooSize size() const { return data_.size(); }

    bool valid() const {
        return type_ != kAooDataUnspecified;
    }
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
        : type(md ? md->type : (AooDataType)kAooDataUnspecified),
          data(md ? md->data : nullptr),
          size(md ? md->size : 0) {}

    AooDataType type;
    const AooByte *data;
    AooSize size;
};

inline osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const metadata_view& md) {
    if (md.type != kAooDataUnspecified) {
        msg << md.type << osc::Blob(md.data, md.size);
    } else {
        msg << osc::Nil << osc::Nil;
    }
    return msg;
}

inline std::optional<AooData> osc_read_metadata(osc::ReceivedMessageArgumentIterator& it) {
    if (it->IsNil()) {
        it++; it++;
        return std::nullopt;
    } else {
        auto type = (it++)->AsInt32();
        const void *blobdata;
        osc::osc_bundle_element_size_t blobsize;
        (it++)->AsBlob(blobdata, blobsize);
        if (blobsize > 0) {
            return AooData { type, (const AooByte *)blobdata, (AooSize)blobsize };
        } else {
            // TODO: are there legitimate use cases for empty metdata?
            throw osc::MalformedMessageException("metadata with empty content");
        }
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
