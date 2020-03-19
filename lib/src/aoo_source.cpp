#include "aoo_source.hpp"
#include "aoo/aoo_utils.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#include <cstring>
#include <algorithm>
#include <random>

/*//////////////////// AoO source /////////////////////*/

#define AOO_DATA_HEADERSIZE 80
// address pattern string: max 32 bytes
// typetag string: max. 12 bytes
// args (without blob data): 36 bytes

aoo_source * aoo_source_new(int32_t id) {
    return new aoo::source(id);
}

aoo::source::source(int32_t id)
    : id_(id)
{
    // event queue
    eventqueue_.resize(AOO_EVENTQUEUESIZE, 1);
}

void aoo_source_free(aoo_source *src){
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::source *>(src);
}

aoo::source::~source() {}

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

#define CHECKARG(type) assert(size == sizeof(type))

int32_t aoo_source_setoption(aoo_source *src, int32_t opt, void *p, int32_t size)
{
    return src->set_option(opt, p, size);
}

int32_t aoo::source::set_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // format
    case aoo_opt_format:
        CHECKARG(aoo_format);
        return set_format(as<aoo_format>(ptr));
    // buffersize
    case aoo_opt_buffersize:
    {
        CHECKARG(int32_t);
        auto bufsize = std::max<int32_t>(as<int32_t>(ptr), 0);
        if (bufsize != buffersize_){
            buffersize_ = bufsize;
            update();
        }
        break;
    }
    // packetsize
    case aoo_opt_packetsize:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = AOO_DATA_HEADERSIZE + 64;
        auto packetsize = as<int32_t>(ptr);
        if (packetsize < minpacketsize){
            LOG_WARNING("packet size too small! setting to " << minpacketsize);
            packetsize_ = minpacketsize;
        } else if (packetsize > AOO_MAXPACKETSIZE){
            LOG_WARNING("packet size too large! setting to " << AOO_MAXPACKETSIZE);
            packetsize_ = AOO_MAXPACKETSIZE;
        } else {
            packetsize_ = packetsize;
        }
        break;
    }
    // timefilter bandwidth
    case aoo_opt_timefilter_bandwidth:
        CHECKARG(float);
        // time filter
        bandwidth_ = as<float>(ptr);
        starttime_ = 0; // will update
        break;
    // resend buffer size
    case aoo_opt_resend_buffersize:
    {
        CHECKARG(int32_t);
        // empty buffer is allowed! (no resending)
        auto bufsize = std::max<int32_t>(as<int32_t>(ptr), 0);
        if (bufsize != resend_buffersize_){
            resend_buffersize_ = bufsize;
            update_historybuffer();
        }
        break;
    }
    // unknown
    default:
        LOG_WARNING("aoo_source: unknown option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_source_getoption(aoo_source *src, int32_t opt, void *p, int32_t size)
{
    return src->get_option(opt, p, size);
}

int32_t aoo::source::get_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // format
    case aoo_opt_format:
        CHECKARG(aoo_format_storage);
        if (encoder_){
            return encoder_->get_format(as<aoo_format_storage>(ptr));
        } else {
            return 0;
        }
        break;
    // buffer size
    case aoo_opt_buffersize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = buffersize_;
        break;
    // time filter bandwidth
    case aoo_opt_timefilter_bandwidth:
        CHECKARG(float);
        as<float>(ptr) = bandwidth_;
        break;
    // resend buffer size
    case aoo_opt_resend_buffersize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_buffersize_;
        break;
    // packetsize
    case aoo_opt_packetsize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packetsize_;
        break;
    // unknown
    default:
        LOG_WARNING("aoo_source: unknown option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_source_setsinkoption(aoo_source *src, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    return src->set_sinkoption(endpoint, id, opt, p, size);
}

int32_t aoo::source::set_sinkoption(void *endpoint, int32_t id,
                                   int32_t opt, void *ptr, int32_t size)
{
    if (id == AOO_ID_WILDCARD){
        // set option on all sinks on the given endpoint
        switch (opt){
        // channel onset
        case aoo_opt_channelonset:
        {
            CHECKARG(int32_t);
            auto chn = as<int32_t>(ptr);
            for (auto& sink : sinks_){
                if (sink.endpoint == endpoint){
                    sink.channel = chn;
                }
            }
            LOG_VERBOSE("aoo_source: send to all sinks on channel " << chn);
            break;
        }
        // unknown
        default:
            LOG_WARNING("aoo_source: unknown sink option " << opt);
            return 0;
        }
        return 1;
    } else {
        auto sink = find_sink(endpoint, id);
        if (sink){
            switch (opt){
            // channel onset
            case aoo_opt_channelonset:
            {
                CHECKARG(int32_t);
                auto chn = as<int32_t>(ptr);
                sink->channel = chn;
                LOG_VERBOSE("aoo_source: send to sink " << sink->id
                            << " on channel " << chn);
                break;
            }
            // unknown
            default:
                LOG_WARNING("aoo_source: unknown sink option " << opt);
                return 0;
            }
            return 1;
        } else {
            LOG_ERROR("aoo_source: couldn't set option " << opt
                      << " - sink not found!");
            return 0;
        }
    }
}

int32_t aoo_source_getsinkoption(aoo_source *src, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    return src->get_sinkoption(endpoint, id, opt, p, size);
}

int32_t aoo::source::get_sinkoption(void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    if (id == AOO_ID_WILDCARD){
        LOG_ERROR("aoo_source: can't use wildcard to get sink option");
        return 0;
    }

    auto sink = find_sink(endpoint, id);
    if (sink){
        switch (opt){
        // channel onset
        case aoo_opt_channelonset:
            CHECKARG(int32_t);
            as<int32_t>(p) = sink->channel;
            break;
        // unknown
        default:
            LOG_WARNING("aoo_source: unknown sink option " << opt);
            return 0;
        }
        return 1;
    } else {
        LOG_ERROR("aoo_source: couldn't get option " << opt
                  << " - sink not found!");
        return 0;
    }
}

int32_t aoo::source::set_format(aoo_format &f){
    salt_ = make_salt();

    if (!encoder_ || strcmp(encoder_->name(), f.codec)){
        auto codec = aoo::find_codec(f.codec);
        if (codec){
            encoder_ = codec->create_encoder();
        } else {
            LOG_ERROR("codec '" << f.codec << "' not supported!");
            return 0;
        }
        if (!encoder_){
            LOG_ERROR("couldn't create encoder!");
            return 0;
        }
    }
    encoder_->set_format(f);

    sequence_ = 0;

    update();

    for (auto& sink : sinks_){
        send_format(sink);
    }

    return 1;
}

int32_t aoo_source_setup(aoo_source *src, const aoo_source_settings *settings){
    if (settings){
        return src->setup(*settings);
    }
    return 0;
}

int32_t aoo::source::setup(const aoo_source_settings &settings){
    if (settings.nchannels > 0
            && settings.samplerate > 0
            && settings.nchannels > 0)
    {
        eventhandler_ = settings.eventhandler;
        user_ = settings.userdata;

        // only update if at least one value has changed
        if (settings.nchannels != nchannels_
                || settings.samplerate != samplerate_
                || settings.blocksize != blocksize_)
        {
            nchannels_ = settings.nchannels;
            samplerate_ = settings.samplerate;
            blocksize_ = settings.blocksize;

            update();
        }

        // always reset time DLL to be on the safe side
        starttime_ = 0; // will update
        return 1;
    }

    return 0;
}

void aoo::source::update(){
    if (!encoder_){
        return;
    }
    assert(encoder_->blocksize() > 0 && encoder_->samplerate() > 0);

    if (blocksize_ > 0){
        assert(samplerate_ > 0 && nchannels_ > 0);
        // setup audio buffer
        auto nsamples = encoder_->blocksize() * nchannels_;
        double bufsize = (double)buffersize_ * encoder_->samplerate() * 0.001;
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        nbuffers = std::max<int32_t>(nbuffers, 1); // need at least 1 buffer!
        audioqueue_.resize(nbuffers * nsamples, nsamples);
        srqueue_.resize(nbuffers, 1);
        LOG_DEBUG("aoo::source::update: nbuffers = " << nbuffers);

        // resampler
        if (blocksize_ != encoder_->blocksize() || samplerate_ != encoder_->samplerate()){
            resampler_.setup(blocksize_, encoder_->blocksize(),
                             samplerate_, encoder_->samplerate(), nchannels_);
            resampler_.update(samplerate_, encoder_->samplerate());
        } else {
            resampler_.clear();
        }

        // history buffer
        update_historybuffer();

        // reset time DLL to be on the safe side
        starttime_ = 0; // will update
    }
}

aoo::source::sink_desc * aoo::source::find_sink(void *endpoint, int32_t id){
    for (auto& sink : sinks_){
        if ((sink.endpoint == endpoint) && (sink.id == id)){
            return &sink;
        }
    }
    return nullptr;
}

int32_t aoo_source_addsink(aoo_source *src, void *sink, int32_t id, aoo_replyfn fn) {
    return src->add_sink(sink, id, fn);
}

int32_t aoo::source::add_sink(void *endpoint, int32_t id, aoo_replyfn fn){
    if (id == AOO_ID_WILDCARD){
        // first remove all sinks on the given endpoint!
        remove_sink(endpoint, AOO_ID_WILDCARD);
    } else {
        if (find_sink(endpoint, id)){
            LOG_WARNING("aoo_source: sink already added!");
            return 0;
        }
    }
    // add sink descriptor
    sink_desc sd = { endpoint, fn, id, 0 };
    sinks_.push_back(sd);
    send_format(sd);
    return 1;
}

int32_t aoo_source_removesink(aoo_source *src, void *sink, int32_t id) {
    return src->remove_sink(sink, id);
}

int32_t aoo::source::remove_sink(void *endpoint, int32_t id){
    if (id == AOO_ID_WILDCARD){
        // remove all sinks on the given endpoint
        auto it = std::remove_if(sinks_.begin(), sinks_.end(), [&](auto& s){
            return s.endpoint == endpoint;
        });
        sinks_.erase(it, sinks_.end());
        return 1;
    } else {
        auto result = std::find_if(sinks_.begin(), sinks_.end(), [&](auto& s){
            return (s.endpoint == endpoint) && (s.id == id);
        });
        if (result != sinks_.end()){
            sinks_.erase(result);
            return 1;
        } else {
            LOG_WARNING("aoo_source: sink not found!");
            return 0;
        }
    }
}

void aoo_source_removeall(aoo_source *src) {
    src->remove_all();
}

void aoo::source::remove_all(){
    sinks_.clear();
}

int32_t aoo_source_handlemessage(aoo_source *src, const char *data, int32_t n,
                              void *sink, aoo_replyfn fn) {
    return src->handle_message(data, n, sink, fn);
}

// /AoO/<src>/request <sink>
int32_t aoo::source::handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn){
    osc::ReceivedPacket packet(data, n);
    osc::ReceivedMessage msg(packet);

    int32_t src = 0;
    auto onset = aoo_parsepattern(data, n, &src);
    if (!onset){
        LOG_WARNING("aoo_source: not an AoO message!");
        return 0;
    }
    if (src == AOO_ID_WILDCARD){
        LOG_WARNING("aoo_source: can't handle wildcard messages (yet)!");
        return 0;
    }
    if (src != id_){
        LOG_WARNING("aoo_source: wrong source ID!");
        return 0;
    }

    if (!strcmp(msg.AddressPattern() + onset, AOO_REQUEST)){
        if (msg.ArgumentCount() == 1){
            try {
                auto it = msg.ArgumentsBegin();
                auto id = it->AsInt32();
                handle_request(endpoint, fn, id);
                return 1;
            } catch (const osc::Exception& e){
                LOG_ERROR(e.what());
            }
        } else {
            LOG_ERROR("wrong number of arguments for /request message");
        }
    } else if (!strcmp(msg.AddressPattern() + onset, AOO_RESEND)){
        if (!history_.capacity()){
            return 0;
        }
        auto count = msg.ArgumentCount();
        if (count >= 4){
            try {
                auto it = msg.ArgumentsBegin();
                auto id = (it++)->AsInt32();
                auto salt = (it++)->AsInt32();

                handle_resend(endpoint, fn, id, salt, count - 2, it);

                return 1;
            } catch (const osc::Exception& e){
                LOG_ERROR(e.what());
            }
        } else {
            LOG_ERROR("bad number of arguments for /resend message");
        }
    } else if (!strcmp(msg.AddressPattern() + onset, AOO_PING)){
        try {
            auto it = msg.ArgumentsBegin();
            auto id = it->AsInt32();

            handle_ping(endpoint, fn, id);

            return 1;
        } catch (const osc::Exception& e){
            LOG_ERROR(e.what());
        }
    } else {
        LOG_WARNING("unknown message '" << (msg.AddressPattern() + onset) << "'");
    }
    return 0;
}

