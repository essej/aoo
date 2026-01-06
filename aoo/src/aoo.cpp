#include "aoo.h"
#if AOO_NET
# include "aoo_requests.h"
# include "common/net_utils.hpp"
#endif
#include "aoo_codec.h"

#include "binmsg.hpp"
#include "detail.hpp"
#include "rt_memory_pool.hpp"

#include "common/log.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "codec/aoo_pcm.h"
#if AOO_USE_OPUS
# include "codec/aoo_opus.h"
#endif

#if AOO_NET
#include "net/detail.hpp"
#endif

#define CERR_LOG_FUNCTION 1
#if CERR_LOG_FUNCTION
# include <cstdio>
# include <cstdarg>
# define CERR_LOG_MUTEX 0
# define CERR_LOG_LABEL 1
#endif // CERR_LOG_FUNCTION

#include <atomic>
#include <random>
#include <unordered_map>

#ifdef ESP_PLATFORM
# include "esp_system.h"
#endif

//--------------------- interface table -------------------//

namespace aoo {
namespace {

void * AOO_CALL default_allocator(void *ptr, AooSize oldsize, AooSize newsize);

void AOO_CALL default_logfunc(AooLogLevel level, const char *message);

// populate interface table with default implementations
static AooCodecHostInterface g_interface = {
    AOO_STRUCT_SIZE(AooCodecHostInterface, log),
    aoo_registerCodec,
    default_allocator,
    default_logfunc
};

} // namespace
} // aoo

//--------------------- helper functions -----------------//

namespace aoo {

int32_t get_random_id(){
#if defined(ESP_PLATFORM)
    // use ESP hardware RNG
    return esp_random() & 0x7fffffff;
#else
    // software PRNG
    static std::random_device rd;
#if 0
    // WARNING: needs lots of memory!
    thread_local std::mt19937 eng(rd());
#else
    // good enough for our purposes
    thread_local std::minstd_rand eng(rd());
#endif
    std::uniform_int_distribution<int32_t> dist;
    return dist(eng);
#endif
}

} // namespace "aoo"

//----------------------- logging --------------------------//

namespace aoo {
namespace {

#if CERR_LOG_FUNCTION

#if CERR_LOG_MUTEX
sync::mutex g_log_mutex;
#endif

void AOO_CALL default_logfunc(AooLogLevel level, const char *message) {
    const char *label = nullptr;

#if CERR_LOG_LABEL
    switch (level) {
    case kAooLogLevelError:
        label = "error";
        break;
    case kAooLogLevelWarning:
        label = "warning";
        break;
    case kAooLogLevelInfo:
        label = "info";
        break;
    case kAooLogLevelDebug:
        label = "debug";
        break;
    case kAooLogLevelVerbose:
        label = "verbose";
        break;
    default:
        break;
    }
#endif

    const auto size = aoo::Log::buffer_size + 12;
    char buffer[size];
    int count;
    if (label) {
        count = snprintf(buffer, size, "[aoo][%s] %s\n", label, message);
    } else {
        count = snprintf(buffer, size, "[aoo] %s\n", message);
    }

#if CERR_LOG_MUTEX
    // shouldn't be necessary since fwrite() is supposed
    // to be atomic.
    sync::scoped_lock<sync::mutex> lock(g_log_mutex);
#endif
    fwrite(buffer, count, 1, stderr);
    fflush(stderr);
}

#else // CERR_LOG_FUNCTION

#ifndef _MSC_VER
void __attribute__((format(printf, 2, 3 )))
#else
void
#endif
    default_logfunc(AooLogLevel, const char *, ...) {}

#endif // CERR_LOG_FUNCTION

} // namespace
} // aoo

AOO_API AooError AOO_CALL aoo_logMessage(AooLogLevel level, const AooChar *msg) {
    if (aoo::g_interface.log) {
        aoo::g_interface.log(level, msg);
    }
    return kAooOk;
}

//----------------------- aoo_strerror ------------------------//

