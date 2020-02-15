#include "aoo/aoo.hpp"

#include "lib/oscpack/osc/OscOutboundPacketStream.h"
#include "lib/oscpack/osc/OscReceivedElements.h"

#include <iostream>
#include <algorithm>
#include <cstring>
#include <random>
#include <chrono>

/*------------------ alloca -----------------------*/
#ifdef _WIN32
# include <malloc.h> // MSVC or mingw on windows
# ifdef _MSC_VER
#  define alloca _alloca
# endif
#elif defined(__linux__) || defined(__APPLE__)
# include <alloca.h> // linux, mac, mingw, cygwin
#else
# include <stdlib.h> // BSDs for example
#endif

/*------------------ endianess -------------------*/
    // endianess check taken from Pure Data (d_osc.c)
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__FreeBSD_kernel__) \
    || defined(__OpenBSD__)
#include <machine/endian.h>
#endif

#if defined(__linux__) || defined(__CYGWIN__) || defined(__GNU__) || \
    defined(ANDROID)
#include <endian.h>
#endif

#ifdef __MINGW32__
#include <sys/param.h>
#endif

#ifdef _MSC_VER
/* _MSVC lacks BYTE_ORDER and LITTLE_ENDIAN */
 #define LITTLE_ENDIAN 0x0001
 #define BYTE_ORDER LITTLE_ENDIAN
#endif

#if !defined(BYTE_ORDER) || !defined(LITTLE_ENDIAN)
 #error No byte order defined
#endif

// 0: error, 1: warning, 2: verbose, 3: debug
#ifndef LOGLEVEL
 #define LOGLEVEL 2
#endif

#define DO_LOG(x) (std::cerr << x << std::endl)

#if LOGLEVEL >= 0
 #define LOG_ERROR(x) DO_LOG(x)
#else
 #define LOG_ERROR(x)
#endif

#if LOGLEVEL >= 1
 #define LOG_WARNING(x) DO_LOG(x)
#else
 #define LOG_WARNING(x)
#endif

#if LOGLEVEL >= 2
 #define LOG_VERBOSE(x) DO_LOG(x)
#else
 #define LOG_VERBOSE(x)
#endif

#if LOGLEVEL >= 3
 #define LOG_DEBUG(x) DO_LOG(x)
#else
 #define LOG_DEBUG(x)
#endif

namespace {

bool is_pow2(int32_t i){
    return (i & (i - 1)) == 0;
}

// conversion routines between aoo_sample and PCM data
union sample_conv {
    int8_t b[8];
    int16_t i16;
    int32_t i32;
    float f;
    double d;
};

void sample_to_pcm_int16(aoo_sample in, char *out){
    sample_conv c;
    int32_t temp = in * 0x7fff;
    c.i16 = (temp > INT16_MAX) ? INT16_MAX : (temp < INT16_MIN) ? INT16_MIN : temp;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(out, c.b, 2); // optimized away
#else
    out[0] = c.b[1];
    out[1] = c.b[0];
#endif
}

void sample_to_pcm_int24(aoo_sample in, char *out){
    const int32_t int24_max = 8388607;
    const int32_t int24_min = -8388608;
    sample_conv c;
    int32_t temp = in * 8388608;
    c.i32 = (temp > int24_max) ? int24_max : (temp < int24_min) ? int24_min : temp;
    // only copy the lower 3 bytes!
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(out, c.b + 1, 3); // optimized away
#else
    out[0] = c.b[2];
    out[1] = c.b[1];
    out[2] = c.b[0];
#endif
}

void sample_to_pcm_float32(aoo_sample in, char *out){
    sample_conv c;
    c.f = in;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(out, c.b, sizeof(float)); // optimized away
#else
    out[0] = c.b[3];
    out[1] = c.b[2];
    out[2] = c.b[1];
    out[3] = c.b[0];
#endif
}

void sample_to_pcm_float64(aoo_sample in, char *out){
    sample_conv c;
    c.d = in;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(out, c.b, sizeof(double)); // optimized away
#else
    out[0] = c.b[7];
    out[1] = c.b[6];
    out[2] = c.b[5];
    out[3] = c.b[4];
    out[4] = c.b[3];
    out[5] = c.b[2];
    out[6] = c.b[1];
    out[7] = c.b[0];
#endif
}

aoo_sample pcm_int16_to_sample(const char *in){
    sample_conv c;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(c.b, in, 2); // optimized away
#else
    c.b[0] = in[1];
    c.b[1] = in[0];
#endif
    return(aoo_sample)c.i16 / 32768.f;
}

aoo_sample pcm_int24_to_sample(const char *in){
    sample_conv c;
    // copy to the lower 3 bytes!
#if BYTE_ORDER == BIG_ENDIAN
    c.b[0] = 0;
    memcpy(c.b + 1, in, 3); // optimized away
#else
    c.b[0] = in[2];
    c.b[1] = in[1];
    c.b[2] = in[0];
    c.b[3] = 0;
#endif
    return (aoo_sample)c.i32 / 8388608.f;
}

aoo_sample pcm_float32_to_sample(const char *in){
    sample_conv c;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(c.b, in, sizeof(float)); // optimized away
#else
    c.b[0] = in[3];
    c.b[1] = in[2];
    c.b[2] = in[1];
    c.b[3] = in[0];
#endif
    return c.f;
}

aoo_sample pcm_float64_to_sample(const char *in){
    sample_conv c;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(c.b, in, sizeof(double)); // optimized away
#else
    c.b[0] = in[7];
    c.b[1] = in[6];
    c.b[2] = in[5];
    c.b[3] = in[4];
    c.b[4] = in[3];
    c.b[5] = in[2];
    c.b[6] = in[1];
    c.b[7] = in[0];
#endif
    return c.d;
}

} // namespace

