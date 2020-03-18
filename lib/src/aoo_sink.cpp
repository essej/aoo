#include "aoo_sink.hpp"
#include "aoo/aoo_utils.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#include <algorithm>

namespace aoo {

/*////////////////////////// source_desc /////////////////////////////*/

source_desc::source_desc(void *_endpoint, aoo_replyfn _fn, int32_t _id, int32_t _salt)
    : endpoint(_endpoint), fn(_fn), id(_id), salt(_salt), laststate(AOO_SOURCE_STATE_STOP) {}

void source_desc::send(const char *data, int32_t n){
    fn(endpoint, data, n);
}

} // aoo

/*//////////////////// aoo_sink /////////////////////*/

namespace aoo {

isink * isink::create(int32_t id){
    return new aoo_sink(id);
}

void isink::destroy(isink *x){
    delete x;
}

} // aoo

aoo_sink * aoo_sink_new(int32_t id) {
    return new aoo_sink(id);
}

void aoo_sink_free(aoo_sink *sink) {
    delete sink;
}

int32_t aoo_sink_setup(aoo_sink *sink, const aoo_sink_settings *settings) {
    if (settings){
        return sink->setup(*settings);
    }
    return 0;
}

int32_t aoo_sink::setup(const aoo_sink_settings& settings){
    processfn_ = settings.processfn;
    eventhandler_ = settings.eventhandler;
    user_ = settings.userdata;
    if (settings.nchannels > 0
            && settings.samplerate > 0
            && settings.blocksize > 0)
    {
        // only update if at least one value has changed
        if (settings.nchannels != nchannels_
                || settings.samplerate != samplerate_
                || settings.blocksize != blocksize_)
        {
            nchannels_ = settings.nchannels;
            samplerate_ = settings.samplerate;
            blocksize_ = settings.blocksize;

            buffer_.resize(blocksize_ * nchannels_);
            // don't need to lock
            update_sources();
        }

        // always reset time DLL to be on the safe side
        starttime_ = 0; // will update

        return 1;
    }
    return 0;
}

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

#define CHECKARG(type) assert(size == sizeof(type))

int32_t aoo_sink_setoption(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->set_option(opt, p, size);
}

int32_t aoo_sink::set_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // buffer size
    case aoo_opt_buffersize:
    {
        CHECKARG(int32_t);
        auto bufsize = std::max<int32_t>(0, as<int32_t>(ptr));
        if (bufsize != buffersize_){
            buffersize_ = bufsize;
            // we don't need to lock (the user might!)
            update_sources();
        }
        break;
    }
    // ping interval
    case aoo_opt_ping_interval:
        CHECKARG(int32_t);
        ping_interval_ = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        break;
    // timefilter bandwidth
    case aoo_opt_timefilter_bandwidth:
        CHECKARG(float);
        bandwidth_ = std::max<double>(0, std::min<double>(1, as<float>(ptr)));
        starttime_ = 0; // will update time DLL and reset timer
        break;
    // packetsize
    case aoo_opt_packetsize:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = 64;
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
    // resend limit
    case aoo_opt_resend_limit:
        CHECKARG(int32_t);
        resend_limit_ = std::max<int32_t>(0, as<int32_t>(ptr));
        break;
    // resend interval
    case aoo_opt_resend_interval:
        CHECKARG(int32_t);
        resend_interval_ = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        break;
    // resend maxnumframes
    case aoo_opt_resend_maxnumframes:
        CHECKARG(int32_t);
        resend_maxnumframes_ = std::max<int32_t>(1, as<int32_t>(ptr));
        break;
    // unknown
    default:
        LOG_WARNING("aoo_sink: unknown option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_sink_getoption(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->get_option(opt, p, size);
}

int32_t aoo_sink::get_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // buffer size
    case aoo_opt_buffersize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = buffersize_;
        break;
    // ping interval
    case aoo_opt_ping_interval:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = ping_interval_;
        break;
    // timefilter bandwidth
    case aoo_opt_timefilter_bandwidth:
        CHECKARG(float);
        as<float>(ptr) = bandwidth_;
        break;
    // resend packetsize
    case aoo_opt_packetsize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packetsize_;
        break;
    // resend limit
    case aoo_opt_resend_limit:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_limit_;
        break;
    // resend interval
    case aoo_opt_resend_interval:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_interval_;
        break;
    // resend maxnumframes
    case aoo_opt_resend_maxnumframes:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_maxnumframes_;
        break;
    // unknown
    default:
        LOG_WARNING("aoo_sink: unknown option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_sink_setsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    return sink->set_sourceoption(endpoint, id, opt, p, size);
}