namespace {

std::array g_error_names {
    "no error",
    "not implemented",
    "not permitted",
    "not initialized",
    "bad argument",
    "bad format",
    "out of range",
    "idle",
    "would block",
    "would overflow",
    "timed out",
    "out of memory",
    "already exists",
    "not found",
    "insufficient buffer",
    "bad state",
    "socket error",
    "codec error",
    "internal error",
    "system error",
    "user-defined error"
};

static_assert(g_error_names.size() == kAooErrorUserDefined + 1,
              "errors are missing");

std::array g_server_error_names {
    "request in progress",
    "unhandled request",
    "version not supported",
    "UDP handshake time out",
    "wrong password",
    "already connected",
    "not connected",
    "group does not exist",
    "cannot create group",
    "already a group member",
    "not a group member",
    "user already exists",
    "user does not exist",
    "cannot create user",
    "not responding"
};

static_assert(g_server_error_names.size() == kAooErrorNotResponding + 1 - 1000,
              "errors are missing");

} // namespace

const char *aoo_strerror(AooError e) {
    if (e < 0) {
        return "unspecified";
    } else if (e < g_error_names.size()) {
        // generic error
        return g_error_names[e];
    } else if (auto e2 = e - 1000; e2 >= 0 && e2 < g_server_error_names.size()) {
        // server error
        return g_server_error_names[e2];
    } else if (e >= kAooErrorCustom) {
        return "custom error";
    } else {
        return "";
    }
}

//----------------------- OSC --------------------------//

AOO_API AooError AOO_CALL aoo_parsePattern(
        const AooByte *msg, AooInt32 size,
        AooMsgType *type, AooId *id, AooInt32 *offset) {
    int32_t count = 0;
    if (aoo::binmsg_check(msg, size)) {
        *type = aoo::binmsg_type(msg, size);
        *id = aoo::binmsg_to(msg, size);
        auto n = aoo::binmsg_headersize(msg, size);
        if (n > 0) {
            if (offset) {
                *offset = n;
            }
            return kAooOk;
        } else {
            return kAooErrorBadArgument;
        }
    } else if (size >= kAooMsgDomainLen
        && !memcmp(msg, kAooMsgDomain, kAooMsgDomainLen)) {
        count += kAooMsgDomainLen;
        if (size >= (count + kAooMsgSourceLen)
            && !memcmp(msg + count, kAooMsgSource, kAooMsgSourceLen)) {
            *type = kAooMsgTypeSource;
            count += kAooMsgSourceLen;
        } else if (size >= (count + kAooMsgSinkLen)
            && !memcmp(msg + count, kAooMsgSink, kAooMsgSinkLen)) {
            *type = kAooMsgTypeSink;
            count += kAooMsgSinkLen;
        } else {
        #if AOO_NET
            if (size >= (count + kAooMsgClientLen)
                && !memcmp(msg + count, kAooMsgClient, kAooMsgClientLen)) {
                *type = kAooMsgTypeClient;
                count += kAooMsgClientLen;
            } else if (size >= (count + kAooMsgServerLen)
                && !memcmp(msg + count, kAooMsgServer, kAooMsgServerLen)) {
                *type = kAooMsgTypeServer;
                count += kAooMsgServerLen;
            } else if (size >= (count + kAooMsgPeerLen)
                && !memcmp(msg + count, kAooMsgPeer, kAooMsgPeerLen)) {
                *type = kAooMsgTypePeer;
                count += kAooMsgPeerLen;
            } else if (size >= (count + kAooMsgRelayLen)
                && !memcmp(msg + count, kAooMsgRelay, kAooMsgRelayLen)) {
                *type = kAooMsgTypeRelay;
                count += kAooMsgRelayLen;
            } else {
                return kAooErrorBadArgument;
            }

            if (offset) {
                *offset = count;
            }
        #endif // AOO_NET

            return kAooOk;
        }

        // /aoo/source or /aoo/sink
        if (id) {
            int skip = 0; // 'int' for sscanf
            if (sscanf((const char *)(msg + count), "/%d%n", (int *)id, &skip) > 0) {
                count += skip;
            } else {
                // TODO only print relevant part of OSC address string
                LOG_ERROR("aoo_parse_pattern: bad ID " << (msg + count));
                return kAooErrorBadArgument;
            }
        } else {
            return kAooErrorBadArgument;
        }

        if (offset) {
            *offset = count;
        }
        return kAooOk;
    } else {
        return kAooErrorBadArgument; // not an AOO message
    }
}