/*//////////////////// OSC ////////////////////////////*/

int32_t aoo_parsepattern(const char *msg, int32_t n, int32_t *id){
    int32_t offset = sizeof(AOO_DOMAIN) - 1;
    if (n < (offset + 2)){
        return 0;
    }
    if (!memcmp(msg, AOO_DOMAIN, offset)){
        if (!memcmp(msg + offset, "/*", 2)){
            *id = AOO_ID_WILDCARD; // wildcard
            return offset + 2;
        }
        int32_t skip = 0;
        if (sscanf(&msg[offset], "/%d%n", id, &skip) > 0){
            return offset + skip;
        }
    }
    return 0;
}

// OSC time stamp (NTP time)
uint64_t aoo_osctime(void){
    // use system clock (1970 epoch)
    auto epoch = std::chrono::system_clock::now().time_since_epoch();
    auto s = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    auto ns = epoch - s;
    // add number of seconds between 1900 and 1970 (including leap years!)
    auto seconds = s.count() + 2208988800UL;
    // fractional part in nanoseconds mapped to the range of uint32_t
    auto nanos = (double)ns.count() * 4.294967296; // 2^32 / 1e9
    // seconds in the higher 4 bytes, nanos in the lower 4 bytes
    return ((int64_t)seconds << 32) | (uint64_t)nanos;
}

/*//////////////////// AoO format /////////////////////*/

int32_t aoo_bytes_per_sample(aoo_bitdepth bd){
    switch (bd){
    case AOO_INT16:
        return 2;
    case AOO_INT24:
        return 3;
    case AOO_FLOAT32:
        return 4;
    case AOO_FLOAT64:
        return 8;
    default:
        return 0;
    }
}

/*//////////////////// AoO source /////////////////////*/


#define AOO_DATA_HEADERSIZE 80
// address pattern string: max 32 bytes
// typetag string: max. 12 bytes
// args (without blob data): 36 bytes

aoo_source * aoo_source_new(int32_t id) {
    return new aoo_source(id);
}

aoo_source::aoo_source(int32_t id)
    : id_(id) {}

void aoo_source_free(aoo_source *src){
    delete src;
}

aoo_source::~aoo_source() {}

void aoo_source_setformat(aoo_source *src, aoo_format *f) {
    if (f){
        src->set_format(*f);
    }
}

void aoo_source::set_format(aoo_format &f){
    assert(is_pow2(f.blocksize));
    if (strcmp(f.mime_type, AOO_MIME_PCM)){
        LOG_ERROR("mime type " << f.mime_type << " not supported!");
        return;
    }
    // make new salt
    thread_local std::random_device dev;
    thread_local std::mt19937 mt(dev());
    std::uniform_int_distribution<int32_t> dist;
    salt_ = dist(mt);
    format_ = std::make_unique<aoo_format>(f);
    bytespersample_ = aoo_bytes_per_sample(format_->bitdepth);
    sequence_ = 0;
    update();
    for (auto& sink : sinks_){
        send_format(sink);
    }
    LOG_VERBOSE("aoo_source::setformat: salt = " << salt_ << ", mime type = " << format_->mime_type << ", sr = " << format_->samplerate
                << ", blocksize = " << format_->blocksize << ", overlap = " << format_->overlap);
}

void aoo_source_setbuffersize(aoo_source *src, int32_t ms) {
    src->set_buffersize(ms);
}

void aoo_source::set_buffersize(int32_t ms){
    buffersize_ = ms > 0 ? ms : 0;
    if (format_){
        update();
    }
}

void aoo_source_setpacketsize(aoo_source *src, int32_t nbytes){
    src->set_packetsize(nbytes);
}

