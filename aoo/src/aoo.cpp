#include "aoo/aoo.h"
#include "aoo/aoo_net.h"

#include "common/time.hpp"
#include "common/utils.hpp"

#include "aoo/codec/aoo_pcm.h"
#if USE_CODEC_OPUS
#include "aoo/codec/aoo_opus.h"
#endif

#include <iostream>
#include <sstream>

/*//////////////////// Log ////////////////////////////*/

static aoo_logfunction gLogFunction = nullptr;

void aoo_set_logfunction(aoo_logfunction f){
    gLogFunction = f;
}

namespace aoo {

Log::~Log(){
    stream_ << "\n";
    std::string msg = stream_.str();
    if (gLogFunction){
        gLogFunction(msg.c_str());
    } else {
        std::cerr << msg;
        std::flush(std::cerr);
    }
}

}

/*//////////////////// OSC ////////////////////////////*/

int32_t aoo_parse_pattern(const char *msg, int32_t n,
                          int32_t *type, int32_t *id)
{
    int32_t offset = 0;
    if (n >= AOO_MSG_DOMAIN_LEN
        && !memcmp(msg, AOO_MSG_DOMAIN, AOO_MSG_DOMAIN_LEN))
    {
        offset += AOO_MSG_DOMAIN_LEN;
        if (n >= (offset + AOO_MSG_SOURCE_LEN)
            && !memcmp(msg + offset, AOO_MSG_SOURCE, AOO_MSG_SOURCE_LEN))
        {
            *type = AOO_TYPE_SOURCE;
            offset += AOO_MSG_SOURCE_LEN;
        } else if (n >= (offset + AOO_MSG_SINK_LEN)
            && !memcmp(msg + offset, AOO_MSG_SINK, AOO_MSG_SINK_LEN))
        {
            *type = AOO_TYPE_SINK;
            offset += AOO_MSG_SINK_LEN;
        } else if (n >= (offset + AOO_NET_MSG_CLIENT_LEN)
            && !memcmp(msg + offset, AOO_NET_MSG_CLIENT, AOO_NET_MSG_CLIENT_LEN))
        {
            *type = AOO_TYPE_CLIENT;
            offset += AOO_NET_MSG_CLIENT_LEN;
        } else if (n >= (offset + AOO_NET_MSG_SERVER_LEN)
            && !memcmp(msg + offset, AOO_NET_MSG_SERVER, AOO_NET_MSG_SERVER_LEN))
        {
            *type = AOO_TYPE_SERVER;
            offset += AOO_NET_MSG_SERVER_LEN;
        } else if (n >= (offset + AOO_NET_MSG_PEER_LEN)
            && !memcmp(msg + offset, AOO_NET_MSG_PEER, AOO_NET_MSG_PEER_LEN))
        {
            *type = AOO_TYPE_PEER;
            offset += AOO_NET_MSG_PEER_LEN;
        } else {
            return 0;
        }

        if (!id){
            return offset;
        }
        if (!memcmp(msg + offset, "/*", 2)){
            *id = AOO_ID_WILDCARD; // wildcard
            return offset + 2;
        }
        int32_t skip = 0;
        if (sscanf(msg + offset, "/%d%n", id, &skip) > 0){
            return offset + skip;
        } else {
            // TODO only print relevant part of OSC address string
            LOG_ERROR("aoo_parse_pattern: bad ID " << msg + offset);
            return 0;
        }
    } else {
        return 0; // not an AoO message
    }
}

// OSC time stamp (NTP time)
uint64_t aoo_osctime_get(void){
    return aoo::time_tag::now().to_uint64();
}

double aoo_osctime_toseconds(uint64_t t){
    return aoo::time_tag(t).to_double();
}

uint64_t aoo_osctime_fromseconds(double s){
    return aoo::time_tag(s).to_uint64();
}

double aoo_osctime_duration(uint64_t t1, uint64_t t2){
    return aoo::time_tag::duration(t1, t2);
}

/*/////////////// version ////////////////////*/

void aoo_version(int *major, int *minor,
                 int *patch, int *pre){
    if (major) *major = AOO_VERSION_MAJOR;
    if (minor) *minor = AOO_VERSION_MINOR;
    if (patch) *patch = AOO_VERSION_PATCH;
    if (pre) *pre = AOO_VERSION_PRERELEASE;
}

const char *aoo_version_string(){
    static std::string version = [](){
        std::stringstream ss;
        ss << AOO_VERSION_MAJOR << "." << AOO_VERSION_MINOR;
    #if AOO_VERSION_PATCH > 0
        ss << "." << AOO_VERSION_PATCH;
    #endif
    #if AOO_VERSION_PRERELEASE > 0
        ss << "-pre" << AOO_VERSION_PRERELEASE;
    #endif
        return ss.str();
    }();

    return version.c_str();
}

namespace aoo {

bool check_version(uint32_t version){
    auto major = (version >> 24) & 255;
    auto minor = (version >> 16) & 255;
    auto bugfix = (version >> 8) & 255;

    if (major != AOO_VERSION_MAJOR){
        return false;
    }

    return true;
}

uint32_t make_version(){
    // make version: major, minor, bugfix, [protocol]
    return ((uint32_t)AOO_VERSION_MAJOR << 24) | ((uint32_t)AOO_VERSION_MINOR << 16)
            | ((uint32_t)AOO_VERSION_PATCH << 8);
}

} // aoo

/*/////////////// (de)initialize //////////////////*/

void aoo_initialize(){
    static bool initialized = false;
    if (!initialized){
        // register codecs
        aoo_codec_pcm_setup(aoo_register_codec);

    #if USE_CODEC_OPUS
        aoo_codec_opus_setup(aoo_register_codec);
    #endif

        initialized = true;
    }
}

void aoo_terminate() {}