//--------------------- relay ------------------------------//

#if AOO_NET

AOO_API AooError aoo_handleRelayMessage(
    const AooByte *data, AooInt32 size,
    const void *address, AooAddrSize addrlen,
    AooSendFunc sendFunc, void *userData, AooSocketFlags socketType)
{
    using namespace aoo;

    if (!(socketType & (kAooSocketIPv4 | kAooSocketIPv6))) {
        // must specify socket type!
        return kAooErrorBadArgument;
    }

    ip_address src_addr((const struct sockaddr*)address, addrlen);
    src_addr.unmap(); // unmap address!

    // check if the embedded destination address is compatible with the given socket type.
    // TODO: handle true dual stack (IPv4 socket + IPv6 socket)
    auto check_addr = [&](ip_address& addr) {
        assert(addr.valid());

        if (addr.is_ipv4_mapped()) {
            LOG_DEBUG("aoo_handleRelayMessage: relay destination must not be IPv4-mapped");
            return false;
        }

        if (addr.type() == ip_address::IPv6) {
            if (socketType & kAooSocketIPv6) {
                return true;
            } else {
                // cannot relay to IPv6 address with IPv4-only socket
                LOG_DEBUG("aoo_handleRelayMessage: cannot relay to destination address " << addr);
                return false;
            }
        } else {
            assert(addr.type() == ip_address::IPv4);
            if (socketType & kAooSocketIPv4) {
                return true;
            } else if (socketType & kAooSocketIPv4Mapped) {
                // map IPv4 address to to IPv6
                addr = addr.ipv4_mapped();
                return true;
            } else {
                // cannot relay to IPv4 address with IPv6-only socket
                LOG_DEBUG("aoo_handleRelayMessage: cannot relay to destination address " << addr);
                return false;
            }
        }
    };

    if (binmsg_check(data, size)) {
        // a) binary relay message

        if (binmsg_type(data, size) != kAooMsgTypeRelay) {
            return kAooErrorBadFormat;
        }

        ip_address dst_addr;
        auto offset = binmsg_read_relay(data, size, dst_addr);
        if (offset == 0) {
            return kAooErrorBadFormat;
        }
        if (!check_addr(dst_addr)) {
            return kAooErrorNotPermitted;
        }

        if (src_addr.type() == dst_addr.type()) {
            // simply replace the header (= rewrite address)
            // NB: we know that the buffer is not really constant
            binmsg_write_relay(const_cast<AooByte *>(data), size, src_addr);

            return sendFunc(userData, data, size,
                            dst_addr.address(), dst_addr.length(), 0);
        } else {
            // rewrite whole message
            AooByte buffer[AOO_MAX_PACKET_SIZE];

            auto result = net::write_relay_message(buffer, sizeof(buffer),
                                                   data + offset, size - offset,
                                                   src_addr, true);
            if (result == 0) {
                return kAooErrorInsufficientBuffer;
            }

            return sendFunc(userData, buffer, result,
                            dst_addr.address(), dst_addr.length(), 0);
        }
    } else if (auto count = kAooMsgDomainLen + kAooMsgRelayLen;
               size >= count && !memcmp(data, kAooMsgDomain kAooMsgRelay, count)) {
        try {
            osc::ReceivedPacket packet((const char *)data, size);
            if (!packet.IsMessage()) {
                return kAooErrorBadFormat;
            }
            osc::ReceivedMessage inmsg(packet);
            auto it = inmsg.ArgumentsBegin();
            auto dst_addr = osc_read_address(it);
            if (!check_addr(dst_addr)) {
                return kAooErrorNotPermitted;
            }

            const void *msgData;
            osc::osc_bundle_element_size_t msgSize;
            (it++)->AsBlob(msgData, msgSize);

            AooByte buffer[AOO_MAX_PACKET_SIZE];
            osc::OutboundPacketStream outmsg((char*)buffer, sizeof(buffer));
            outmsg << osc::BeginMessage(kAooMsgDomain kAooMsgRelay)
                   << src_addr << osc::Blob(msgData, msgSize)
                   << osc::EndMessage;

            return sendFunc(userData, buffer, outmsg.Size(),
                            dst_addr.address(), dst_addr.length(), 0);
        } catch (const osc::Exception& e) {
            LOG_ERROR("aoo_handleRelayMessage: OSC exception: " << e.what());
            return kAooErrorBadFormat;
        }
    } else {
        return kAooErrorBadFormat;
    }
}