int32_t aoo_sink::set_sourceoption(void *endpoint, int32_t id,
                                   int32_t opt, void *ptr, int32_t size)
{
    auto src = find_source(endpoint, id);
    if (src){
        switch (opt){
        case aoo_opt_format:
            LOG_ERROR("aoo_sink: can't set source format!");
            return 0;
        default:
            LOG_WARNING("aoo_sink: unknown source option " << opt);
            return 0;
        }
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_sink_getsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    return sink->get_sourceoption(endpoint, id, opt, p, size);
}

int32_t aoo_sink::get_sourceoption(void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size)
{
    auto src = find_source(endpoint, id);
    if (src){
        switch (opt){
        case aoo_opt_format:
            CHECKARG(aoo_format_storage);
            if (src->decoder){
                return src->decoder->get_format(as<aoo_format_storage>(p));
            } else {
                return 0;
            }
            break;
        default:
            LOG_WARNING("aoo_sink: unknown source option " << opt);
            return 0;
        }
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_sink_handlemessage(aoo_sink *sink, const char *data, int32_t n,
                            void *src, aoo_replyfn fn) {
    return sink->handle_message(data, n, src, fn);
}

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
                aoo_format f;
                f.nchannels = (it++)->AsInt32();
                f.samplerate = (it++)->AsInt32();
                f.blocksize = (it++)->AsInt32();
                f.codec = (it++)->AsString();
                const void *blobdata;
                osc::osc_bundle_element_size_t blobsize;
                (it++)->AsBlob(blobdata, blobsize);

                handle_format_message(endpoint, fn, id, salt, f, (const char *)blobdata, blobsize);
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
                aoo::data_packet d;
                d.sequence = (it++)->AsInt32();
                d.samplerate = (it++)->AsDouble();
                d.channel = (it++)->AsInt32();
                d.totalsize = (it++)->AsInt32();
                d.nframes = (it++)->AsInt32();
                d.framenum = (it++)->AsInt32();
                const void *blobdata;
                osc::osc_bundle_element_size_t blobsize;
                (it++)->AsBlob(blobdata, blobsize);
                d.data = (const char *)blobdata;
                d.size = blobsize;

                handle_data_message(endpoint, fn, id, salt, d);
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

// /AoO/<sink>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <settings...>

void aoo_sink::handle_format_message(void *endpoint, aoo_replyfn fn,
                                     int32_t id, int32_t salt, const aoo_format& f,
                                     const char *settings, int32_t size){
    LOG_DEBUG("handle format message");

    auto update_format = [&](aoo::source_desc& src){
        if (!src.decoder || strcmp(src.decoder->name(), f.codec)){
            auto c = aoo::find_codec(f.codec);
            if (c){
                src.decoder = c->create_decoder();
            } else {
                LOG_ERROR("codec '" << f.codec << "' not supported!");
                return;
            }
            if (!src.decoder){
                LOG_ERROR("couldn't create decoder!");
                return;
            }
        }
        src.decoder->read_format(f.nchannels, f.samplerate, f.blocksize, settings, size);

        update_source(src);

        // called with mutex locked, so we don't have to synchronize with the process() method!
        aoo_event event;
        event.header.type = AOO_FORMAT_EVENT;
        event.header.endpoint = src.endpoint;
        event.header.id = src.id;
        src.eventqueue.write(event);
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
        auto src = find_source(endpoint, id);
        std::unique_lock<std::mutex> lock(mutex_); // !
        if (!src){
            // not found - add new source
            sources_.emplace_back(endpoint, fn, id, salt);
            src = &sources_.back();
        } else {
            src->salt = salt;
        }
        // update source
        update_format(*src);
    }
}

// /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

void aoo_sink::handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                                   int32_t salt, const aoo::data_packet& d){
    // first try to find existing source
    auto src = find_source(endpoint, id);
    // check if the 'salt' values match. the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    if (src && src->salt == salt){
        auto& queue = src->blockqueue;
        auto& acklist = src->ack_list;
    #if 1
        if (!src->decoder){
            LOG_DEBUG("ignore data message");
            return;
        }
    #else
        assert(src->decoder != nullptr);
    #endif
        LOG_DEBUG("got block: seq = " << d.sequence << ", sr = " << d.samplerate
                  << ", chn = " << d.channel << ", totalsize = " << d.totalsize
                  << ", nframes = " << d.nframes << ", frame = " << d.framenum << ", size " << d.size);

        if (src->next < 0){
            src->next = d.sequence;
        }

        if (d.sequence < src->next){
            // block too old, discard!
            LOG_VERBOSE("discarded old block " << d.sequence);
            return;
        }

        if (d.sequence < src->newest){
            // TODO the following distinction doesn't seem to work reliably.
            if (acklist.find(d.sequence)){
                LOG_DEBUG("resent block " << d.sequence);
                // record resending
                src->streamstate.resent++;
            } else {
                LOG_VERBOSE("block " << d.sequence << " out of order!");
                // record reordering
                src->streamstate.reordered++;
            }
        } else if ((d.sequence - src->newest) > 1){
            LOG_VERBOSE("skipped " << (d.sequence - src->newest - 1) << " blocks");
        }

        if (src->newest > 0 && (d.sequence - src->newest) > queue.capacity()){
            // too large gap between incoming block and most recent block.
            // either network problem or stream has temporarily stopped.

            // record dropped blocks
            src->streamstate.lost += queue.size();
            src->streamstate.gap += (d.sequence - src->newest - 1);
            // clear the block queue and fill audio buffer with zeros.
            queue.clear();
            acklist.clear();
            src->next = d.sequence;
            // push silent blocks to keep the buffer full, but leave room for one block!
            int count = 0;
            auto nsamples = src->audioqueue.blocksize();
            while (src->audioqueue.write_available() > 1 && src->infoqueue.write_available() > 1){
                auto ptr = src->audioqueue.write_data();
                for (int i = 0; i < nsamples; ++i){
                    ptr[i] = 0;
                }
                src->audioqueue.write_commit();
                // push nominal samplerate + default channel (0)
                aoo::source_desc::info i;
                i.sr = src->decoder->samplerate();
                i.channel = 0;
                src->infoqueue.write(i);

                count++;
            }
            LOG_VERBOSE("wrote " << count << " silent blocks for transmission gap");
        }
        auto block = queue.find(d.sequence);
        if (!block){
            if (queue.full()){
                // if the queue is full, we have to drop a block;
                // in this case we send a block of zeros to the audio buffer
                auto nsamples = src->audioqueue.blocksize();
                if (src->audioqueue.write_available() && src->infoqueue.write_available()){
                    auto ptr = src->audioqueue.write_data();
                    for (int i = 0; i < nsamples; ++i){
                        ptr[i] = 0;
                    }
                    src->audioqueue.write_commit();
                    // push nominal samplerate + default channel (0)
                    aoo::source_desc::info i;
                    i.sr = src->decoder->samplerate();
                    i.channel = 0;
                    src->infoqueue.write(i);
                }
                LOG_VERBOSE("dropped block " << queue.front().sequence);
                // remove block from acklist
                acklist.remove(queue.front().sequence);
                // record dropped block
                src->streamstate.lost++;
            }
            // add new block
            block = queue.insert(d.sequence, d.samplerate, d.channel, d.totalsize, d.nframes);
        } else if (block->has_frame(d.framenum)){
            LOG_VERBOSE("frame " << d.framenum << " of block " << d.sequence << " already received!");
            return;
        }

        // add frame to block
        block->add_frame(d.framenum, (const char *)d.data, d.size);

    #if 1
        if (block->complete()){
            // remove block from acklist as early as possible
            acklist.remove(block->sequence);
        }
    #endif

        // update newest sequence number
        if (d.sequence > src->newest){
            src->newest = d.sequence;
        }

        // Transfer all consecutive complete blocks as long as
        // no previous (expected) blocks are missing.
        if (!queue.empty()){
            block = queue.begin();
            int32_t count = 0;
            int32_t next = src->next;
            while ((block != queue.end()) && block->complete()
                   && (block->sequence == next)
                   && src->audioqueue.write_available() && src->infoqueue.write_available())
            {
                LOG_DEBUG("write samples (" << block->sequence << ")");

                auto ptr = src->audioqueue.write_data();
                auto nsamples = src->audioqueue.blocksize();
                assert(block->data() != nullptr && block->size() > 0 && ptr != nullptr && nsamples > 0);
                if (src->decoder->decode(block->data(), block->size(), ptr, nsamples) <= 0){
                    LOG_VERBOSE("bad block: size = " << block->size() << ", nsamples = " << nsamples);
                    // decoder failed - fill with zeros
                    std::fill(ptr, ptr + nsamples, 0);
                }

                src->audioqueue.write_commit();

                // push info
                aoo::source_desc::info i;
                i.sr = block->samplerate;
                i.channel = block->channel;
                src->infoqueue.write(i);

                next++;
                count++;
                block++;
            }
            src->next = next;
            // pop blocks
            while (count--){
            #if 0
                // remove block from acklist
                acklist.remove(queue.front().sequence);
            #endif
                // pop block
                LOG_DEBUG("pop block " << queue.front().sequence);
                queue.pop_front();
            }
            LOG_DEBUG("next: " << src->next);
        }

    #if 1
        // pop outdated blocks (shouldn't really happen...)
        while (!queue.empty() &&
               (src->newest - queue.front().sequence) >= queue.capacity())
        {
            auto old = queue.front().sequence;
            LOG_VERBOSE("pop outdated block " << old);
            // remove block from acklist
            acklist.remove(old);
            // pop block
            queue.pop_front();
            // update 'next'
            if (src->next <= old){
                src->next = old + 1;
            }
            // record dropped block
            src->streamstate.lost++;
        }
    #endif

        // deal with "holes" in block queue
        if (!queue.empty()){
        #if LOGLEVEL >= 3
            std::cerr << queue << std::endl;
        #endif
            int32_t numframes = 0;
            retransmit_list_.clear();

            // resend incomplete blocks except for the last block
            LOG_DEBUG("resend incomplete blocks");
            for (auto it = queue.begin(); it != (queue.end() - 1); ++it){
                if (!it->complete()){
                    // insert ack (if needed)
                    auto& ack = acklist.get(it->sequence);
                    if (ack.check(elapsedtime_.get(), resend_interval_)){
                        for (int i = 0; i < it->num_frames(); ++i){
                            if (!it->has_frame(i)){
                                if (numframes < resend_maxnumframes_){
                                    retransmit_list_.push_back(data_request { it->sequence, i });
                                    numframes++;
                                } else {
                                    goto resend_incomplete_done;
                                }
                            }
                        }
                    }
                }
            }
            resend_incomplete_done:

            // resend missing blocks before any (half)completed blocks
            LOG_DEBUG("resend missing blocks");
            int32_t next = src->next;
            for (auto it = queue.begin(); it != queue.end(); ++it){
                auto missing = it->sequence - next;
                if (missing > 0){
                    for (int i = 0; i < missing; ++i){
                        // insert ack (if necessary)
                        auto& ack = acklist.get(next + i);
                        if (ack.check(elapsedtime_.get(), resend_interval_)){
                            if (numframes + it->num_frames() <= resend_maxnumframes_){
                                retransmit_list_.push_back(data_request { next + i, -1 }); // whole block
                                numframes += it->num_frames();
                            } else {
                                goto resend_missing_done;
                            }
                        }
                    }
                } else if (missing < 0){
                    LOG_VERBOSE("bug: sequence = " << it->sequence << ", next = " << next);
                    assert(false);
                }
                next = it->sequence + 1;
            }
            resend_missing_done:

            assert(numframes <= resend_maxnumframes_);
            if (numframes > 0){
                LOG_DEBUG("requested " << numframes << " frames");
            }

            // request data
            request_data(*src);

        #if 1
            // clean ack list
            auto removed = acklist.remove_before(src->next);
            if (removed > 0){
                LOG_DEBUG("block_ack_list: removed " << removed << " outdated items");
            }
        #endif
        } else {
            if (!acklist.empty()){
                LOG_WARNING("bug: acklist not empty");
                acklist.clear();
            }
        }
    #if LOGLEVEL >= 3
        std::cerr << acklist << std::endl;
    #endif
        // ping source
        ping(*src);
    } else {
        // discard data and request format!
        request_format(endpoint, fn, id);
    }
}

aoo::source_desc * aoo_sink::find_source(void *endpoint, int32_t id){
    for (auto& src : sources_){
        if ((src.endpoint == endpoint) && (src.id == id)){
            return &src;
        }
    }
    return nullptr;
}

void aoo_sink::update_sources(){
    for (auto& src : sources_){
        update_source(src);
    }
}

void aoo_sink::update_source(aoo::source_desc &src){
    // resize audio ring buffer
    if (src.decoder && src.decoder->blocksize() > 0 && src.decoder->samplerate() > 0){
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
            LOG_DEBUG("write silent block");
            src.audioqueue.write_commit();
            // push nominal samplerate + default channel (0)
            aoo::source_desc::info i;
            i.sr = src.decoder->samplerate();
            i.channel = 0;
            src.infoqueue.write(i);
        };
        // reset event queue
        src.eventqueue.resize(AOO_EVENTQUEUESIZE, 1);
        // setup resampler
        src.resampler.setup(src.decoder->blocksize(), blocksize_,
                            src.decoder->samplerate(), samplerate_, src.decoder->nchannels());
        // resize block queue
        src.blockqueue.resize(nbuffers);
        src.newest = 0;
        src.next = -1;
        src.channel = 0;
        src.samplerate = src.decoder->samplerate();
        src.lastpingtime = 0;
        src.laststate = AOO_SOURCE_STATE_STOP;
        src.streamstate.reset();
        src.ack_list.setup(resend_limit_);
        src.ack_list.clear();
        LOG_DEBUG("update source " << src.id << ": sr = " << src.decoder->samplerate()
                    << ", blocksize = " << src.decoder->blocksize() << ", nchannels = "
                    << src.decoder->nchannels() << ", bufsize = " << nbuffers * nsamples);
    }
}

