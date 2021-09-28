#include "aoo/aoo.h"
#if USE_AOO_NET
# include "aoo/aoo_net.h"
# include "common/net_utils.hpp"
#endif
#include "aoo/aoo_codec.h"

#include "imp.hpp"

#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "aoo/codec/aoo_pcm.h"
#if USE_CODEC_OPUS
# include "aoo/codec/aoo_opus.h"
#endif

#define CERR_LOG_FUNCTION 1
#if CERR_LOG_FUNCTION
# include <iostream>
#endif
#define CERR_LOG_MUTEX 1

#include <atomic>
#include <random>
#include <unordered_map>

namespace aoo {

//--------------------- helper functions -----------------//

char * copy_string(const char * s){
    if (s){
        auto len = strlen(s);
        auto result = aoo::allocate(len + 1);
        memcpy(result, s, len + 1);
        return (char *)result;
    } else {
        return nullptr;
    }
}

void free_string(char *s){
    if (s){
        auto len = strlen(s);
        aoo::deallocate(s, len + 1);
    }
}

void * copy_sockaddr(const void *sa, int32_t len){
    if (sa){
        auto result = aoo::allocate(len);
        memcpy(result, sa, len);
        return result;
    } else {
        return nullptr;
    }
}

void free_sockaddr(void *sa, int32_t len){
    if (sa){
        aoo::deallocate(sa, len);
    }
}

int32_t get_random_id(){
    thread_local std::random_device dev;
    thread_local std::mt19937 mt(dev());
    std::uniform_int_distribution<int32_t> dist;
    return dist(mt);
}

} // aoo

//------------------- allocator ------------------//

#if AOO_CUSTOM_ALLOCATOR || AOO_DEBUG_MEMORY

namespace aoo {

#if AOO_DEBUG_MEMORY
std::atomic<ptrdiff_t> total_memory{0};
#endif

static AooAllocator g_allocator {
    [](size_t n, void *){
    #if AOO_DEBUG_MEMORY
        auto total = total_memory.fetch_add(n, std::memory_order_relaxed) + (ptrdiff_t)n;
        LOG_ALL("allocate " << n << " bytes (total: " << total << ")");
    #endif
        return operator new(n);
    },
    nullptr,
    [](void *ptr, size_t n, void *){
    #if AOO_DEBUG_MEMORY
        auto total = total_memory.fetch_sub(n, std::memory_order_relaxed) - (ptrdiff_t)n;
        LOG_ALL("deallocate " << n << " bytes (total: " << total << ")");
    #endif
        operator delete(ptr);
    },
    nullptr
};

void * allocate(size_t size){
    return g_allocator.alloc(size, g_allocator.context);
}

void deallocate(void *ptr, size_t size){
    g_allocator.free(ptr, size, g_allocator.context);
}

} // aoo

#endif

//----------------------- logging --------------------------//