#endif // AOO_NET

//-------------------- NTP time ----------------------------//

AOO_API AooNtpTime AOO_CALL aoo_getCurrentNtpTime(void) {
    return aoo::time_tag::now();
}

AOO_API AooSeconds AOO_CALL aoo_ntpTimeToSeconds(AooNtpTime t){
    return aoo::time_tag(t).to_seconds();
}

AOO_API AooNtpTime AOO_CALL aoo_ntpTimeFromSeconds(AooSeconds s){
    return aoo::time_tag::from_seconds(s);
}

AOO_API AooSeconds AOO_CALL aoo_ntpTimeDuration(AooNtpTime t1, AooNtpTime t2){
    return aoo::time_tag::duration(t1, t2);
}

//---------------------- version -------------------------//

AOO_API void AOO_CALL aoo_getVersion(AooInt32 *major, AooInt32 *minor,
                            AooInt32 *patch, AooInt32 *test){
    if (major) *major = kAooVersionMajor;
    if (minor) *minor = kAooVersionMinor;
    if (patch) *patch = kAooVersionPatch;
    if (test) *test = kAooVersionTest;
}

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

const char *aoo_getVersionString() {
    return STR(kAooVersionMajor) "." STR(kAooVersionMinor)
    #if kAooVersionPatch > 0
        "." STR(kAooVersionPatch)
    #endif
    #if kAooVersionTest > 0
       "-test" STR(kAooVersionTest)
    #endif
        ;
}

//---------------------- AooData ----------------------//

namespace {

std::unordered_map<std::string_view, AooDataType> g_data_type_map = {
    { "unspecified", kAooDataUnspecified },
    { "raw", kAooDataRaw },
    { "binary", kAooDataBinary },
    { "text", kAooDataText },
    { "osc", kAooDataOSC },
    { "midi", kAooDataMIDI },
    { "fudi", kAooDataFUDI },
    { "json", kAooDataJSON },
    { "xml", kAooDataXML },
    { "float32", kAooDataFloat32 },
    { "float64", kAooDataFloat64 },
    { "int16", kAooDataInt16 },
    { "int32", kAooDataInt32 },
    { "int64", kAooDataInt64 }
};

std::array g_data_type_names {
    "raw", // same as "binary"!
    "text",
    "osc",
    "midi",
    "fudi",
    "json",
    "xml",
    "float32",
    "float64",
    "int16",
    "int32",
    "int64"
};

static_assert(g_data_type_names.size() == kAooDataInt64 + 1,
              "missing data type");

} // namespace

AooDataType AOO_CALL aoo_dataTypeFromString(const AooChar *str) {
    auto it = g_data_type_map.find(str);
    if (it != g_data_type_map.end()) {
        return it->second;
    } else {
        return kAooDataUnspecified;
    }
}

