#include "aoo/aoo.hpp"
#include "aoo/aoo_utils.hpp"
#include "aoo/aoo_pcm.h"
#include "aoo/aoo_opus.h"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <chrono>
#include <unordered_map>

namespace aoo {

static std::unordered_map<std::string, std::unique_ptr<aoo::codec>> codec_dict;

void register_codec(const char *name, const aoo_codec *codec){
    LOG_VERBOSE("aoo: registered codec '" << name << "'");
    codec_dict[name] = std::make_unique<aoo::codec>(codec);
}

const aoo::codec * find_codec(const std::string& name){
    auto it = codec_dict.find(name);
    if (it != codec_dict.end()){
        return it->second.get();
    } else {
        return nullptr;
    }
}

bool is_pow2(int32_t i){
    return (i & (i - 1)) == 0;
}

} // aoo

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
uint64_t aoo_osctime_get(void){
    // use system clock (1970 epoch)
    auto epoch = std::chrono::system_clock::now().time_since_epoch();
    auto s = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    auto ns = epoch - s;
    // add number of seconds between 1900 and 1970 (including leap years!)
    auto seconds = s.count() + 2208988800UL;
    // fractional part in nanoseconds mapped to the range of uint32_t
    auto nanos = (double)ns.count() * 4.294967296; // 2^32 / 1e9
    // seconds in the higher 4 bytes, nanos in the lower 4 bytes
    aoo::time_tag tt;
    tt.seconds = seconds;
    tt.nanos = nanos;
    return tt.to_uint64();
}

double aoo_osctime_toseconds(uint64_t t){
    return aoo::time_tag(t).to_double();
}

uint64_t aoo_osctime_fromseconds(double s){
    return aoo::time_tag(s).to_uint64();
}

uint64_t aoo_osctime_addseconds(uint64_t t, double s){
    // LATER do operator overloading for aoo::time_tag
    // split osctime
    uint64_t th = t >> 32;
    uint64_t tl = (t & 0xFFFFFFFF);
    // split seconds
    uint64_t sh = (uint64_t)s;
    double fract = s - (double)sh;
    uint64_t sl = fract * 4294967296.0;
    // combine and reassemble
    uint64_t rh = th + sh;
    uint64_t rl = tl + sl;
    // handle overflowing nanoseconds
    rh += (rl >> 32); // add carry
    rl &= 0xFFFFFFFF; // mask carry
    return (rh << 32) + rl;
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
    : id_(id){}

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
    salt_ = make_salt();

    if (!encoder_ || strcmp(encoder_->name(), f.codec)){
        auto codec = aoo::find_codec(f.codec);
        if (codec){
            encoder_ = codec->create_encoder();
        } else {
            LOG_ERROR("codec '" << f.codec << "' not supported!");
            return;
        }
        if (!encoder_){
            LOG_ERROR("couldn't create encoder!");
            return;
        }
    }
    encoder_->setup(f);

    sequence_ = 0;
    update();
    for (auto& sink : sinks_){
        send_format(sink);
    }
}

void aoo_source_setup(aoo_source *src, aoo_source_settings *settings){
    if (settings){
        src->setup(*settings);
    }
}

void aoo_source::setup(aoo_source_settings &settings){
    blocksize_ = settings.blocksize;
    nchannels_ = settings.nchannels;
    samplerate_ = settings.samplerate;
    buffersize_ = std::max<int32_t>(settings.buffersize, 0);

    // packet size
    const int32_t minpacketsize = AOO_DATA_HEADERSIZE + 64;
    if (settings.packetsize < minpacketsize){
        LOG_WARNING("packet size too small! setting to " << minpacketsize);
        packetsize_ = minpacketsize;
    } else if (settings.packetsize > AOO_MAXPACKETSIZE){
        LOG_WARNING("packet size too large! setting to " << AOO_MAXPACKETSIZE);
        packetsize_ = AOO_MAXPACKETSIZE;
    } else {
        packetsize_ = settings.packetsize;
    }

    // time filter
    bandwidth_ = settings.time_filter_bandwidth;
    starttime_ = 0; // will update

    if (encoder_){
        update();
    }
}