// /AoO/<src>/request <sink>

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

// /AoO/<src>/resend <sink> <salt> <seq0> <frame0> <seq1> <frame1> ...

void aoo_sink::request_data(aoo::source_desc& src){
    if (retransmit_list_.empty()){
        return;
    }
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t maxaddrsize = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_RESEND);
    char address[maxaddrsize];
    snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, src.id, AOO_RESEND);

    const int32_t maxdatasize = packetsize_ - maxaddrsize - 16; // id + salt + padding
    const int32_t maxrequests = maxdatasize / 10; // 2 * (int32_t + typetag)
    auto d = div(retransmit_list_.size(), maxrequests);

    auto dorequest = [&](const data_request* data, int32_t n){
        msg << osc::BeginMessage(address) << id_ << src.salt;
        for (int i = 0; i < n; ++i){
            msg << data[i].sequence << data[i].frame;
        }
        msg << osc::EndMessage;

        src.send(msg.Data(), msg.Size());
    };

    for (int i = 0; i < d.quot; ++i){
        dorequest(retransmit_list_.data() + i * maxrequests, maxrequests);
    }
    if (d.rem > 0){
        dorequest(retransmit_list_.data() + retransmit_list_.size() - d.rem, d.rem);
    }

    retransmit_list_.clear(); // not really necessary
}

