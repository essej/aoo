#include "aoo/aoo.h"
#if USE_AOO_NET
# include "aoo/aoo_net.h"
# include "common/net_utils.hpp"
#endif
#include "aoo/aoo_codec.h"

#include "binmsg.hpp"
#include "detail.hpp"
#include "rt_memory_pool.hpp"

#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "aoo/codec/aoo_pcm.h"
#if USE_CODEC_OPUS
# include "aoo/codec/aoo_opus.h"
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

namespace aoo {

//--------------------- interface table -------------------//

void * AOO_CALL def_allocator(void *ptr, AooSize oldsize, AooSize newsize);

#ifndef _MSC_VER
void __attribute__((format(printf, 2, 3 )))
#else
void
#endif
    def_logfunc(AooLogLevel, const char *, ...);

// populate interface table with default implementations
static AooCodecHostInterface g_interface = {
    sizeof(AooCodecHostInterface),
    aoo_registerCodec,
    def_allocator,
    def_logfunc
};

//--------------------- helper functions -----------------//

int32_t get_random_id(){
#if defined(ESP_PLATFORM)
    // use ESP hardware RNG
    return esp_random() & 0x7fffffff;
#else
    // software PRNG
#if defined(__i386__) || defined(_M_IX86) || \
        defined(__x86_64__) || defined(_M_X64) || \
        defined(__arm__) || defined(__aarch64__)
    // Don't use on embedded platforms because it can cause issues,
    // e.g. ESP-IDF stores thread_local variables on the stack!
    thread_local std::mt19937 mt(std::random_device{}());
#else
    // fallback for embedded platforms
    static sync::padded_spinlock spinlock;
    static std::mt19937 mt(std::random_device{}());
    sync::scoped_lock<sync::padded_spinlock> lock(spinlock);
#endif
    std::uniform_int_distribution<int32_t> dist;
    return dist(mt);
#endif
}

//------------------------------ OSC utilities ---------------------------------//

osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const aoo::metadata& md) {
    if (md.size() > 0) {
        msg << md.type() << osc::Blob(md.data(), md.size());
    } else {
        msg << "" << osc::Blob(msg.Data(), 0); // HACK: do not use nullptr because of memcpy()
    }
    return msg;
}

AooDataView osc_read_metadata(osc::ReceivedMessageArgumentIterator& it) {
    auto type = (it++)->AsString();
    const void *blobdata;
    osc::osc_bundle_element_size_t blobsize;
    (it++)->AsBlob(blobdata, blobsize);
    if (blobsize) {
        return AooDataView { type, (const AooByte *)blobdata, (AooSize)blobsize };
    } else {
        return AooDataView { type, nullptr, 0 };
    }
}

osc::OutboundPacketStream& operator<<(osc::OutboundPacketStream& msg, const ip_address& addr) {
    // send *unmapped* addresses in case the client is IPv4 only
    if (addr.valid()) {
        msg << addr.name_unmapped() << (int32_t)addr.port();
    } else {
        msg << "" << (int32_t)0;
    }
    return msg;
}

ip_address osc_read_address(osc::ReceivedMessageArgumentIterator& it, ip_address::ip_type type) {
    auto host = (it++)->AsString();
    auto port = (it++)->AsInt32();
    return ip_address(host, port, type);
}

//----------------------- logging --------------------------//

#if CERR_LOG_FUNCTION

#if CERR_LOG_MUTEX
static sync::mutex g_log_mutex;
#endif

#ifndef _MSC_VER
void __attribute__((format(printf, 2, 3 )))
#else
void
#endif
    def_logfunc(AooLogLevel level, const char *fmt, ...)
{
    const char *label = nullptr;

#if CERR_LOG_LABEL
    switch (level) {
    case kAooLogLevelError:
        label = "error";
        break;
    case kAooLogLevelWarning:
        label = "warning";
        break;
    case kAooLogLevelVerbose:
        label = "verbose";
        break;
    case kAooLogLevelDebug:
        label = "debug";
        break;
    default:
        break;
    }
#endif
    const auto size = Log::buffer_size;
    char buffer[size];
    int count = 0;
    if (label) {
        count += snprintf(buffer, size, "[aoo][%s] ", label);
    } else {
        count += snprintf(buffer, size, "[aoo] ");
    }

    va_list args;
    va_start (args, fmt);
    count += vsnprintf(buffer + count, size - count, fmt, args);
    va_end (args);

    // force newline
    count = std::min(count, size - 2);
    buffer[count++] = '\n';
    buffer[count++] = '\0';

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
    def_logfunc(AooLogLevel, const char *, ...) {}

#endif // CERR_LOG_FUNCTION

Log::int_type Log::overflow(int_type c) {
    if (pos_ < buffer_size - 1) {
        buffer_[pos_++] = c;
        return 0;
    } else {
        return std::streambuf::traits_type::eof();
    }
}

std::streamsize Log::xsputn(const char_type *s, std::streamsize n) {
    auto limit = buffer_size - 1;
    if (pos_ < limit) {
        if (pos_ + n > limit) {
            n = limit - pos_;
        }
        memcpy(buffer_ + pos_, s, n);
        pos_ += n;
        return n;
    } else {
        return 0;
    }
}

Log::~Log() {
    if (aoo::g_interface.logFunc) {
        buffer_[pos_] = '\0';
        aoo::g_interface.logFunc(level_, buffer_);
    }
}

} // aoo