namespace aoo {

void source::handle_request(void *endpoint, aoo_replyfn fn, int32_t id){
    auto sink = find_sink(endpoint, id);
    if (sink){
        // just resend format (the last format message might have been lost)
        send_format(*sink);
    } else {
        LOG_WARNING("aoo_source: ignoring '/request' message - sink not found");
    }
}

void source::handle_resend(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt,
                               int32_t count, osc::ReceivedMessageArgumentIterator it){
    auto sink = find_sink(endpoint, id);
    if (!sink){
        LOG_WARNING("aoo_source: ignoring '/resend' message - sink not found");
        return;
    }
    if (salt != salt_){
        LOG_VERBOSE("aoo_source: ignoring '/resend' message - source has changed");
        return;
    }
    // get pairs of [seq, frame]
    int npairs = count / 2;
    while (npairs--){
        auto seq = (it++)->AsInt32();
        auto framenum = (it++)->AsInt32();
        auto block = history_.find(seq);
        if (block){
            aoo::data_packet d;
            d.sequence = block->sequence;
            d.samplerate = block->samplerate;
            d.totalsize = block->size();
            d.nframes = block->num_frames();
            if (framenum < 0){
                // whole block
                for (int i = 0; i < d.nframes; ++i){
                    d.framenum = i;
                    block->get_frame(i, d.data, d.size);
                    send_data(*sink, d);
                }
            } else {
                // single frame
                d.framenum = framenum;
                block->get_frame(framenum, d.data, d.size);
                send_data(*sink, d);
            }
        } else {
            LOG_VERBOSE("couldn't find block " << seq);
        }
    }
}

void source::handle_ping(void *endpoint, aoo_replyfn fn, int32_t id){
    auto sink = find_sink(endpoint, id);
    if (sink){
        // push ping event
        aoo_event event;
        event.type = AOO_PING_EVENT;
        event.sink.endpoint = endpoint;
        event.sink.id = id;
        eventqueue_.write(event);
    } else {
        LOG_WARNING("ignoring '/ping' message: sink not found");
    }
}

} // aoo

