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
            unique_lock lock(update_mutex_); // writer lock!
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
        timer_.reset(); // will update
        break;
    // resend buffer size
    case aoo_opt_resend_buffersize:
    {
        CHECKARG(int32_t);
        // empty buffer is allowed! (no resending)
        auto bufsize = std::max<int32_t>(as<int32_t>(ptr), 0);
        if (bufsize != resend_buffersize_){
            resend_buffersize_ = bufsize;
            unique_lock lock(update_mutex_); // writer lock!
            update_historybuffer();
        }
        break;
    }
    // unknown
    default:
        LOG_WARNING("aoo_source: unsupported option " << opt);
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
            shared_lock lock(update_mutex_); // read lock!
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
        LOG_WARNING("aoo_source: unsupported option " << opt);
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
            shared_lock lock(sinklist_mutex_); // reader lock!
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
            LOG_WARNING("aoo_source: unsupported sink option " << opt);
            return 0;
        }
        return 1;
    } else {
        shared_lock lock(sinklist_mutex_); // reader lock!
        auto sink = find_sink(endpoint, id);
        if (sink){
            if (sink->id == AOO_ID_WILDCARD){
                LOG_WARNING("aoo_source: can't set individual sink option "
                            << opt << " because of wildcard");
                return 0;
            }

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

    shared_lock lock(sinklist_mutex_); // reader lock!
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
            LOG_WARNING("aoo_source: unsupported sink option " << opt);
            return 0;
        }
        return 1;
    } else {
        LOG_ERROR("aoo_source: couldn't get option " << opt
                  << " - sink " << id << " not found!");
        return 0;
    }
}

int32_t aoo_source_setup(aoo_source *src, const aoo_source_settings *settings){
    if (settings){
        return src->setup(*settings);
    }
    return 0;
}

int32_t aoo::source::setup(const aoo_source_settings &settings){
    unique_lock lock(update_mutex_); // writer lock!
    if (settings.nchannels > 0
            && settings.samplerate > 0
            && settings.nchannels > 0)
    {
        eventhandler_ = settings.eventhandler;
        user_ = settings.userdata;
        nchannels_ = settings.nchannels;
        samplerate_ = settings.samplerate;
        blocksize_ = settings.blocksize;

        // reset timer + time DLL filter
        timer_.setup(samplerate_, blocksize_);

        update();

        return 1;
    }

    return 0;
}

int32_t aoo_source_addsink(aoo_source *src, void *sink, int32_t id, aoo_replyfn fn) {
    return src->add_sink(sink, id, fn);
}

int32_t aoo::source::add_sink(void *endpoint, int32_t id, aoo_replyfn fn){
    unique_lock lock(sinklist_mutex_); // writer lock!
    if (id == AOO_ID_WILDCARD){
        // first remove all sinks on the given endpoint!
        auto it = std::remove_if(sinks_.begin(), sinks_.end(), [&](auto& s){
            return s.endpoint == endpoint;
        });
        sinks_.erase(it, sinks_.end());
    } else {
        // check if sink exists!
        auto result = find_sink(endpoint, id);
        if (result){
            if (result->id == AOO_ID_WILDCARD){
                LOG_WARNING("aoo_source: can't add individual sink "
                            << id << " because of wildcard!");
            } else {
                LOG_WARNING("aoo_source: sink already added!");
            }
            return 0;
        }
    }
    // add sink descriptor
    sinks_.emplace_back(endpoint, fn, id);

    return 1;
}

int32_t aoo_source_removesink(aoo_source *src, void *sink, int32_t id) {
    return src->remove_sink(sink, id);
}