void aoo_source::set_packetsize(int32_t nbytes){
    int32_t minsize = AOO_DATA_HEADERSIZE + 64;
    if (nbytes < minsize){
        LOG_WARNING("packet size too small! setting to " << minsize);
        packetsize_ = minsize;
    } else if (nbytes > AOO_MAXPACKETSIZE){
        LOG_WARNING("packet size too big! setting to " << AOO_MAXPACKETSIZE);
        packetsize_ = AOO_MAXPACKETSIZE;
    } else {
        packetsize_ = nbytes;
    }
}

void aoo_source::update(){
    assert(format_ != nullptr);
    auto nsamples = format_->blocksize * format_->nchannels;
    // recalculate buffersize from ms to samples
    double bufsize = (double)buffersize_ * format_->samplerate * 0.001;
    auto d = div(bufsize, format_->blocksize);
    int32_t nbuffers = d.quot + (d.rem != 0); // round up
    nbuffers = std::max<int32_t>(nbuffers, 1);
    // resize audio queue
    audioqueue_.resize(nbuffers * nsamples, nsamples, nsamples);
    ttqueue_.resize(nbuffers, 1, 1);
    LOG_DEBUG("aoo_source::update: nbuffers = " << nbuffers);
}

void aoo_source_addsink(aoo_source *src, void *sink, int32_t id, aoo_replyfn fn) {
    src->add_sink(sink, id, fn);
}

void aoo_source::add_sink(void *sink, int32_t id, aoo_replyfn fn){
    if (id == AOO_ID_WILDCARD){
        // remove all existing descriptors matching sink
        remove_sink(sink, AOO_ID_WILDCARD);
    }
    auto result = std::find_if(sinks_.begin(), sinks_.end(), [&](auto& s){
        return (s.endpoint == sink) && (s.id == id);
    });
    if (result == sinks_.end()){
        sink_desc sd = { sink, fn, id, 0 };
        sinks_.push_back(sd);
        send_format(sd);
    } else {
        LOG_WARNING("aoo_source::add_sink: sink already added!");
    }
}

void aoo_source_removesink(aoo_source *src, void *sink, int32_t id) {
    src->remove_sink(sink, id);
}

void aoo_source::remove_sink(void *sink, int32_t id){
    if (id == AOO_ID_WILDCARD){
        // remove all descriptors matching sink (ignore id)
        auto it = std::remove_if(sinks_.begin(), sinks_.end(), [&](auto& s){
            return s.endpoint == sink;
        });
        sinks_.erase(it, sinks_.end());
    } else {
        auto result = std::find_if(sinks_.begin(), sinks_.end(), [&](auto& s){
            return (s.endpoint == sink) && (s.id == id);
        });
        if (result != sinks_.end()){
            sinks_.erase(result);
        } else {
            LOG_WARNING("aoo_source::remove_sink: sink not found!");
        }
    }
}

void aoo_source_removeall(aoo_source *src) {
    src->remove_all();
}

void aoo_source::remove_all(){
    sinks_.clear();
}

void aoo_source_setsinkchannel(aoo_source *src, void *sink, int32_t id, int32_t chn){
    src->set_sink_channel(sink, id, chn);
}

void aoo_source::set_sink_channel(void *sink, int32_t id, int32_t chn){
    if (chn < 0){
        chn = 0;
    }
    auto result = std::find_if(sinks_.begin(), sinks_.end(), [&](auto& s){
        return (s.endpoint == sink) && (s.id == id);
    });
    if (result != sinks_.end()){
        LOG_VERBOSE("aoo_source::set_sink_channel: " << chn);
        result->channel = chn;
    } else {
        LOG_ERROR("aoo_source::set_sink_channel: sink not found!");
    }
}

void aoo_source_handlemessage(aoo_source *src, const char *data, int32_t n,
                              void *sink, aoo_replyfn fn) {
    src->handle_message(data, n, sink, fn);
}

// /AoO/<src>/request <sink>
void aoo_source::handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn){
    osc::ReceivedPacket packet(data, n);
    osc::ReceivedMessage msg(packet);

    int32_t src = 0;
    auto onset = aoo_parsepattern(data, n, &src);
    if (!onset){
        LOG_WARNING("not an AoO message!");
        return;
    }
    if (src != id_ && src != AOO_ID_WILDCARD){
        LOG_WARNING("wrong source ID!");
        return;
    }

    if (!strcmp(msg.AddressPattern() + onset, AOO_REQUEST)){
        if (msg.ArgumentCount() == 1){
            try {
                auto it = msg.ArgumentsBegin();
                auto id = it->AsInt32();
                auto sink = std::find_if(sinks_.begin(), sinks_.end(), [&](auto& s){
                    return (s.endpoint == endpoint) && (s.id == id);
                });
                if (sink != sinks_.end()){
                    // just resend format (the last format message might have been lost)
                    send_format(*sink);
                } else {
                    // add new sink
                    add_sink(endpoint, id, fn);
                }
            } catch (const osc::Exception& e){
                LOG_ERROR(e.what());
            }
        } else {
            LOG_ERROR("wrong number of arguments for /request message");
        }
    } else {
        LOG_WARNING("unknown message '" << (msg.AddressPattern() + onset) << "'");
    }
}