int32_t aoo_source_send(aoo_source *src) {
    return src->send();
}

int32_t aoo::source::send(){
    if (!encoder_){
        return 0;
    }

    if (audioqueue_.read_available() && srqueue_.read_available()){
        const auto nchannels = encoder_->nchannels();
        const auto blocksize = encoder_->blocksize();
        aoo::data_packet d;
        d.sequence = sequence_;
        srqueue_.read(d.samplerate);

        // copy and convert audio samples to blob data
        const auto blobmaxsize = sizeof(double) * nchannels * blocksize; // overallocate
        char * blobdata = (char *)alloca(blobmaxsize);

        d.totalsize = encoder_->encode(audioqueue_.read_data(), audioqueue_.blocksize(),
                                        blobdata, blobmaxsize);

        if (d.totalsize == 0){
            return 1; // ?
        }

        auto maxpacketsize = packetsize_ - AOO_DATA_HEADERSIZE;
        auto dv = div(d.totalsize, maxpacketsize);
        d.nframes = dv.quot + (dv.rem != 0);

        // save block
        history_.push(d.sequence, d.samplerate,
                      blobdata, d.totalsize, d.nframes, maxpacketsize);

        // send a single frame to all sink
        // /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>
        auto dosend = [&](int32_t frame, const char* data, auto n){
            d.framenum = frame;
            d.data = data;
            d.size = n;
            for (auto& sink : sinks_){
                send_data(sink, d);
            }
        };

        auto blobptr = blobdata;
        // send large frames (might be 0)
        for (int32_t i = 0; i < dv.quot; ++i, blobptr += maxpacketsize){
            dosend(i, blobptr, maxpacketsize);
        }
        // send remaining bytes as a single frame (might be the only one!)
        if (dv.rem){
            dosend(dv.quot, blobptr, dv.rem);
        }

        audioqueue_.read_commit(); // commit the read after sending!

        sequence_++;
        // handle overflow (with 64 samples @ 44.1 kHz this happens every 36 days)
        // for now just force a reset by changing the salt, LATER think how to handle this better
        if (sequence_ == INT32_MAX){
            salt_ = make_salt();
        }
        return 1;
    } else {
        // LOG_DEBUG("couldn't send");
        return 0;
    }
}