int32_t aoo::source::remove_sink(void *endpoint, int32_t id){
    unique_lock lock(sinklist_mutex_); // writer lock!
    if (id == AOO_ID_WILDCARD){
        // remove all sinks on the given endpoint
        auto it = std::remove_if(sinks_.begin(), sinks_.end(), [&](auto& s){
            return s.endpoint == endpoint;
        });
        sinks_.erase(it, sinks_.end());
        return 1;
    } else {
        for (auto it = sinks_.begin(); it != sinks_.end(); ++it){
            if (it->endpoint == endpoint){
                if (it->id == AOO_ID_WILDCARD){
                    LOG_WARNING("aoo_source: can't remove individual sink "
                                << id << " because of wildcard!");
                    return 0;
                } else if (it->id == id){
                    sinks_.erase(it);
                    return 1;
                }
            }
        }
        LOG_WARNING("aoo_source: sink not found!");
        return 0;
    }
}

void aoo_source_removeall(aoo_source *src) {
    src->remove_all();
}

void aoo::source::remove_all(){
    unique_lock lock(sinklist_mutex_); // writer lock!
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

int32_t aoo_source_send(aoo_source *src) {
    return src->send();
}

// This method reads audio samples from the ringbuffer,
// encodes them and sends them to all sinks.
// We have to aquire both the update lock and the sink list lock
// and release both before calling the sink's send method
// to avoid possible deadlocks in the client code.
// We have to make a local copy of the client list, but this should be
// rather cheap in comparison to encoding and sending the audio data.
int32_t aoo::source::send(){
    shared_lock updatelock(update_mutex_); // reader lock!
    if (!encoder_){
        return 0;
    }

    int32_t salt = salt_;
    data_packet d;

    // *first* check for dropped blocks
    // NOTE: there's no ABA problem because the variable will only be decremented in this method.
    if (dropped_ > 0){
        // send empty block
        d.sequence = sequence_++;
        d.samplerate = encoder_->samplerate(); // use nominal samplerate
        d.totalsize = 0;
        d.nframes = 0;
        d.framenum = 0;
        d.data = nullptr;
        d.size = 0;
        // save to unlock
        updatelock.unlock();

        // make local copy of sink descriptors
        shared_lock listlock(sinklist_mutex_);
        int32_t numsinks = sinks_.size();
        auto sinks = (sink_desc *)alloca(numsinks * sizeof(sink_desc));
        std::copy(sinks_.begin(), sinks_.end(), sinks);

        // unlock before sending!
        listlock.unlock();

        // send block to sinks
        for (int i = 0; i < numsinks; ++i){
            sinks[i].send_data(id_, salt, d);
        }
        --dropped_;
    } else if (audioqueue_.read_available() && srqueue_.read_available()){
        // make local copy of sink descriptors
        shared_lock listlock(sinklist_mutex_);
        int32_t numsinks = sinks_.size();
        auto sinks = (sink_desc *)alloca((numsinks + 1) * sizeof(sink_desc)); // avoid alloca(0)
        bool need_format = false;
        for (int i = 0; i < numsinks; ++i){
            // placement new
            new (&sinks[i]) sink_desc(sinks_[i]);
            // check if at least one sink needs the format
            if (sinks_[i].format_changed){
                need_format = true;
                // clear flag in original sink descriptor!
                sinks_[i].format_changed = false;
            }
        }
        // unlock before sending!
        listlock.unlock();

        d.sequence = sequence_++;
        srqueue_.read(d.samplerate); // read samplerate from ringbuffer

        if (numsinks){
            // copy and convert audio samples to blob data
            auto nchannels = encoder_->nchannels();
            auto blocksize = encoder_->blocksize();
            blockbuffer_.resize(sizeof(double) * nchannels * blocksize); // overallocate

            d.totalsize = encoder_->encode(audioqueue_.read_data(), audioqueue_.blocksize(),
                                           blockbuffer_.data(), blockbuffer_.size());
            audioqueue_.read_commit();

            if (d.totalsize > 0){
                // calculate number of frames
                auto maxpacketsize = packetsize_ - AOO_DATA_HEADERSIZE;
                auto dv = div(d.totalsize, maxpacketsize);
                d.nframes = dv.quot + (dv.rem != 0);

                // save block
                history_.push(d.sequence, d.samplerate, blockbuffer_.data(),
                              d.totalsize, d.nframes, maxpacketsize);

                // send format if needed
                if (need_format){
                    LOG_VERBOSE("need format");
                    // get format
                    char options[AOO_CODEC_MAXSETTINGSIZE];
                    aoo_format f;
                    f.codec = encoder_->name();
                    auto size = encoder_->write_format(f.nchannels, f.samplerate, f.blocksize,
                                                       options, AOO_CODEC_MAXSETTINGSIZE);
                    // unlock before sending!
                    updatelock.unlock();
                    // send format to sinks
                    for (int i = 0; i < numsinks; ++i){
                        if (sinks[i].format_changed){
                            sinks[i].send_format(id_, salt, f, options, size);
                        }
                    }
                } else {
                    // unlock before sending!
                    updatelock.unlock();
                }

                // from here on we don't hold any lock!

                // send a single frame to all sinks
                // /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>
                auto dosend = [&](int32_t frame, const char* data, auto n){
                    d.framenum = frame;
                    d.data = data;
                    d.size = n;
                    for (int i = 0; i < numsinks; ++i){
                        sinks[i].send_data(id_, salt, d);
                    }
                };

                auto ptr = blockbuffer_.data();
                // send large frames (might be 0)
                for (int32_t i = 0; i < dv.quot; ++i, ptr += maxpacketsize){
                    dosend(i, ptr, maxpacketsize);
                }
                // send remaining bytes as a single frame (might be the only one!)
                if (dv.rem){
                    dosend(dv.quot, ptr, dv.rem);
                }
            } else {
                LOG_WARNING("aoo_source: couldn't encode audio data!");
            }
        } else {
            // drain buffer anyway
            audioqueue_.read_commit();
        }
    } else {
        // LOG_DEBUG("couldn't send");
        return 0;
    }

    // handle overflow (with 64 samples @ 44.1 kHz this happens every 36 days)
    // for now just force a reset by changing the salt, LATER think how to handle this better
    if (d.sequence == INT32_MAX){
        unique_lock lock2(update_mutex_); // take writer lock
        salt_ = make_salt();
    }

    return 1;
}

int32_t aoo_source_process(aoo_source *src, const aoo_sample **data, int32_t n, uint64_t t) {
    return src->process(data, n, t);
}

int32_t aoo::source::process(const aoo_sample **data, int32_t n, uint64_t t){
    // update time DLL filter
    double error;
    auto state = timer_.update(t, error);
    if (state == timer::state::reset){
        LOG_VERBOSE("setup time DLL filter for source");
        dll_.setup(samplerate_, blocksize_, bandwidth_, 0);
    } else if (state == timer::state::error){
        // skip blocks
        double period = (double)blocksize_ / (double)samplerate_;
        int nblocks = error / period + 0.5;
        LOG_VERBOSE("skip " << nblocks << " blocks");
        dropped_ += nblocks;
        timer_.reset();
    } else {
        auto elapsed = timer_.get_elapsed();
        dll_.update(elapsed);
    #if AOO_DEBUG_DLL
        DO_LOG("time elapsed: " << elapsed << ", period: " << dll_.period()
               << ", samplerate: " << dll_.samplerate());
    #endif
    }

    // the mutex should be uncontended most of the time.
    // NOTE: We could use try_lock() and skip the block if we couldn't aquire the lock.
    shared_lock lock(update_mutex_);

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
    } else {
        // bypass resampler
        if (audioqueue_.write_available() && srqueue_.write_available()){
            // copy audio samples
            std::copy(buf, buf + outsamples, audioqueue_.write_data());
            audioqueue_.write_commit();

            // push samplerate
            srqueue_.write(dll_.samplerate());
        } else {
            LOG_DEBUG("couldn't process");
        }
    }
    return 1;
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
    // always thread-safe
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

/*//////////////////////////////// sink_desc /////////////////////////////////////*/

// /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <nframes> <frame> <data>

void sink_desc::send_data(int32_t src, int32_t salt, const data_packet& data) const {
    send_data(id, src, salt, data);
}

void sink_desc::send_data(int32_t sink, int32_t src, int32_t salt, const aoo::data_packet& d) const{
    // call without lock!

    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    if (sink != AOO_ID_WILDCARD){
        const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_DATA);
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, sink, AOO_DATA);

        msg << osc::BeginMessage(address);
    } else {
        msg << osc::BeginMessage(AOO_DATA_WILDCARD);
    }

    LOG_DEBUG("send block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << channel.load() << ", totalsize = " << d.totalsize
              << ", nframes = " << d.nframes << ", frame = " << d.framenum << ", size " << d.size);

    msg << src << salt << d.sequence << d.samplerate << channel.load()
        << d.totalsize << d.nframes << d.framenum
        << osc::Blob(d.data, d.size) << osc::EndMessage;

    send(msg.Data(), msg.Size());
}