// AoO/<id>/ping <sink>

void aoo_sink::ping(aoo::source_desc& src){
    if (ping_interval_ == 0){
        return;
    }
    auto now = elapsedtime_.get();
    if ((now - src.lastpingtime) > ping_interval_){
        char buffer[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream msg(buffer, sizeof(buffer));

        // make OSC address pattern
        const int32_t max_addr_size = sizeof(AOO_DOMAIN) + 16 + sizeof(AOO_PING);
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s/%d%s", AOO_DOMAIN, src.id, AOO_PING);

        msg << osc::BeginMessage(address) << id_ << osc::EndMessage;

        src.send(msg.Data(), msg.Size());

        src.lastpingtime = now;

        LOG_DEBUG("send ping");
    }
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
        elapsedtime_.reset();
    } else {
        auto elapsed = tt.to_double() - starttime_;
        dll_.update(elapsed);
    #if AOO_DEBUG_DLL
        DO_LOG("SINK");
        DO_LOG("elapsed: " << elapsed << ", period: " << dll_.period()
               << ", samplerate: " << dll_.samplerate());
    #endif
        elapsedtime_.set(elapsed);
    }

    // the mutex is uncontended most of the time, but LATER we might replace
    // this with a lockless and/or waitfree solution
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& src : sources_){
        if (!src.decoder){
            continue;
        }
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
            aoo::source_desc::info info;
            src.infoqueue.read(info);
            src.channel = info.channel;
            src.samplerate = info.sr;
            src.resampler.write(src.audioqueue.read_data(), nsamples);
            src.audioqueue.read_commit();

            // record stream state
            auto lost = std::atomic_exchange(&src.streamstate.lost, 0);
            auto reordered = std::atomic_exchange(&src.streamstate.reordered, 0);
            auto resent = std::atomic_exchange(&src.streamstate.resent, 0);
            auto gap = std::atomic_exchange(&src.streamstate.gap, 0);

            aoo_event event;
            event.header.endpoint = src.endpoint;
            event.header.id = src.id;
            if (lost > 0){
                // push packet loss event
                event.header.type = AOO_BLOCK_LOSS_EVENT;
                event.block_loss.count = lost;
                src.eventqueue.write(event);
            }
            if (reordered > 0){
                // push packet reorder event
                event.header.type = AOO_BLOCK_REORDER_EVENT;
                event.block_reorder.count = reordered;
                src.eventqueue.write(event);
            }
            if (resent > 0){
                // push packet resend event
                event.header.type = AOO_BLOCK_RESEND_EVENT;
                event.block_resend.count = resent;
                src.eventqueue.write(event);
            }
            if (gap > 0){
                // push packet gap event
                event.header.type = AOO_BLOCK_GAP_EVENT;
                event.block_gap.count = gap;
                src.eventqueue.write(event);
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
            didsomething = true;
            LOG_DEBUG("read samples");

            if (src.laststate != AOO_SOURCE_STATE_START){
                // push "start" event
                aoo_event event;
                event.header.type = AOO_SOURCE_STATE_EVENT;
                event.header.endpoint = src.endpoint;
                event.header.id = src.id;
                event.source_state.state = AOO_SOURCE_STATE_START;
                src.eventqueue.write(event);
                src.laststate = AOO_SOURCE_STATE_START;
            }
        } else {
            // buffer ran out -> push "stop" event
            if (src.laststate != AOO_SOURCE_STATE_STOP){
                aoo_event event;
                event.header.type = AOO_SOURCE_STATE_EVENT;
                event.header.endpoint = src.endpoint;
                event.header.id = src.id;
                event.source_state.state = AOO_SOURCE_STATE_STOP;
                src.eventqueue.write(event);
                src.laststate = AOO_SOURCE_STATE_STOP;
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
        processfn_(user_, vec, blocksize_);
        return 1;
    } else {
        return 0;
    }
}
int32_t aoo_sink_eventsavailable(aoo_sink *sink){
    return sink->events_available();
}

bool aoo_sink::events_available(){
    // the mutex is uncontended most of the time, but LATER we might replace
    // this with a lockless and/or waitfree solution
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& src : sources_){
        if (src.eventqueue.read_available() > 0){
            return true;
        }
    }
    return false;
}

int32_t aoo_sink_handleevents(aoo_sink *sink){
    return sink->handle_events();
}

int32_t aoo_sink::handle_events(){
    if (!eventhandler_){
        return 0;
    }
    int total = 0;
    // the mutex is uncontended most of the time, but LATER we might replace
    // this with a lockless and/or waitfree solution
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& src : sources_){
        auto n = src.eventqueue.read_available();
        if (n > 0){
            // copy events
            auto events = (aoo_event *)alloca(sizeof(aoo_event) * n);
            for (int i = 0; i < n; ++i){
                src.eventqueue.read(events[i]);
            }
            // send events
            eventhandler_(user_, events, n);
            total += n;
        }
    }
    return total;
}