namespace aoo {

#if CERR_LOG_FUNCTION

#if CERR_LOG_MUTEX
static aoo::sync::mutex g_log_mutex;
#endif

static void cerr_logfunction(AooLogLevel level, const char *msg, ...){
#if CERR_LOG_MUTEX
    aoo::sync::scoped_lock<aoo::sync::mutex> lock(g_log_mutex);
#endif
    std::cerr << msg << std::endl;
}

static AooLogFunc g_logfunction = cerr_logfunction;

#else // CERR_LOG_FUNCTION

static AooLogFunc g_logfunction = nullptr;

#endif // CERR_LOG_FUNCTION

void log_message(AooLogLevel level, const std::string &msg){
    if (g_logfunction) {
        g_logfunction(level, msg.c_str());
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
    case kAooErrorIdle:
        return "idle";
    case kAooErrorOutOfMemory:
        return "out of memory";
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
    if (size >= kAooBinMsgHeaderSize &&
        !memcmp(msg, kAooBinMsgDomain, kAooBinMsgDomainSize))
    {
        // domain (int32), type (int16), cmd (int16), id (int32) ...
        *type = aoo::from_bytes<int16_t>(msg + 4);
        // cmd = aoo::from_bytes<int16_t>(msg + 6);
        *id = aoo::from_bytes<int32_t>(msg + 8);
        *offset = 12;

        return kAooOk;
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

//---------------------- memory -----------------------------//

#define DEBUG_MEMORY_LIST 0

memory_list::block * memory_list::block::alloc(size_t size){
    auto fullsize = sizeof(block::header) + size;
    auto b = (block *)aoo::allocate(fullsize);
    b->header.next = nullptr;
    b->header.size = size;
#if DEBUG_MEMORY_LIST
    LOG_ALL("allocate memory block (" << size << " bytes)");
#endif
    return b;
}

void memory_list::block::free(memory_list::block *b){
#if DEBUG_MEMORY_LIST
    LOG_ALL("deallocate memory block (" << b->header.size << " bytes)");
#endif
    auto fullsize = sizeof(block::header) + b->header.size;
    aoo::deallocate(b, fullsize);
}

memory_list::~memory_list(){
    // free memory blocks
    auto b = list_.load(std::memory_order_relaxed);
    while (b){
        auto next = b->header.next;
        block::free(b);
        b = next;
    }
}

void* memory_list::allocate(size_t size) {
    for (;;){
        // try to pop existing block
        auto head = list_.load(std::memory_order_relaxed);
        if (head){
            auto next = head->header.next;
            if (list_.compare_exchange_weak(head, next, std::memory_order_acq_rel)){
                if (head->header.size >= size){
                #if DEBUG_MEMORY_LIST
                    LOG_ALL("reuse memory block (" << head->header.size << " bytes)");
                #endif
                    return head->data;
                } else {
                    // free block
                    block::free(head);
                }
            } else {
                // try again
                continue;
            }
        }
        // allocate new block
        return block::alloc(size)->data;
    }
}
void memory_list::deallocate(void* ptr) {
    auto b = block::from_bytes(ptr);
    b->header.next = list_.load(std::memory_order_relaxed);
    // check if the head has changed and update it atomically.
    // (if the CAS fails, 'next' is updated to the current head)
    while (!list_.compare_exchange_weak(b->header.next, b, std::memory_order_acq_rel)) ;
#if DEBUG_MEMORY_LIST
    LOG_ALL("return memory block (" << b->header.size << " bytes)");
#endif
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

AooError AOO_CALL aoo_registerCodec(const char *name, const AooCodecInterface *codec){
    if (aoo::find_codec(name)) {
        LOG_WARNING("aoo: codec " << name << " already registered!");
        return kAooErrorUnknown;
    }
    aoo::g_codec_list.emplace_back(name, codec);
    LOG_VERBOSE("aoo: registered codec '" << name << "'");
    return kAooOk;
}

//--------------------------- (de)initialize -----------------------------------//

void aoo_pcmCodecSetup(AooCodecRegisterFunc fn, AooLogFunc log, const AooAllocator *alloc);
#if USE_CODEC_OPUS
void aoo_opusCodecSetup(AooCodecRegisterFunc fn, AooLogFunc log, const AooAllocator *alloc);
#endif

#if AOO_CUSTOM_ALLOCATOR || AOO_DEBUG_MEMORY
#define ALLOCATOR &aoo::g_allocator
#else
#define ALLOCATOR nullptr
#endif

void AOO_CALL aoo_initialize(){
    static bool initialized = false;
    if (!initialized){
    #if USE_AOO_NET
        aoo::socket_init();
    #endif

        // register codecs
        aoo_pcmCodecSetup(aoo_registerCodec, aoo::g_logfunction, ALLOCATOR);

    #if USE_CODEC_OPUS
        aoo_opusCodecSetup(aoo_registerCodec, aoo::g_logfunction, ALLOCATOR);
    #endif

        initialized = true;
    }
}

void AOO_CALL aoo_initializeEx(AooLogFunc log, const AooAllocator *alloc) {
    if (log) {
        aoo::g_logfunction = log;
    }
#if AOO_CUSTOM_ALLOCATOR
    if (alloc) {
        aoo::g_allocator = *alloc;
    }
#endif
    aoo_initialize();
}

void AOO_CALL aoo_terminate() {}