int32_t aoo_source_process(aoo_source *src, const aoo_sample **data, int32_t n, uint64_t t) {
    return src->process(data, n, t);
}

int32_t aoo::source::process(const aoo_sample **data, int32_t n, uint64_t t){
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

    if (!encoder_){
        return 0;
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

        return 1;
    } else {
        // bypass resampler
        if (audioqueue_.write_available() && srqueue_.write_available()){
            // copy audio samples
            std::copy(buf, buf + outsamples, audioqueue_.write_data());
            audioqueue_.write_commit();

            // push samplerate
            srqueue_.write(dll_.samplerate());

            return 1;
        } else {
            LOG_DEBUG("couldn't process");
            return 0;
        }
    }
}

int32_t aoo_source_eventsavailable(aoo_source *src){
    return src->events_available();
}

int32_t aoo::source::events_available(){
    return eventqueue_.read_available() > 0;
}

int32_t aoo_source_handleevents(aoo_source *src){
    return src->handle_events();
}

int32_t aoo::source::handle_events(){
    auto n = eventqueue_.read_available();
    if (n > 0){
        // copy events
        auto events = (aoo_event *)alloca(sizeof(aoo_event) * n);
        for (int i = 0; i < n; ++i){
            eventqueue_.read(events[i]);
        }
        // send events
        eventhandler_(user_, events, n);
    }
    return n;
}