AOO_API const AooChar * AOO_CALL aoo_dataTypeToString(AooDataType type) {
    if (type < 0) {
        return "unspecified";
    } else if (type < g_data_type_names.size()) {
        return g_data_type_names[type];
    } else if (type >= kAooDataUser) {
        return "user";
    } else {
        return "";
    }
}

//------------------------ sockaddr -------------------------//

AOO_API AooBool AOO_CALL aoo_sockAddrEqual(
    const void *sockaddr1, AooAddrSize addrlen1,
    const void *sockaddr2, AooAddrSize addrlen2)
{
    aoo::ip_address addr1((const struct sockaddr *)sockaddr1, addrlen1);
    aoo::ip_address addr2((const struct sockaddr *)sockaddr2, addrlen2);
    return addr1 == addr2;
}

AOO_API AooSize AOO_CALL aoo_sockAddrHash(
    const void *sockaddr, AooAddrSize addrlen)
{
    aoo::ip_address addr((const struct sockaddr *)sockaddr, addrlen);
    return addr.hash();
}

AOO_API AooError AOO_CALL aoo_ipEndpointToSockAddr(
    const AooChar *ipAddress, AooUInt16 port,
    AooSocketFlags type, void *sockaddr, AooAddrSize *addrlen)
{
    aoo::ip_address::ip_type family;
    if (type & kAooSocketIPv6) {
        if (type & kAooSocketIPv4) {
            family = aoo::ip_address::Unspec; // both IPv6 and IPv4
        } else {
            family = aoo::ip_address::IPv6;
        }
    } else {
        family = aoo::ip_address::IPv4;
    }
    bool ipv4mapped = type & kAooSocketIPv4Mapped;

    aoo::ip_address addr(ipAddress, port, family, ipv4mapped);
    if (!addr.valid()) {
        return kAooErrorBadFormat;
    }
    auto len = addr.length();
    if (len > *addrlen) {
        return kAooErrorInsufficientBuffer;
    }
    memcpy(sockaddr, addr.address(), len);
    *addrlen = len;
    return kAooOk;
}