void aoo_source::update(){
    assert(encoder_ != nullptr && encoder_->blocksize() > 0 && encoder_->samplerate() > 0);
    if (blocksize_ > 0 && samplerate_ > 0 && nchannels_ > 0){
        auto nsamples = encoder_->blocksize() * nchannels_;
        // recalculate buffersize from ms to samples
        double bufsize = (double)buffersize_ * encoder_->samplerate() * 0.001;
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        nbuffers = std::max<int32_t>(nbuffers, 1);
        // resize audio queue
        audioqueue_.resize(nbuffers * nsamples, nsamples);
        srqueue_.resize(nbuffers, 1);
        LOG_DEBUG("aoo_source::update: nbuffers = " << nbuffers);
        // setup resampler
        if (blocksize_ != encoder_->blocksize() || samplerate_ != encoder_->samplerate()){
            resampler_.setup(blocksize_, encoder_->blocksize(),
                             samplerate_, encoder_->samplerate(), nchannels_);
            resampler_.update(samplerate_, encoder_->samplerate());
        } else {
            resampler_.clear();
        }
    }
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
        LOG_ERROR("aoo_source: channel onset " << chn << " out of range!");
    }
    if (id == AOO_ID_WILDCARD){
        for (auto& s : sinks_){
            if (s.endpoint == sink){
                LOG_VERBOSE("aoo_source: send to sink " << s.id << " on channel " << chn);
                s.channel = chn;
            }
        }
    } else {
        auto result = std::find_if(sinks_.begin(), sinks_.end(), [&](auto& s){
            return (s.endpoint == sink) && (s.id == id);
        });
        if (result != sinks_.end()){
            LOG_VERBOSE("aoo_source: send to sink " << result->id << " on channel " << chn);
            result->channel = chn;
        } else {
            LOG_ERROR("aoo_source::set_sink_channel: sink not found!");
        }
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
    if (!encoder_){
        return false;
    }

    if (audioqueue_.read_available() && srqueue_.read_available()){
        const auto nchannels = encoder_->nchannels();
        const auto blocksize = encoder_->blocksize();
        aoo::data_packet packet;
        packet.sequence = sequence_;
        packet.samplerate = srqueue_.read();

        // copy and convert audio samples to blob data
        const auto blobmaxsize = sizeof(double) * nchannels * blocksize; // overallocate
        char * blobdata = (char *)alloca(blobmaxsize);

        packet.totalsize = encoder_->encode(audioqueue_.read_data(), audioqueue_.blocksize(),
                                        blobdata, blobmaxsize);

        audioqueue_.read_commit();

        auto maxpacketsize = packetsize_ - AOO_DATA_HEADERSIZE;
        auto d = div(packet.totalsize, maxpacketsize);
        packet.nframes = d.quot + (d.rem != 0);

        // send a single frame to all sink
        // /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>
        auto dosend = [&](int32_t frame, const char* data, auto n){
            LOG_DEBUG("send frame: " << frame << ", size: " << n);
            packet.framenum = frame;
            packet.data = data;
            packet.size = n;
            for (auto& sink : sinks_){
                packet.channel = sink.channel;
                send_data(sink, packet);
            }
        };

        auto blobptr = blobdata;
        // send large frames (might be 0)
        for (int32_t i = 0; i < d.quot; ++i, blobptr += maxpacketsize){
            dosend(i, blobptr, maxpacketsize);
        }
        // send remaining bytes as a single frame (might be the only one!)
        if (d.rem){
            dosend(d.quot, blobptr, d.rem);
        }

        sequence_++;
        // handle overflow (with 64 samples @ 44.1 kHz this happens every 36 days)
        // for now just force a reset by changing the salt, LATER think how to handle this better
        if (sequence_ == INT32_MAX){
            salt_ = make_salt();
        }
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
    // update DLL
    aoo::time_tag tt(t);
    if (starttime_ == 0){
        LOG_VERBOSE("setup time DLL for source");
        starttime_ = tt.to_double();
        dll_.setup(samplerate_, blocksize_, bandwidth_, 0);
    } else {
        auto elapsed = tt.to_double() - starttime_;
        dll_.update(elapsed);
    #if AOO_DEBUG_DLL
        fprintf(stderr, "SOURCE\n");
        // fprintf(stderr, "timetag: %llu, seconds: %f\n", tt.to_uint64(), tt.to_double());
        fprintf(stderr, "elapsed: %f, period: %f, samplerate: %f\n",
                elapsed, dll_.period(), dll_.samplerate());
        fflush(stderr);
    #endif
    }

    if (!encoder_ || sinks_.empty()){
        return false;
    }

    // non-interleaved -> interleaved
    auto insamples = blocksize_ * nchannels_;
    auto outsamples = encoder_->blocksize() * nchannels_;
    auto *buf = (aoo_sample *)alloca(insamples * sizeof(aoo_sample));
    for (int i = 0; i < nchannels_; ++i){
        for (int j = 0; j < n; ++j){
            buf[j * nchannels_ + i] = data[i][j];
        }
    }
    if (encoder_->blocksize() != blocksize_ || encoder_->samplerate() != samplerate_){
        // go through resampler
        if (resampler_.write_available() >= insamples){
            resampler_.write(buf, insamples);
        } else {
            LOG_DEBUG("couldn't process");
            return false;
        }
        while (resampler_.read_available() >= outsamples
               && audioqueue_.write_available()
               && srqueue_.write_available())
        {
            // copy audio samples
            resampler_.read(audioqueue_.write_data(), audioqueue_.blocksize());
            audioqueue_.write_commit();

            // push samplerate
            auto ratio = (double)encoder_->samplerate() / (double)samplerate_;
            srqueue_.write(dll_.samplerate() * ratio);
        }

        return true;
    } else {
        // bypass resampler
        if (audioqueue_.write_available() && srqueue_.write_available()){
            // copy audio samples
            std::copy(buf, buf + outsamples, audioqueue_.write_data());
            audioqueue_.write_commit();

            // push samplerate
            srqueue_.write(dll_.samplerate());

            return true;
        } else {
            LOG_DEBUG("couldn't process");
            return false;
        }
    }
}

// /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

void aoo_source::send_data(sink_desc& sink, const aoo::data_packet& packet){
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

    msg << id_ << salt_ << packet.sequence << packet.samplerate << packet.channel
        << packet.totalsize << packet.nframes << packet.framenum
        << osc::Blob(packet.data, packet.size) << osc::EndMessage;

    sink.send(msg.Data(), msg.Size());
}

// /AoO/<sink>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <options...>

void aoo_source::send_format(sink_desc &sink){
    if (encoder_){
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

        auto settings = (char *)alloca(AOO_CODEC_MAXSETTINGSIZE);
        int32_t nchannels, samplerate, blocksize;
        auto setsize = encoder_->write(nchannels, samplerate, blocksize, settings, AOO_CODEC_MAXSETTINGSIZE);

        msg << id_ << salt_ << nchannels << samplerate << blocksize
            << encoder_->name() << osc::Blob(settings, setsize) << osc::EndMessage;

        sink.send(msg.Data(), msg.Size());
    }
}

int32_t aoo_source::make_salt(){
    thread_local std::random_device dev;
    thread_local std::mt19937 mt(dev());
    std::uniform_int_distribution<int32_t> dist;
    return dist(mt);
}

/*//////////////////// aoo_sink /////////////////////*/

aoo_sink * aoo_sink_new(int32_t id) {
    return new aoo_sink(id);
}

void aoo_sink_free(aoo_sink *sink) {
    delete sink;
}

void aoo_sink_setup(aoo_sink *sink, aoo_sink_settings *settings) {
    if (settings){
        sink->setup(*settings);
    }
}

void aoo_sink::setup(aoo_sink_settings& settings){
    processfn_ = settings.processfn;
    user_ = settings.userdata;
    nchannels_ = settings.nchannels;
    samplerate_ = settings.samplerate;
    blocksize_ = settings.blocksize;
    buffersize_ = std::max<int32_t>(settings.buffersize, 0);
    bandwidth_ = std::max<double>(0, std::min<double>(1, settings.time_filter_bandwidth));
    starttime_ = 0; // will update time DLL

    buffer_.resize(blocksize_ * nchannels_);
    for (auto& src : sources_){
        // don't need to lock
        update_source(src);
    }
}

int32_t aoo_sink_handlemessage(aoo_sink *sink, const char *data, int32_t n,
                            void *src, aoo_replyfn fn) {
    return sink->handle_message(data, n, src, fn);
}

// /AoO/<sink>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <settings...>
// /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

int32_t aoo_sink::handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn){
    osc::ReceivedPacket packet(data, n);
    osc::ReceivedMessage msg(packet);

    if (samplerate_ == 0){
        return 1; // not setup yet
    }

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
                auto nchannels = (it++)->AsInt32();
                auto samplerate = (it++)->AsInt32();
                auto blocksize = (it++)->AsInt32();
                auto codec = (it++)->AsString();
                const void *blobdata;
                osc::osc_bundle_element_size_t blobsize;
                (it++)->AsBlob(blobdata, blobsize);

                handle_format_message(endpoint, fn, id, salt, nchannels,
                                      samplerate, blocksize, codec, (const char *)blobdata, blobsize);
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
                auto sr = (it++)->AsDouble();
                auto chn = (it++)->AsInt32();
                auto totalsize = (it++)->AsInt32();
                auto nframes = (it++)->AsInt32();
                auto frame = (it++)->AsInt32();
                const void *blobdata;
                osc::osc_bundle_element_size_t blobsize;
                (it++)->AsBlob(blobdata, blobsize);

                handle_data_message(endpoint, fn, id, salt, seq, sr, chn, totalsize,
                                    nframes, frame, (const char *)blobdata, blobsize);
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
                                     int32_t id, int32_t salt,
                                     int32_t nchannels, int32_t samplerate, int32_t blocksize,
                                     const char *codec, const char *settings, int32_t size){
    LOG_DEBUG("handle format message");

    auto update_format = [&](aoo::source_desc& src){
        if (!src.decoder || strcmp(src.decoder->name(), codec)){
            auto c = aoo::find_codec(codec);
            if (c){
                src.decoder = c->create_decoder();
            } else {
                LOG_ERROR("codec '" << codec << "' not supported!");
                return;
            }
            if (!src.decoder){
                LOG_ERROR("couldn't create decoder!");
                return;
            }
        }
        src.decoder->read(nchannels, samplerate, blocksize, settings, size);

        update_source(src);
    };

    if (id == AOO_ID_WILDCARD){
        // update all sources from this endpoint
        for (auto& src : sources_){
            if (src.endpoint == endpoint){
                std::unique_lock<std::mutex> lock(mutex_); // !
                src.salt = salt;
                update_format(src);
            }
        }
    } else {
        // try to find existing source
        auto src = std::find_if(sources_.begin(), sources_.end(), [&](auto& s){
            return (s.endpoint == endpoint) && (s.id == id);
        });
        std::unique_lock<std::mutex> lock(mutex_); // !
        if (src == sources_.end()){
            // not found - add new source
            sources_.emplace_back(endpoint, fn, id, salt);
            src = sources_.end() - 1;
        } else {
            src->salt = salt;
        }
        // update source
        update_format(*src);
    }
}