int32_t aoo_source_send(aoo_source *src) {
    return src->send();
}

bool aoo_source::send(){
    if (!format_){
        return false;
    }
    if (audioqueue_.read_available() && ttqueue_.read_available()){
        const auto nchannels = format_->nchannels;
        const auto blocksize = format_->blocksize;
        const auto samplesize = bytespersample_;

        // copy and convert audio samples to blob data (non-interleaved -> interleaved)
        const auto nbytes = samplesize * nchannels * blocksize;
        char * blobdata = (char *)alloca(nbytes);
        auto ptr = audioqueue_.read_data();

        auto samples_to_blob = [&](auto fn){
            auto blobptr = blobdata;
            for (int i = 0; i < blocksize; ++i){
                for (int j = 0; j < nchannels; ++j){
                    auto f = ptr[j * blocksize + i];
                    fn(f, blobptr);
                    blobptr += samplesize;
                }
            }
        };

        switch (format_->bitdepth){
        case AOO_INT16:
            samples_to_blob(sample_to_pcm_int16);
            break;
        case AOO_INT24:
            samples_to_blob(sample_to_pcm_int24);
            break;
        case AOO_FLOAT32:
            samples_to_blob(sample_to_pcm_float32);
            break;
        case AOO_FLOAT64:
            samples_to_blob(sample_to_pcm_float64);
            break;
        default:
            // unknown bitdepth
            break;
        }

        audioqueue_.read_commit();

        // read timetag
        auto tt = ttqueue_.read();

        auto maxpacketsize = packetsize_ - AOO_DATA_HEADERSIZE;
        auto d = div(nbytes, maxpacketsize);
        int32_t nframes = d.quot + (d.rem != 0);

        // send a single frame to all sink
        // /AoO/<sink>/data <src> <salt> <seq> <t> <channel_onset> <numpackets> <packetnum> <data>
        auto send_data = [&](int32_t frame, const char* data, auto n){
            LOG_DEBUG("send frame: " << frame << ", size: " << n);
            for (auto& sink : sinks_){
                char buf[AOO_MAXPACKETSIZE];
                osc::OutboundPacketStream msg(buf, sizeof(buf));

                if (sink.id != AOO_ID_WILDCARD){
                    const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_DATA);
                    char address[max_addr_size];
                    snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, sink.id, AOO_DATA);

                    msg << osc::BeginMessage(address);
                } else {
                    msg << osc::BeginMessage(AOO_DATA_WILDCARD);
                }

                // for now ignore timetag
                msg << id_ << salt_ << sequence_ << osc::TimeTag(tt)
                    << sink.channel << nframes << frame << osc::Blob(data, n)
                    << osc::EndMessage;

                sink.send(msg.Data(), msg.Size());
            }
        };

        auto blobptr = blobdata;
        // send large frames (might be 0)
        for (int32_t i = 0; i < d.quot; ++i, blobptr += maxpacketsize){
            send_data(i, blobptr, maxpacketsize);
        }
        // send remaining bytes as a single frame (might be the only one!)
        if (d.rem){
            send_data(d.quot, blobptr, d.rem);
        }

        sequence_++;
        return true;
    } else {
        // LOG_DEBUG("couldn't send");
        return false;
    }
}

int32_t aoo_source_process(aoo_source *src, const aoo_sample **data, int32_t n, uint64_t t) {
    return src->process(data, n, t);
}

bool aoo_source::process(const aoo_sample **data, int32_t n, uint64_t t){
    if (!format_){
        return false;
    }
    // LATER allow different blocksize in process()
    assert(n == format_->blocksize);
    if (audioqueue_.write_available() && ttqueue_.write_available()){
        // copy audio samples (non-interleaved!)
        auto ptr = audioqueue_.write_data();
        int nchannels = format_->nchannels;
        for (int i = 0; i < nchannels; ++i, ptr += n){
            std::copy(data[i], data[i] + n, ptr);
        }
        audioqueue_.write_commit();

        // push timetag
        ttqueue_.write(t);

        return true;
    } else {
        // LOG_DEBUG("couldn't process");
        return false;
    }
}

// /AoO/<sink>/format <src> <salt> <mime> <bitdepth> <numchannels> <samplerate> <blocksize> <overlap>