AOO_API AooError AOO_CALL aoo_sockAddrToIpEndpoint(
    const void *sockaddr, AooSize addrlen,
    AooChar *ipAddressBuffer, AooSize *ipAddressSize,
    AooUInt16 *port, AooSocketFlags *type)
{
    aoo::ip_address addr((const struct sockaddr *)sockaddr, addrlen);
    auto ipstring = addr.name();
    auto ipsize = strlen(ipstring) + 1;
    if (ipsize > *ipAddressSize) {
        return kAooErrorInsufficientBuffer;
    }
    memcpy(ipAddressBuffer, ipstring, ipsize);
    *ipAddressSize = ipsize - 1; // exclude null character!
    *port = addr.port();
    if (type) {
        switch (addr.type()) {
        case aoo::ip_address::IPv6:
            *type = addr.is_ipv4_mapped() ? kAooSocketDualStack : kAooSocketIPv6;
            break;
        case aoo::ip_address::IPv4:
            *type = kAooSocketIPv4;
            break;
        default:
            return kAooErrorBadFormat; // shouldn't really happen...
        }
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL aoo_resolveIpEndpoint(
    const AooChar *hostName, AooUInt16 port,
    AooSocketFlags type, void *sockaddr, AooAddrSize *addrlen)
{
    aoo::ip_address::ip_type family;
    if (type & kAooSocketIPv6) {
        if (type & kAooSocketIPv4) {
            family = aoo::ip_address::Unspec; // both IPv6 and IPv4
        } else {
            family = aoo::ip_address::IPv6;
        }
    } else {
        family = aoo::ip_address::IPv4;
    }
    bool ipv4mapped = type & kAooSocketIPv4Mapped;

    try {
        auto addr = aoo::ip_address::resolve(hostName, port, family, ipv4mapped).front();
        auto len = addr.length();
        if (len > *addrlen) {
            return kAooErrorInsufficientBuffer;
        }
        memcpy(sockaddr, addr.address(), len);
        *addrlen = len;
    } catch (const aoo::resolve_error& e) {
        aoo::socket::set_last_error(e.code());
        return kAooErrorSocket;
    }

    return kAooOk;
}

//--------------------------- socket/system error --------------------------//

AOO_API AooError AOO_CALL aoo_getLastSocketError(
    AooInt32 *errorCode, AooChar *errorMessageBuffer, AooSize *errorMessageSize)
{
    auto e = aoo::socket::get_last_error();
    *errorCode = e;
    if (errorMessageBuffer) {
        auto len = aoo::socket::strerror(e, errorMessageBuffer, *errorMessageSize);
        // NB: 0 character excluded!
        if (len > 0 && len < *errorMessageSize) {
            *errorMessageSize = len;
        } else {
            return kAooErrorInsufficientBuffer;
        }
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL aoo_getLastSystemError(
    AooInt32 *errorCode, AooChar *errorMessageBuffer, AooSize *errorMessageSize)
{
    // WSAGetLastError() is just a wrapper around GetLastError()
    return aoo_getLastSocketError(errorCode, errorMessageBuffer, errorMessageSize);
}


namespace aoo {

AooError check_version(const char *version) {
    int major, minor;
    if (sscanf(version, "%d.%d", &major, &minor) != 2) {
        return kAooErrorBadFormat;
    }
    // LOG_DEBUG("major version: " << major << ", minor version: " << minor);

    if (major == kAooVersionMajor){
        return kAooOk;
    } else {
        return kAooErrorVersionNotSupported;
    }
}

} // aoo

//------------------- allocator ------------------//

namespace aoo {

namespace {

#if AOO_DEBUG_MEMORY
std::atomic<ptrdiff_t> total_memory{0};
#endif

void * AOO_CALL default_allocator(void *ptr, AooSize oldsize, AooSize newsize) {
    if (newsize > 0) {
        // allocate new memory
        // NOTE: we never reallocate
        assert(ptr == nullptr && oldsize == 0);
    #if AOO_DEBUG_MEMORY
        auto total = total_memory.fetch_add(newsize, std::memory_order_relaxed) + (ptrdiff_t)newsize;
        LOG_DEBUG("allocate " << newsize << " bytes (total: " << total << ")");
    #endif
        return operator new(newsize);
    } else if (oldsize > 0) {
        // free memory
    #if AOO_DEBUG_MEMORY
        auto total = total_memory.fetch_sub(oldsize, std::memory_order_relaxed) - (ptrdiff_t)oldsize;
        LOG_DEBUG("deallocate " << oldsize << " bytes (total: " << total << ")");
    #endif
        operator delete(ptr);
    } else {
        // (de)allocating memory of size 0: do nothing.
        assert(ptr == nullptr);
    }
    return nullptr;
}

} // namespace

#if AOO_CUSTOM_ALLOCATOR || AOO_DEBUG_MEMORY

void * allocate(size_t size) {
    auto result = g_interface.alloc(nullptr, 0, size);
    if (!result && size > 0) {
        throw std::bad_alloc{};
    }
    return result;
}

void deallocate(void *ptr, size_t size){
    g_interface.alloc(ptr, size, 0);
}

#endif

} // aoo

//---------------------- RT memory --------------------------//

namespace aoo {

namespace {
aoo::rt_memory_pool<true, aoo::allocator<char>> g_rt_memory_pool;
}

void * rt_allocate(size_t size) {
    auto ptr = g_rt_memory_pool.allocate(size);
    if (!ptr && (size > 0)) {
        throw std::bad_alloc{};
    }
    return ptr;
}

void rt_deallocate(void *ptr, size_t size) {
    g_rt_memory_pool.deallocate(ptr, size);
}

sync::mutex g_rt_memory_pool_lock;
size_t g_rt_memory_pool_refcount = 0;

void rt_memory_pool_ref() {
    sync::scoped_lock<sync::mutex> l(g_rt_memory_pool_lock);
    g_rt_memory_pool_refcount++;
    // LOG_DEBUG("rt_memory_pool_ref: " << g_rt_memory_pool_refcount);
}

void rt_memory_pool_unref() {
    sync::scoped_lock<sync::mutex> l(g_rt_memory_pool_lock);
    if (--g_rt_memory_pool_refcount == 0) {
        LOG_DEBUG("total RT memory usage: " << g_rt_memory_pool.memory_usage()
                  << " / " << g_rt_memory_pool.capacity() << " bytes");
        g_rt_memory_pool.reset();
    }
    // LOG_DEBUG("rt_memory_pool_unref: " << g_rt_memory_pool_refcount);
}

} // aoo

//------------------------ codec ---------------------------//

namespace aoo {

// can't use std::unordered_map with custom allocator, so let's just use
// aoo::vector instead. performance might be better anyway, since the vector
// will be very small.

using codec_list = aoo::vector<std::pair<aoo::string, const AooCodecInterface *>>;
static codec_list g_codec_list;

const AooCodecInterface * find_codec(const char * name){
    for (auto& [key, codec] : g_codec_list) {
        if (key == name) {
            return codec;
        }
    }
    return nullptr;
}

} // aoo

const AooCodecHostInterface * aoo_getCodecHostInterface(void)
{
    return &aoo::g_interface;
}

AOO_API AooError AOO_CALL aoo_registerCodec(const AooCodecInterface *codec){
    if (aoo::find_codec(codec->name)) {
        LOG_WARNING("codec " << codec->name << " already registered!");
        return kAooErrorAlreadyExists;
    }
    aoo::g_codec_list.emplace_back(codec->name, codec);
    LOG_INFO("registered codec '" << codec->name << "'");
    return kAooOk;
}

//--------------------------- (de)initialize -----------------------------------//

void aoo_nullLoad(const AooCodecHostInterface *);
void aoo_nullUnload();
void aoo_pcmLoad(const AooCodecHostInterface *);
void aoo_pcmUnload();
#if AOO_USE_OPUS
void aoo_opusLoad(const AooCodecHostInterface *);
void aoo_opusUnload();
#endif

#define CHECK_SETTING(ptr, field) \
    (ptr && AOO_CHECK_FIELD(ptr, AooSettings, field))

AOO_API AooError AOO_CALL aoo_initialize(const AooSettings *settings) {
    static bool initialized = false;
    if (!initialized) {
    #if AOO_NET
        aoo::socket::init();
    #endif
        // optional settings
        if (CHECK_SETTING(settings, logFunc) && settings->logFunc) {
            aoo::g_interface.log = settings->logFunc;
        }
        if (CHECK_SETTING(settings, allocFunc) && settings->allocFunc) {
    #if AOO_CUSTOM_ALLOCATOR
            aoo::g_interface.alloc = settings->allocFunc;
    #else
            LOG_WARNING("aoo_initialize: custom allocator not supported");
    #endif
        }

        if (CHECK_SETTING(settings, memPoolSize) && settings->memPoolSize > 0) {
            aoo::g_rt_memory_pool.resize(settings->memPoolSize);
        } else {
            aoo::g_rt_memory_pool.resize(AOO_MEM_POOL_SIZE);
        }

        // register codecs
        aoo_nullLoad(&aoo::g_interface);
        aoo_pcmLoad(&aoo::g_interface);
    #if AOO_USE_OPUS
        aoo_opusLoad(&aoo::g_interface);
    #endif

        initialized = true;
    }
    return kAooOk;
}

AOO_API void AOO_CALL aoo_terminate() {
#if AOO_DEBUG_MEMORY
    aoo::g_rt_memory_pool.print();
#endif
    // unload codecs
    aoo_nullUnload();
    aoo_pcmUnload();
#if AOO_USE_OPUS
    aoo_opusUnload();
#endif
    // free codec plugin list
    aoo::codec_list tmp;
    std::swap(tmp, aoo::g_codec_list);
}