void aoo_sink::handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                                   int32_t salt, int32_t seq, double sr, int32_t chn, int32_t totalsize,
                                   int32_t nframes, int32_t frame, const char *data, int32_t size){
    // first try to find existing source
    auto result = std::find_if(sources_.begin(), sources_.end(), [&](auto& s){
        return (s.endpoint == endpoint) && (s.id == id);
    });
    // check if the 'salt' values match. the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    if (result != sources_.end() && result->salt == salt){
        auto& src = *result;
        auto& queue = src.blockqueue;
    #if 1
        if (!src.decoder){
            LOG_DEBUG("ignore data message");
            return;
        }
    #else
        assert(src.decoder != nullptr);
    #endif

        if (seq < src.newest){
            LOG_VERBOSE("block " << seq << " out of order!");
        }

        if ((seq - src.newest) > 1){
            LOG_VERBOSE("skipped " << (seq - src.newest - 1) << " blocks");
        }

        if ((src.newest - seq) > queue.capacity()){
            // block too old, discard!
            LOG_VERBOSE("discarded old block " << seq);
            return;
        }
        if ((seq - src.newest) > queue.capacity()){
            // too large gap between incoming block and most recent block.
            // either network problem or stream has temporarily stopped.
            // clear the block queue and fill audio buffer with zeros.
            queue.clear();
            // push silent blocks to keep the buffer full, but leave room for one block!
            int count = 0;
            auto nsamples = src.audioqueue.blocksize();
            while (src.audioqueue.write_available() > 1 && src.infoqueue.write_available() > 1){
                auto ptr = src.audioqueue.write_data();
                for (int i = 0; i < nsamples; ++i){
                    ptr[i] = 0;
                }
                src.audioqueue.write_commit();
                // push nominal samplerate + default channel (0)
                aoo::source_desc::info i;
                i.sr = src.decoder->samplerate();
                i.channel = 0;
                i.state = AOO_SOURCE_STOP;
                src.infoqueue.write(i);

                count++;
            }
            LOG_VERBOSE("wrote " << count << " silent blocks for transmission gap");
        }
        auto block = queue.find(seq);
        if (!block){
            if (queue.full()){
                // if the queue is full, we have to drop a block;
                // in this case we send a block of zeros to the audio buffer
                auto nsamples = src.audioqueue.blocksize();
                if (src.audioqueue.write_available() && src.infoqueue.write_available()){
                    auto ptr = src.audioqueue.write_data();
                    for (int i = 0; i < nsamples; ++i){
                        ptr[i] = 0;
                    }
                    src.audioqueue.write_commit();
                    // push nominal samplerate + default channel (0)
                    aoo::source_desc::info i;
                    i.sr = src.decoder->samplerate();
                    i.channel = 0;
                    i.state = AOO_SOURCE_STOP;
                    src.infoqueue.write(i);

                    LOG_VERBOSE("wrote silence for dropped block " << queue.front().sequence);
                }
            }
            // add new block
            block = queue.insert(aoo::block (seq, sr, chn, totalsize, nframes));
        }

        // add frame to block
        block->add_frame(frame, (const char *)data, size);

        // update newest sequence number
        if (seq > src.newest){
            src.newest = seq;
        }

        if (!queue.empty()){
            // Check if the *oldest* block is complete, so we can transfer it to the audio buffer.
            // Do the same for subsequent blocks, but stop if a block is missing!
            block = queue.begin();
            int32_t count = 0;
            int32_t start = block->sequence;
            while ((block != queue.end()) && block->complete()
                   && (block->sequence == (start + count))
                   && src.audioqueue.write_available() && src.infoqueue.write_available())
            {
                LOG_DEBUG("write samples (" << block->sequence << ")");

                src.decoder->decode(block->data(), block->size(),
                                    src.audioqueue.write_data(), src.audioqueue.blocksize());

                src.audioqueue.write_commit();

                // push info
                aoo::source_desc::info i;
                i.sr = block->samplerate;
                i.channel = block->channel;
                i.state = AOO_SOURCE_PLAY;
                src.infoqueue.write(i);

                count++;
                block++;
            }
            while (count--){
                queue.pop_front();
            }
        }
    } else {
        // discard data and request format!
        request_format(endpoint, fn, id);
    }
}