namespace aoo {

// /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <nframes> <frame> <data>

void source::send_data(sink_desc& sink, const aoo::data_packet& d){
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    assert(d.data != nullptr);

    if (sink.id != AOO_ID_WILDCARD){
        const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_DATA);
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, sink.id, AOO_DATA);

        msg << osc::BeginMessage(address);
    } else {
        msg << osc::BeginMessage(AOO_DATA_WILDCARD);
    }

    LOG_DEBUG("send block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << sink.channel << ", totalsize = " << d.totalsize
              << ", nframes = " << d.nframes << ", frame = " << d.framenum << ", size " << d.size);

    msg << id_ << salt_ << d.sequence << d.samplerate << sink.channel
        << d.totalsize << d.nframes << d.framenum
        << osc::Blob(d.data, d.size) << osc::EndMessage;

    sink.send(msg.Data(), msg.Size());
}

// /AoO/<sink>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <options...>

void source::send_format(sink_desc &sink){
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
        auto setsize = encoder_->write_format(nchannels, samplerate, blocksize,
                                              settings, AOO_CODEC_MAXSETTINGSIZE);

        msg << id_ << salt_ << nchannels << samplerate << blocksize
            << encoder_->name() << osc::Blob(settings, setsize) << osc::EndMessage;

        sink.send(msg.Data(), msg.Size());
    }
}

int32_t source::make_salt(){
    thread_local std::random_device dev;
    thread_local std::mt19937 mt(dev());
    std::uniform_int_distribution<int32_t> dist;
    return dist(mt);
}

void source::update_historybuffer(){
    if (samplerate_ > 0 && encoder_){
        double bufsize = (double)resend_buffersize_ * 0.001 * samplerate_;
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        history_.resize(nbuffers);
    }
}

} // aoo