void aoo_source::send_format(sink_desc &sink){
    if (format_){
        char buf[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream msg(buf, sizeof(buf));

        if (sink.id != AOO_ID_WILDCARD){
            const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_FORMAT);
            char address[max_addr_size];
            snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, sink.id, AOO_FORMAT);

            msg << osc::BeginMessage(address);
        } else {
            msg << osc::BeginMessage(AOO_FORMAT_WILDCARD);
        }

        msg << id_ << salt_ << format_->mime_type << static_cast<int32_t>(format_->bitdepth)
            << format_->nchannels << format_->samplerate << format_->blocksize
            << format_->overlap << osc::EndMessage;

        sink.send(msg.Data(), msg.Size());
    }
}

/*//////////////////// aoo_sink /////////////////////*/

aoo_sink * aoo_sink_new(int32_t id) {
    return new aoo_sink(id);
}

void aoo_sink_free(aoo_sink *sink) {
    delete sink;
}

void aoo_sink_setup(aoo_sink *sink, int32_t nchannels, int32_t sr, int32_t blocksize,
                    aoo_processfn fn, void *user) {
    sink->setup(nchannels, sr, blocksize, fn, user);
}

void aoo_sink::setup(int32_t nchannels, int32_t sr, int32_t blocksize,
                     aoo_processfn fn, void *user){
    nchannels_ = nchannels;
    samplerate_ = sr;
    blocksize_ = blocksize;
    processfn_ = fn;
    user_ = user;
    buffer_.resize(blocksize * nchannels);
    for (auto& src : sources_){
        update_source(src);
    }
}

void aoo_sink_setbuffersize(aoo_sink *sink, int32_t ms){
    sink->set_buffersize(ms);
}

void aoo_sink::set_buffersize(int32_t ms){
    buffersize_ = ms > 0 ? ms : 0;
    for (auto& src : sources_){
        update_source(src);
    }
}

int32_t aoo_sink_handlemessage(aoo_sink *sink, const char *data, int32_t n,
                            void *src, aoo_replyfn fn) {
    return sink->handle_message(data, n, src, fn);
}

// /AoO/<sink>/format <src> <mime> <bitdepth> <numchannels> <samplerate> <blocksize> <overlap>
// /AoO/<sink>/data <src> <seq> <t> <channel_onset> [<numpackets> <packetnum>] <data>

int32_t aoo_sink::handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn){
    osc::ReceivedPacket packet(data, n);
    osc::ReceivedMessage msg(packet);

    int32_t sink = 0;
    auto onset = aoo_parsepattern(data, n, &sink);
    if (!onset){
        LOG_WARNING("not an AoO message!");
        return 1; // ?
    }
    if (sink != id_ && sink != AOO_ID_WILDCARD){
        LOG_WARNING("wrong sink ID!");
        return 1; // ?
    }

    if (!strcmp(msg.AddressPattern() + onset, AOO_FORMAT)){
        if (msg.ArgumentCount() == AOO_FORMAT_NARGS){
            auto it = msg.ArgumentsBegin();
            try {
                int32_t id = (it++)->AsInt32();
                int32_t salt = (it++)->AsInt32();
                // get format from arguments
                aoo_format format;
                auto mimetype = (it++)->AsString();
                if (!strcmp(mimetype, AOO_MIME_PCM)){
                    format.mime_type = AOO_MIME_PCM;
                } else {
                    LOG_ERROR("bad mime type " << mimetype);
                    return 1;
                }
                format.bitdepth = (aoo_bitdepth)(it++)->AsInt32();
                format.nchannels = (aoo_bitdepth)(it++)->AsInt32();
                format.samplerate = (aoo_bitdepth)(it++)->AsInt32();
                format.blocksize = (aoo_bitdepth)(it++)->AsInt32();
                format.overlap = (aoo_bitdepth)(it++)->AsInt32();

                handle_format_message(endpoint, fn, id, salt, format);
            } catch (const osc::Exception& e){
                LOG_ERROR(e.what());
            }
        } else {
            LOG_ERROR("wrong number of arguments for /format message");
        }
    } else if (!strcmp(msg.AddressPattern() + onset, AOO_DATA)){
        if (msg.ArgumentCount() == AOO_DATA_NARGS){
            auto it = msg.ArgumentsBegin();
            try {
                // get header from arguments
                auto id = (it++)->AsInt32();
                auto salt = (it++)->AsInt32();
                auto seq = (it++)->AsInt32();
                auto tt {(it++)->AsTimeTag()};
                auto chn = (it++)->AsInt32();
                auto nframes = (it++)->AsInt32();
                auto frame = (it++)->AsInt32();
                const void *blobdata;
                osc::osc_bundle_element_size_t blobsize;
                (it++)->AsBlob(blobdata, blobsize);

                handle_data_message(endpoint, fn, id, salt, seq, tt,
                                    chn, nframes, frame, (const char *)blobdata, blobsize);
            } catch (const osc::Exception& e){
                LOG_ERROR(e.what());
            }
        } else {
            LOG_ERROR("wrong number of arguments for /data message");
        }
    } else {
        LOG_WARNING("unknown message '" << (msg.AddressPattern() + onset) << "'");
    }
    return 1; // ?
}