// extra space to compensate heavy jitter
#define AOO_RCVBUFSIZE 2

void aoo_sink::update_source(aoo::source_desc &src){
    // resize audio ring buffer
    if (src.decoder && src.decoder->blocksize() > 0 && src.decoder->samplerate() > 0){
        LOG_DEBUG("update source");
        // recalculate buffersize from ms to samples
        double bufsize = (double)buffersize_ * src.decoder->samplerate() * 0.001;
        auto d = div(bufsize, src.decoder->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        nbuffers = std::max<int32_t>(1, nbuffers); // e.g. if buffersize_ is 0
        // resize audio buffer and initially fill with zeros.
        auto nsamples = src.decoder->nchannels() * src.decoder->blocksize();
        src.audioqueue.resize(nbuffers * nsamples, nsamples);
        src.infoqueue.resize(nbuffers, 1);
        while (src.audioqueue.write_available() && src.infoqueue.write_available()){
            LOG_VERBOSE("write silent block");
            src.audioqueue.write_commit();
            // push nominal samplerate + default channel (0)
            aoo::source_desc::info i;
            i.sr = src.decoder->samplerate();
            i.channel = 0;
            i.state = AOO_SOURCE_STOP;
            src.infoqueue.write(i);
        };
        // setup resampler
        src.resampler.setup(src.decoder->blocksize(), blocksize_,
                            src.decoder->samplerate(), samplerate_, src.decoder->nchannels());
        // resize block queue
        src.blockqueue.resize(nbuffers * AOO_RCVBUFSIZE);
        src.newest = 0;
        src.channel = 0;
        src.samplerate = src.decoder->samplerate();
        LOG_VERBOSE("update source " << src.id << ": sr = " << src.decoder->samplerate()
                    << ", blocksize = " << src.decoder->blocksize() << ", nchannels = "
                    << src.decoder->nchannels() << ", bufsize = " << nbuffers * nsamples);
    }
}

void aoo_sink::request_format(void *endpoint, aoo_replyfn fn, int32_t id){
    LOG_DEBUG("request format");
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_REQUEST);
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, id, AOO_REQUEST);

    msg << osc::BeginMessage(address) << id_ << osc::EndMessage;

    fn(endpoint, msg.Data(), msg.Size());
}