// /AoO/<sink>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <options...>

void sink_desc::send_format(int32_t src, int32_t salt, const aoo_format& f,
                            const char *options, int32_t size) const {
    send_format(id, src, salt, f, options, size);
}

void sink_desc::send_format(int32_t sink, int32_t src, int32_t salt, const aoo_format& f,
                            const char *options, int32_t size) const {
    // call without lock!
    LOG_DEBUG("send format to " << sink << " (salt = " << salt << ")");

    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // use 'id' instead of 'sink.id'! this is for cases where 'sink.id' is a wildcard
    // but we want to reply to an individual sink.
    if (sink != AOO_ID_WILDCARD){
        const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_FORMAT);
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, sink, AOO_FORMAT);

        msg << osc::BeginMessage(address);
    } else {
        msg << osc::BeginMessage(AOO_FORMAT_WILDCARD);
    }

    msg << src << salt << f.nchannels << f.samplerate << f.blocksize
        << f.codec << osc::Blob(options, size) << osc::EndMessage;

    send(msg.Data(), msg.Size());
}

/*///////////////////////// source ////////////////////////////////*/

sink_desc * source::find_sink(void *endpoint, int32_t id){
    for (auto& sink : sinks_){
        if ((sink.endpoint == endpoint) &&
            (sink.id == AOO_ID_WILDCARD || sink.id == id))
        {
            return &sink;
        }
    }
    return nullptr;
}