const char *aoo_strerror(AooError e){
    switch (e){
    case kAooErrorUnknown:
        return "unspecified error";
    case kAooErrorNone:
        return "no error";
    case kAooErrorNotImplemented:
        return "not implemented";
    case kAooErrorBadArgument:
        return "bad argument";
    case kAooErrorIdle:
        return "idle";
    case kAooErrorOutOfMemory:
        return "out of memory";
    case kAooErrorInsufficientBuffer:
        return "insufficient buffer";
    default:
        return "unknown error code";
    }
}

//----------------------- OSC --------------------------//

AooError AOO_CALL aoo_parsePattern(
        const AooByte *msg, AooInt32 size,
        AooMsgType *type, AooId *id, AooInt32 *offset)
{
    int32_t count = 0;
    if (aoo::binmsg_check(msg, size))
    {
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
        && !memcmp(msg, kAooMsgDomain, kAooMsgDomainLen))
    {
        count += kAooMsgDomainLen;
        if (size >= (count + kAooMsgSourceLen)
            && !memcmp(msg + count, kAooMsgSource, kAooMsgSourceLen))
        {
            *type = kAooTypeSource;
            count += kAooMsgSourceLen;
        } else if (size >= (count + kAooMsgSinkLen)
            && !memcmp(msg + count, kAooMsgSink, kAooMsgSinkLen))
        {
            *type = kAooTypeSink;
            count += kAooMsgSinkLen;
        } else {
        #if USE_AOO_NET
            if (size >= (count + kAooNetMsgClientLen)
                && !memcmp(msg + count, kAooNetMsgClient, kAooNetMsgClientLen))
            {
                *type = kAooTypeClient;
                count += kAooNetMsgClientLen;
            } else if (size >= (count + kAooNetMsgServerLen)
                && !memcmp(msg + count, kAooNetMsgServer, kAooNetMsgServerLen))
            {
                *type = kAooTypeServer;
                count += kAooNetMsgServerLen;
            } else if (size >= (count + kAooNetMsgPeerLen)
                && !memcmp(msg + count, kAooNetMsgPeer, kAooNetMsgPeerLen))
            {
                *type = kAooTypePeer;
                count += kAooNetMsgPeerLen;
            } else if (size >= (count + kAooNetMsgRelayLen)
                && !memcmp(msg + count, kAooNetMsgRelay, kAooNetMsgRelayLen))
            {
                *type = kAooTypeRelay;
                count += kAooNetMsgRelayLen;
            } else {
                return kAooErrorUnknown;
            }

            if (offset){
                *offset = count;
            }
        #endif // USE_AOO_NET

            return kAooOk;
        }

        // /aoo/source or /aoo/sink
        if (id){
            int32_t skip = 0;
            if (sscanf((const char *)(msg + count), "/%d%n", id, &skip) > 0){
                count += skip;
            } else {
                // TODO only print relevant part of OSC address string
                LOG_ERROR("aoo_parse_pattern: bad ID " << (msg + count));
                return kAooErrorUnknown;
            }
        } else {
            return kAooErrorUnknown;
        }

        if (offset){
            *offset = count;
        }
        return kAooOk;
    } else {
        return kAooErrorUnknown; // not an AOO message
    }
}

//-------------------- NTP time ----------------------------//

uint64_t AOO_CALL aoo_getCurrentNtpTime(void){
    return aoo::time_tag::now();
}

double AOO_CALL aoo_osctime_to_seconds(uint64_t t){
    return aoo::time_tag(t).to_seconds();
}

uint64_t AOO_CALL aoo_osctime_from_seconds(double s){
    return aoo::time_tag::from_seconds(s);
}

double AOO_CALL aoo_ntpTimeDuration(uint64_t t1, uint64_t t2){
    return aoo::time_tag::duration(t1, t2);
}

//---------------------- version -------------------------//