void aoo_sink::handle_format_message(void *endpoint, aoo_replyfn fn,
                                     int32_t id, int32_t salt, const aoo_format &format){
    LOG_DEBUG("handle format message");
    if (id == AOO_ID_WILDCARD){
        // update all sources from this endpoint
        for (auto& src : sources_){
            if (src.endpoint == endpoint){
                src.salt = salt;
                src.format = format;
                update_source(src);
            }
        }
    } else {
        // try to find existing source
        auto src = std::find_if(sources_.begin(), sources_.end(), [&](auto& s){
            return (s.endpoint == endpoint) && (s.id == id);
        });
        // LATER support different samplerates
        if (format.samplerate != samplerate_){
            LOG_ERROR("sample rate mismatch!");
            if (src != sources_.end()){
                sources_.erase(src);
            }
            return;
        }
        if (src == sources_.end()){
            // not found - add new source
            sources_.emplace_back(endpoint, fn, id, salt);
            src = sources_.end() - 1;
        } else {
            src->salt = salt;
        }
        // update source
        src->format = format;
        update_source(*src);
    }
}

void aoo_sink::handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                                   int32_t salt, int32_t seq, aoo::time_tag tt, int32_t chn,
                                   int32_t nframes, int32_t frame, const char *data, int32_t size){
    // first try to find existing source
    auto result = std::find_if(sources_.begin(), sources_.end(), [&](auto& s){
        return (s.endpoint == endpoint) && (s.id == id);
    });
    // check if the 'salt' values match, otherwise the source format has changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    if (result != sources_.end() && result->salt == salt){
        LOG_DEBUG("handle data message");
        auto& src = *result;
        auto samplesize = aoo_bytes_per_sample(src.format.bitdepth);
        auto& queue = src.blockqueue;

        if (seq < src.newest){
            LOG_DEBUG("block " << seq << " out of order!");
        }

        if ((src.newest - seq) > queue.capacity()){
            // block too old, discard!
            LOG_DEBUG("discarded old block " << seq);
            return;
        }
        if ((seq - src.newest) > queue.capacity()){
            // too large gap between incoming block and most recent block.
            // either network problem or stream has temporarily stopped.
            // clear the block queue and fill audio buffer with zeros.
            queue.clear();
            // push silent blocks to keep the buffer full, but leave room for one block!
            int count = 0;
            auto nsamples = src.audioqueue.write_size();
            while (src.audioqueue.write_available() > 1 && src.infoqueue.write_available() > 1){
                auto ptr = src.audioqueue.write_data();
                for (int i = 0; i < nsamples; ++i){
                    ptr[i] = 0;
                }
                src.audioqueue.write_commit();
                // push nominal samplerate + default channel (0)
                aoo::source_desc::info i;
                i.sr = src.format.samplerate;
                i.channel = 0;
                src.infoqueue.write(i);

                count++;
            }
            LOG_DEBUG("wrote " << count << " silent blocks for transmission gap");
        }
        auto block = queue.find(seq);
        if (!block){
            if (queue.full()){
                // if the queue is full, we have to drop a block;
                // in this case we send a block of zeros to the audio buffer
                auto nsamples = src.audioqueue.write_size();
                if (src.audioqueue.write_available() && src.infoqueue.write_available()){
                    auto ptr = src.audioqueue.write_data();
                    for (int i = 0; i < nsamples; ++i){
                        ptr[i] = 0;
                    }
                    src.audioqueue.write_commit();
                    // push nominal samplerate + default channel (0)
                    aoo::source_desc::info i;
                    i.sr = src.format.samplerate;
                    i.channel = 0;
                    src.infoqueue.write(i);

                    LOG_DEBUG("wrote silence for dropped block");
                }
            }
            // add new block
            auto nbytes = src.format.blocksize * src.format.nchannels * samplesize;
            block = queue.insert(aoo::block (seq, tt, chn, nbytes, nframes));
        }

        // add frame to block
        block->add_frame(frame, (const char *)data, size);

        // update newest sequence number
        if (seq > src.newest){
            src.newest = seq;
        }

        // Check if the *oldest* block is complete, so we can transfer it to the audio buffer.
        // We can do the same for all subsequent blocks which have already been completed.
        block = queue.begin();
        int32_t count = 0;
        while ((block != queue.end()) && block->complete()
               && src.audioqueue.write_available() && src.infoqueue.write_available()){
            LOG_DEBUG("write samples (" << block->sequence << ")");
            auto nsamples = src.audioqueue.write_size();

            auto ptr = src.audioqueue.write_data();

            // convert from source format to samples.
            // keep samples interleaved, so that we don't need
            // an additional buffer for reblocking.
            auto blob_to_samples = [&](auto convfn){
                auto b = block->data();
                for (int i = 0; i < nsamples; ++i, b += samplesize){
                    ptr[i] = convfn(b);
                }
            };

            switch (src.format.bitdepth){
            case AOO_INT16:
                blob_to_samples(pcm_int16_to_sample);
                break;
            case AOO_INT24:
                blob_to_samples(pcm_int24_to_sample);
                break;
            case AOO_FLOAT32:
                blob_to_samples(pcm_float32_to_sample);
                break;
            case AOO_FLOAT64:
                blob_to_samples(pcm_float64_to_sample);
                break;
            default:
                // unknown bitdepth
                break;
            }

            src.audioqueue.write_commit();

            // first handle timetag!
            src.handle_timetag(block->timetag);

            // push info
            aoo::source_desc::info i;
            i.sr = src.dll.samplerate();
            i.channel = block->channel;
            src.infoqueue.write(i);

            count++;
            block++;
        }
        while (count--){
            queue.pop_front();
        }
    } else {
        // discard data and request format!
        request_format(endpoint, fn, id);
    }
}