int32_t source::set_format(aoo_format &f){
    unique_lock lock(update_mutex_); // writer lock!
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

    update();

    return 1;
}

int32_t source::make_salt(){
    thread_local std::random_device dev;
    thread_local std::mt19937 mt(dev());
    std::uniform_int_distribution<int32_t> dist;
    return dist(mt);
}

// always called with upate_mutex_ locked!
void source::update(){
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
        timer_.reset();

        // Start new sequence and resend format.
        // We naturally want to do this when setting the format,
        // but it's good to also do it in setup() to eliminate
        // any timing gaps.
        salt_ = make_salt();
        sequence_ = 0;
        dropped_ = 0;
        for (auto& sink : sinks_){
            sink.format_changed = true;
        }
    }
}

void source::update_historybuffer(){
    if (samplerate_ > 0 && encoder_){
        double bufsize = (double)resend_buffersize_ * 0.001 * samplerate_;
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        history_.resize(nbuffers);
    }
}

void source::handle_request(void *endpoint, aoo_replyfn fn, int32_t id){
    shared_lock listlock(sinklist_mutex_); // reader lock!
    auto s = find_sink(endpoint, id);
    if (s){
        // Just resend format (the last format message might have been lost)
        // Use 'id' because we want to send to an individual sink!
        // ('sink.id' might be a wildcard)

        // first make local copy of sink
        sink_desc sink = *s;

        // unlock before sending!
        listlock.unlock();

        // get format
        shared_lock updatelock(update_mutex_);
        auto salt = salt_;
        char options[AOO_CODEC_MAXSETTINGSIZE];
        aoo_format f;
        f.codec = encoder_->name();
        auto size = encoder_->write_format(f.nchannels, f.samplerate, f.blocksize,
                                           options, AOO_CODEC_MAXSETTINGSIZE);
        // unlock before sending!
        updatelock.unlock();

        // send format to sink
        // use 'id' because sink_desc.id might be a wildcard!
        sink.send_format(id, id_, salt, f, options, size);
    } else {
        LOG_WARNING("aoo_source: ignoring '" << AOO_REQUEST << "' message - sink not found");
    }
}