#if AOO_DEBUG_RESAMPLING
thread_local int32_t debug_counter = 0;
#endif

int32_t aoo_sink_process(aoo_sink *sink, uint64_t t) {
    return sink->process(t);
}

#define AOO_MAXNUMEVENTS 256

int32_t aoo_sink::process(uint64_t t){
    if (!processfn_){
        return 0;
    }
    std::fill(buffer_.begin(), buffer_.end(), 0);

    bool didsomething = false;

    // update time DLL
    aoo::time_tag tt(t);
    if (starttime_ == 0){
        starttime_ = tt.to_double();
        LOG_VERBOSE("setup time DLL for sink");
        dll_.setup(samplerate_, blocksize_, bandwidth_, 0);
    } else {
        auto elapsed = tt.to_double() - starttime_;
        dll_.update(elapsed);
    #if AOO_DEBUG_DLL
        DO_LOG("SINK");
        DO_LOG("elapsed: " << elapsed << ", period: " << dll_.period()
               << ", samplerate: " << dll_.samplerate());
    #endif
    }

    // pre-allocate event array (max. 1 per source)
    aoo_event *events = (aoo_event *)alloca(sizeof(aoo_event) * AOO_MAXNUMEVENTS);
    size_t numevents = 0;

    // the mutex is uncontended most of the time, but LATER we might replace
    // this with a lockless and/or waitfree solution
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& src : sources_){
        if (!src.decoder){
            continue;
        }
        double sr = src.decoder->samplerate();
        int32_t nchannels = src.decoder->nchannels();
        int32_t nsamples = src.audioqueue.blocksize();
        // write samples into resampler
        while (src.audioqueue.read_available() && src.infoqueue.read_available()
               && src.resampler.write_available() >= nsamples){
        #if AOO_DEBUG_RESAMPLING
            if (debug_counter == 0){
                DO_LOG("read available: " << src.audioqueue.read_available());
            }
        #endif
            auto info = src.infoqueue.read();
            src.channel = info.channel;
            src.samplerate = info.sr;
            src.resampler.write(src.audioqueue.read_data(), nsamples);
            src.audioqueue.read_commit();
            // check state
            if (info.state != src.laststate && numevents < AOO_MAXNUMEVENTS){
                aoo_event& event = events[numevents++];
                event.source_state.type = AOO_SOURCE_STATE_EVENT;
                event.source_state.endpoint = src.endpoint;
                event.source_state.id = src.id;
                event.source_state.state = info.state;

                src.laststate = info.state;
            }
        }
        // update resampler
        src.resampler.update(src.samplerate, dll_.samplerate());
        // read samples from resampler
        auto readsamples = blocksize_ * nchannels;
        if (src.resampler.read_available() >= readsamples){
            auto buf = (aoo_sample *)alloca(readsamples * sizeof(aoo_sample));
            src.resampler.read(buf, readsamples);

            // sum source into sink (interleaved -> non-interleaved),
            // starting at the desired sink channel offset.
            // out of bound source channels are silently ignored.
            for (int i = 0; i < nchannels; ++i){
                auto chn = i + src.channel;
                // ignore out-of-bound source channels!
                if (chn < nchannels_){
                    for (int j = 0; j < blocksize_; ++j){
                        buffer_[chn * blocksize_ + j] += buf[j * nchannels + i];
                    }
                }
            }
            LOG_DEBUG("read samples");
            didsomething = true;
        } else {
            // buffer ran out -> send "stop" event
            if (src.laststate != AOO_SOURCE_STOP && numevents < AOO_MAXNUMEVENTS){
                aoo_event& event = events[numevents++];
                event.source_state.type = AOO_SOURCE_STATE_EVENT;
                event.source_state.endpoint = src.endpoint;
                event.source_state.id = src.id;
                event.source_state.state = AOO_SOURCE_STOP;

                src.laststate = AOO_SOURCE_STOP;
                didsomething = true;
            }
        }
    }
    lock.unlock();

    if (didsomething){
    #if AOO_CLIP_OUTPUT
        for (auto it = buffer_.begin(); it != buffer_.end(); ++it){
            if (*it > 1.0){
                *it = 1.0;
            } else if (*it < -1.0){
                *it = -1.0;
            }
        }
    #endif
        // set buffer pointers and pass to audio callback
        auto vec = (const aoo_sample **)alloca(sizeof(aoo_sample *) * nchannels_);
        for (int i = 0; i < nchannels_; ++i){
            vec[i] = &buffer_[i * blocksize_];
        }
        processfn_(user_, vec, blocksize_, events, numevents);
        return 1;
    } else {
        return 0;
    }
}