void aoo_getVersion(int32_t *major, int32_t *minor,
                    int32_t *patch, int32_t *test){
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

namespace aoo {

bool check_version(uint32_t version){
    auto major = (version >> 24) & 255;
#if 0
    auto minor = (version >> 16) & 255;
    auto bugfix = (version >> 8) & 255;
#endif

    if (major != kAooVersionMajor){
        return false;
    }

    return true;
}

uint32_t make_version(){
    // make version: major, minor, bugfix, [protocol]
    return ((uint32_t)kAooVersionMajor << 24)
            | ((uint32_t)kAooVersionMinor << 16)
            | ((uint32_t)kAooVersionPatch << 8);
}

//------------------- allocator ------------------//

#if AOO_DEBUG_MEMORY
std::atomic<ptrdiff_t> total_memory{0};
#endif

void * AOO_CALL def_allocator(void *ptr, AooSize oldsize, AooSize newsize) {
    if (newsize > 0) {
        // allocate new memory
        // NOTE: we never reallocate
        assert(ptr == nullptr && oldsize == 0);
    #if AOO_DEBUG_MEMORY
        auto total = total_memory.fetch_add(newsize, std::memory_order_relaxed) + (ptrdiff_t)newsize;
        LOG_ALL("allocate " << newsize << " bytes (total: " << total << ")");
    #endif
        return operator new(newsize);
    } else if (oldsize > 0) {
        // free memory
    #if AOO_DEBUG_MEMORY
        auto total = total_memory.fetch_sub(oldsize, std::memory_order_relaxed) - (ptrdiff_t)oldsize;
        LOG_ALL("deallocate " << oldsize << " bytes (total: " << total << ")");
    #endif
        operator delete(ptr);
    } else {
        // (de)allocating memory of size 0: do nothing.
        assert(ptr == nullptr);
    }
    return nullptr;
}

#if AOO_CUSTOM_ALLOCATOR || AOO_DEBUG_MEMORY

void * allocate(size_t size) {
    auto result = g_interface.allocFunc(nullptr, 0, size);
    if (!result && size > 0) {
        throw std::bad_alloc{};
    }
    return result;
}

void deallocate(void *ptr, size_t size){
    g_interface.allocFunc(ptr, size, 0);
}

#endif

//---------------------- RT memory --------------------------//

static rt_memory_pool<true, aoo::allocator<char>> g_rt_memory_pool;

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
}

void rt_memory_pool_unref() {
    sync::scoped_lock<sync::mutex> l(g_rt_memory_pool_lock);
    if (--g_rt_memory_pool_refcount == 0) {
        g_rt_memory_pool.reset();
    }
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
    for (auto& codec : g_codec_list) {
        if (codec.first == name) {
            return codec.second;
        }
    }
    return nullptr;
}

} // aoo

const AooCodecHostInterface * aoo_getCodecHostInterface(void)
{
    return &aoo::g_interface;
}

AooError AOO_CALL aoo_registerCodec(const char *name, const AooCodecInterface *codec){
    if (aoo::find_codec(name)) {
        LOG_WARNING("codec " << name << " already registered!");
        return kAooErrorUnknown;
    }
    aoo::g_codec_list.emplace_back(name, codec);
    LOG_VERBOSE("registered codec '" << name << "'");
    return kAooOk;
}

//--------------------------- (de)initialize -----------------------------------//

void aoo_pcmLoad(const AooCodecHostInterface *);
void aoo_pcmUnload();
#if USE_CODEC_OPUS
void aoo_opusLoad(const AooCodecHostInterface *);
void aoo_opusUnload();
#endif

#define HAVE_SETTING(settings, field) \
    (settings && (settings->size >= (offsetof(AooSettings, field) + sizeof(settings->field))))

AooError AOO_CALL aoo_initialize(const AooSettings *settings) {
    static bool initialized = false;
    if (!initialized) {
    #if USE_AOO_NET
        aoo::socket_init();
    #endif
        // optional settings
        if (HAVE_SETTING(settings, logFunc) && settings->logFunc) {
            aoo::g_interface.logFunc = settings->logFunc;
        }
        if (HAVE_SETTING(settings, allocFunc) && settings->allocFunc) {
    #if AOO_CUSTOM_ALLOCATOR
            aoo::g_interface.allocFunc = settings->allocFunc;
    #else
            LOG_WARNING("aoo_initialize: custom allocator not supported");
    #endif
        }

        if (HAVE_SETTING(settings, memPoolSize) && settings->memPoolSize > 0) {
            aoo::g_rt_memory_pool.resize(settings->memPoolSize);
        } else {
            aoo::g_rt_memory_pool.resize(AOO_MEM_POOL_SIZE);
        }

        // register codecs
        aoo_pcmLoad(&aoo::g_interface);

    #if USE_CODEC_OPUS
        aoo_opusLoad(&aoo::g_interface);
    #endif

        initialized = true;
    }
    return kAooOk;
}

void AOO_CALL aoo_terminate() {
#if AOO_LOG_LEVEL >= kAooLogLevelDebug
    aoo::g_rt_memory_pool.print();
#endif
    // unload codecs
    aoo_pcmUnload();
#if USE_CODEC_OPUS
    aoo_opusUnload();
#endif
    // free codec pluginlist
    aoo::codec_list tmp;
    std::swap(tmp, aoo::g_codec_list);
}