void source::handle_resend(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt,
                               int32_t count, osc::ReceivedMessageArgumentIterator it){
    shared_lock updatelock(update_mutex_); // reader lock!
    if (!history_.capacity()){
        return;
    }
    if (salt != salt_){
        LOG_VERBOSE("aoo_source: ignoring '" << AOO_RESEND << "' - source has changed");
        return;
    }

    shared_lock listlock(sinklist_mutex_); // reader lock!
    auto s = find_sink(endpoint, id);
    if (!s){
        LOG_WARNING("aoo_source: ignoring '" << AOO_RESEND << "' message - sink not found");
        return;
    }

    // first make local copy of sink
    sink_desc sink = *s;

    // unlock before sending!
    listlock.unlock();

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
            // We use a buffer on the heap because blocks and even frames
            // can be quite large and we don't want them to sit on the stack.
            if (framenum < 0){
                // Copy whole block and save frame pointers.
                resendbuffer_.resize(d.totalsize);
                char *buf = resendbuffer_.data();
                char *frameptr[256];
                int32_t framesize[256];
                int32_t onset = 0;

                for (int i = 0; i < d.nframes; ++i){
                    auto nbytes = block->get_frame(i, buf + onset, d.totalsize - onset);
                    if (nbytes > 0){
                        frameptr[i] = buf + onset;
                        framesize[i] = nbytes;
                        onset += nbytes;
                    } else {
                        return; // error
                    }
                }
                // unlock before sending
                updatelock.unlock();

                // send frames to sink
                for (int i = 0; i < d.nframes; ++i){
                    d.framenum = i;
                    d.data = frameptr[i];
                    d.size = framesize[i];
                    // use 'id' because sink_desc.id might be a wildcard!
                    sink.send_data(id, id_, salt, d);
                }
            } else {
                // Copy a single frame
                if (framenum >= 0 && framenum < d.nframes){
                    int32_t size = block->frame_size(framenum);
                    resendbuffer_.resize(size);
                    block->get_frame(framenum, resendbuffer_.data(), size);
                    // unlock before sending
                    updatelock.unlock();

                    // send frame to sink
                    // use 'id' because sink_desc.id might be a wildcard!
                    d.framenum = framenum;
                    d.data = resendbuffer_.data();
                    d.size = size;
                    // use 'id' because sink_desc.id might be a wildcard!
                    sink.send_data(id, id_, salt, d);
                } else {
                    LOG_ERROR("frame number " << framenum << " out of range!");
                }
            }
            // lock again
            updatelock.lock();
        } else {
            LOG_VERBOSE("couldn't find block " << seq);
        }
    }
}

void source::handle_ping(void *endpoint, aoo_replyfn fn, int32_t id){
    shared_lock lock(sinklist_mutex_); // reader lock!
    auto sink = find_sink(endpoint, id);
    if (sink){
        // push ping event
        aoo_event event;
        event.type = AOO_PING_EVENT;
        event.sink.endpoint = endpoint;
        // Use 'id' because we want the individual sink! ('sink.id' might be a wildcard)
        event.sink.id = id;
        eventqueue_.write(event);
    } else {
        LOG_WARNING("ignoring '" << AOO_PING << "' message: sink not found");
    }
}

} // aoo