namespace aoo {

/*////////////////////////// block /////////////////////////////*/

block::block(int32_t seq, double sr, int32_t chn,
             int32_t nbytes, int32_t nframes)
    : sequence(seq), samplerate(sr), channel(chn),
      numframes_(nframes), framesize_(0)
{
    assert(nbytes > 0);
    buffer_.resize(nbytes);
    // set missing frame bits to 1
    frames_ = 0;
    for (int i = 0; i < nframes; ++i){
        frames_ |= (1 << (uint64_t)i);
    }
    // LOG_DEBUG("initial frames: " << (unsigned)frames);
}

void block::set(int32_t seq, double sr, int32_t chn,
                const char *data, int32_t nbytes,
                int32_t nframes, int32_t framesize)
{
    sequence = seq;
    samplerate = sr;
    channel = chn;
    numframes_ = nframes;
    framesize_ = framesize;
    frames_ = 0; // no frames missing
    buffer_.assign(data, data + nbytes);
}

bool block::complete() const {
    assert(buffer_.data() != nullptr);
    assert(sequence >= 0);
    return frames_ == 0;
}

void block::add_frame(int32_t which, const char *data, int32_t n){
    assert(data != nullptr);
    assert(buffer_.data() != nullptr);
    if (which == numframes_ - 1){
        LOG_DEBUG("copy last frame with " << n << " bytes");
        std::copy(data, data + n, buffer_.end() - n);
    } else {
        LOG_DEBUG("copy frame " << which << " with " << n << " bytes");
        std::copy(data, data + n, buffer_.begin() + which * n);
        framesize_ = n; // LATER allow varying framesizes
    }
    frames_ &= ~(1 << which);
    LOG_DEBUG("frames: " << frames_);
}

void block::get_frame(int32_t which, const char *&data, int32_t &n){
    auto onset = which * framesize_;
    data = buffer_.data() + onset;
    if (which == size() - 1){ // last frame
        n = size() - onset;
    } else {
        n = framesize_;
    }
}

    }
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
    assert(capacity() > 0);
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
            LOG_DEBUG("insert block at pos " << pos << " and pop old block");
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
        LOG_DEBUG("insert block at pos " << pos);
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
    assert(!empty());
    return blocks_.front();
}