void aoo_sink::update_source(aoo::source_desc &src){
    // resize audio ring buffer
    if (src.format.mime_type){
        // recalculate buffersize from ms to samples
        double bufsize = (double)buffersize_ * src.format.samplerate * 0.001;
        // make sure that the buffer size is divisible by the *larger* blocksize
        auto stride = std::max<int32_t>(src.format.blocksize, blocksize_);
        auto d = div(bufsize, stride);
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        nbuffers = std::max<int32_t>(1, nbuffers); // e.g. if buffersize_ is 0
        // resize audio buffer and initially fill with zeros.
        auto nchannels = src.format.nchannels;
        auto writesize = src.format.blocksize * nchannels;
        auto readsize = blocksize_ * nchannels;
        auto nsamples = nbuffers * stride * nchannels;
        auto nwritebuffers = nsamples / writesize;
        LOG_VERBOSE("read size " << readsize << ", write size " << writesize << ", nsamples " << nsamples <<
                    ", nbuffers " << nbuffers << ", nwritebuffers " << nwritebuffers);
        src.audioqueue.resize(nsamples, readsize, writesize);
        src.infoqueue.resize(nwritebuffers, 1, 1);
        while (src.audioqueue.write_available() && src.infoqueue.write_available()){
            LOG_DEBUG("write zero block");
            src.audioqueue.write_commit();
            // push nominal samplerate + default channel (0)
            aoo::source_desc::info i;
            i.sr = src.format.samplerate;
            i.channel = 0;
            src.infoqueue.write(i);
        }
        // resize block queue
        src.blockqueue.resize(nwritebuffers);
        src.newest = 0;
        src.starttime = 0;
        LOG_VERBOSE("update source " << src.id << ": sr = " << src.format.samplerate
                    << ", blocksize = " << src.format.blocksize << ", nchannels = "
                    << src.format.nchannels << ", bufsize = " << nsamples);
    }
}

void aoo_sink::request_format(void *endpoint, aoo_replyfn fn, int32_t id){
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_REQUEST);
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, id, AOO_REQUEST);

    msg << osc::BeginMessage(address) << id_ << osc::EndMessage;

    fn(endpoint, msg.Data(), msg.Size());
}

int32_t aoo_sink_process(aoo_sink *sink) {
    return sink->process();
}

int32_t aoo_sink::process(){
    if (!processfn_){
        return 0;
    }
    std::fill(buffer_.begin(), buffer_.end(), 0);

    bool didsomething = false;

    for (auto& src : sources_){
        // check if the source has enough samples
        if (src.audioqueue.read_available() && src.infoqueue.read_available()){
            // sum source into sink (interleaved -> non-interleaved),
            // starting at the desired sink channel offset.
            // out of bound source channels are silently ignored.
            auto info = src.infoqueue.read();
            auto ptr = src.audioqueue.read_data();
            auto nchannels = src.format.nchannels;
            auto offset = info.channel;
            for (int i = 0; i < blocksize_; ++i){
                for (int j = 0; j < nchannels; ++j){
                    auto chn = j + offset;
                    // ignore out-of-bound source channels!
                    if (chn < nchannels_){
                        buffer_[chn * blocksize_ + i] += *ptr;
                    }
                    ptr++;
                }
            }
            src.audioqueue.read_commit();
            LOG_DEBUG("read samples");
            didsomething = true;
        }
    }

    if (didsomething){
        // set buffer pointers and pass to audio callback
        auto vec = (const aoo_sample **)alloca(sizeof(aoo_sample *) * nchannels_);
        for (int i = 0; i < nchannels_; ++i){
            vec[i] = &buffer_[i * blocksize_];
        }
        processfn_(vec, blocksize_, user_);
        return 1;
    } else {
        return 0;
    }
}

namespace aoo {

/*////////////////////////// source_block /////////////////////////////*/

block::block(int32_t seq, time_tag tt, int32_t chn,
                           int32_t nbytes, int32_t nframes)
    : sequence(seq), timetag(tt), channel(chn), numframes(nframes)
{
    buffer.resize(nbytes);
    // set missing frame bits to 1
    frames = 0;
    for (int i = 0; i < nframes; ++i){
        frames |= (1 << (uint64_t)i);
    }
    // LOG_DEBUG("initial frames: " << (unsigned)frames);
}

bool block::complete() const {
    assert(buffer.data() != nullptr);
    assert(sequence >= 0);
    return frames == 0;
}

void block::add_frame(int which, const char *data, int32_t n){
    assert(data != nullptr);
    assert(buffer.data() != nullptr);
    if (which == numframes - 1){
        LOG_DEBUG("copy last frame with " << n << " bytes");
        std::copy(data, data + n, buffer.end() - n);
    } else {
        LOG_DEBUG("copy frame " << which << " with " << n << " bytes");
        std::copy(data, data + n, buffer.begin() + which * n);
    }
    frames &= ~(1 << which);
#if 0
    for (int i = 0; i < n; i += 4){
        std::cerr << pcm_float32_to_sample(data + i) << " ";
    }
    std::cerr << std::endl;
#endif
    LOG_DEBUG("frames: " << (unsigned)frames);
}

/*////////////////////////// block_queue /////////////////////////////*/

void block_queue::clear(){
    blocks_.clear();
}

void block_queue::resize(int32_t n){
    // LATER remove older items instead of recent ones
    blocks_.clear();
    blocks_.reserve(n);
    capacity_ = n;
}

bool block_queue::empty() const {
    return blocks_.empty();
}

bool block_queue::full() const {
    return (int32_t)blocks_.size() == capacity_;
}

int32_t block_queue::size() const {
    return blocks_.size();
}

int32_t block_queue::capacity() const {
    return capacity_;
}

block* block_queue::insert(block&& b){
    // find pos to insert
    int pos = 0;
    while (pos < size()){
        assert(blocks_[pos].sequence != b.sequence);
        if (blocks_[pos].sequence > b.sequence){
            break;
        }
        pos++;
    }
    if (full()){
        if (pos > 0){
            // move older blocks to the left
            LOG_DEBUG("insert block and pop old block");
            std::move(&blocks_[1], &blocks_[pos], &blocks_[0]);
            blocks_[pos-1] = std::move(b);
            return &blocks_[pos-1];
        } else {
            // simply replace first item
            LOG_DEBUG("replace oldest block");
            blocks_[0] = std::move(b);
            return &blocks_[0];
        }
    } else {
        LOG_DEBUG("insert block");
        // insert block (will move newer items to the right)
        blocks_.insert(blocks_.begin() + pos, std::move(b));
        return &blocks_[pos];
    }
}

block* block_queue::find(int32_t seq){
    for (int32_t i = 0; i < size(); ++i){
        if (blocks_[i].sequence == seq){
            return &blocks_[i];
        }
    }
    return nullptr;
}

void block_queue::pop_front(){
    assert(!empty());
    blocks_.erase(blocks_.begin());
}

void block_queue::pop_back(){
    assert(!empty());
    blocks_.pop_back();
}

block& block_queue::front(){
    return blocks_.front();
}

block& block_queue::back(){
    return blocks_.back();
}

block* block_queue::begin(){
    return blocks_.data();
}

block* block_queue::end(){
    return begin() + size();
}

block& block_queue::operator[](int32_t i){
    return blocks_[i];
}


/*////////////////////////// source_desc /////////////////////////////*/

source_desc::source_desc(void *_endpoint, aoo_replyfn _fn, int32_t _id, int32_t _salt)
    : endpoint(_endpoint), fn(_fn), id(_id), salt(_salt)
{
    memset(&format, 0, sizeof(format));
}

void source_desc::send(const char *data, int32_t n){
    fn(endpoint, data, n);
}

void source_desc::handle_timetag(time_tag tt){
    // update DLL and push samplerate
    // TODO: handle out of order packets
    if (starttime == 0){
        starttime = tt.to_double();
        dll.setup(format.samplerate, format.blocksize, AOO_DLL_BW, 0);
    } else {
        auto elapsed = tt.to_double() - starttime;
        dll.update(elapsed);
    #if AOO_DEBUG_DLL
        fprintf(stderr, "SOURCE\n");
        // fprintf(stderr, "timetag: %llu, seconds: %f\n", tt.to_uint64(), tt.to_double());
        fprintf(stderr, "elapsed: %f, period: %f, samplerate: %f\n",
                elapsed, dll.period(), dll.samplerate());
        fflush(stderr);
    #endif
    }
}

} // aoo