block& block_queue::back(){
    assert(!empty());
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

/*////////////////////////// dynamic_resampler /////////////////////////////*/

#define AOO_RESAMPLER_SPACE 3

void dynamic_resampler::setup(int32_t nfrom, int32_t nto, int32_t srfrom, int32_t srto, int32_t nchannels){
    nchannels_ = nchannels;
    auto blocksize = std::max<int32_t>(nfrom, nto);
#if 0
    // this doesn't work as expected...
    auto ratio = srfrom > srto ? (double)srfrom / (double)srto : (double)srto / (double)srfrom;
    buffer_.resize(blocksize * nchannels_ * ratio * AOO_RESAMPLER_SPACE); // extra space for fluctuations
#else
    buffer_.resize(blocksize * nchannels_ * AOO_RESAMPLER_SPACE); // extra space for fluctuations
#endif
    clear();
}

void dynamic_resampler::clear(){
    ratio_ = 1;
    rdpos_ = 0;
    wrpos_ = 0;
    balance_ = 0;
}

void dynamic_resampler::update(double srfrom, double srto){
    if (srfrom == srto){
        ratio_ = 1;
    } else {
        ratio_ = srto / srfrom;
    }
#if AOO_DEBUG_RESAMPLING
    if (debug_counter == 100){
        DO_LOG("srfrom: " << srfrom << ", srto: " << srto);
        DO_LOG("resample factor: " << ratio_);
        DO_LOG("balance: " << balance_ << ", size: " << buffer_.size());
        debug_counter = 0;
    } else {
        debug_counter++;
    }
#endif
}

int32_t dynamic_resampler::write_available(){
    return (double)buffer_.size() - balance_ + 0.5; // !
}

void dynamic_resampler::write(const aoo_sample *data, int32_t n){
    auto size = (int32_t)buffer_.size();
    auto end = wrpos_ + n;
    int32_t n1, n2;
    if (end > size){
        n1 = size - wrpos_;
        n2 = end - size;
    } else {
        n1 = n;
        n2 = 0;
    }
    std::copy(data, data + n1, &buffer_[wrpos_]);
    std::copy(data + n1, data + n, &buffer_[0]);
    wrpos_ += n;
    if (wrpos_ >= size){
        wrpos_ -= size;
    }
    balance_ += n;
}

int32_t dynamic_resampler::read_available(){
    return balance_ * ratio_;
}

void dynamic_resampler::read(aoo_sample *data, int32_t n){
    auto size = (int32_t)buffer_.size();
    auto limit = size / nchannels_;
    int32_t intpos = (int32_t)rdpos_;
    if (ratio_ != 1.0 || (rdpos_ - intpos) != 0.0){
        // interpolating version
        double incr = 1. / ratio_;
        assert(incr > 0);
        for (int i = 0; i < n; i += nchannels_){
            int32_t index = (int32_t)rdpos_;
            double fract = rdpos_ - (double)index;
            for (int j = 0; j < nchannels_; ++j){
                double a = buffer_[index * nchannels_ + j];
                double b = buffer_[((index + 1) * nchannels_ + j) % size];
                data[i + j] = a + (b - a) * fract;
            }
            rdpos_ += incr;
            if (rdpos_ >= limit){
                rdpos_ -= limit;
            }
        }
        balance_ -= n * incr;
    } else {
        // non-interpolating (faster) version
        int32_t pos = intpos * nchannels_;
        int32_t end = pos + n;
        int n1, n2;
        if (end > size){
            n1 = size - pos;
            n2 = end - size;
        } else {
            n1 = n;
            n2 = 0;
        }
        std::copy(&buffer_[pos], &buffer_[pos + n1], data);
        std::copy(&buffer_[0], &buffer_[n2], data + n1);
        rdpos_ += n / nchannels_;
        if (rdpos_ >= limit){
            rdpos_ -= limit;
        }
        balance_ -= n;
    }
}

/*////////////////////////// source_desc /////////////////////////////*/

source_desc::source_desc(void *_endpoint, aoo_replyfn _fn, int32_t _id, int32_t _salt)
    : endpoint(_endpoint), fn(_fn), id(_id), salt(_salt), laststate(AOO_SOURCE_STOP) {}

void source_desc::send(const char *data, int32_t n){
    fn(endpoint, data, n);
}

} // aoo

void aoo_setup(){
    aoo_codec_pcm_setup(aoo::register_codec);
    aoo_codec_opus_setup(aoo::register_codec);
}

void aoo_close() {}
