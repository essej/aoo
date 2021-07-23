/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "source.hpp"

#include <cstring>
#include <algorithm>
#include <random>
#include <cmath>

const size_t kAooEventQueueSize = 8;

/*//////////////////// AoO source /////////////////////*/

// OSC data message
// address pattern string: max 32 bytes
// typetag string: max. 12 bytes
// args (without blob data): 36 bytes
#define kAooMsgDataHeaderSize 80

// binary data message:
// header: 12 bytes
// args: 48 bytes (max.)
#define kAooBinMsgDataHeaderSize 48

AOO_API AooSource * AOO_CALL AooSource_new(
        AooId id, AooFlag flags, AooError *err) {
    return aoo::construct<aoo::source_imp>(id, flags, err);
}

aoo::source_imp::source_imp(AooId id, AooFlag flags, AooError *err)
    : id_(id)
{
    // event queue
    eventqueue_.reserve(kAooEventQueueSize);
    // request queues
    // formatrequestqueue_.resize(64);
    // datarequestqueue_.resize(1024);
}

AOO_API void AOO_CALL AooSource_free(AooSource *src){
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::source_imp *>(src));
}

aoo::source_imp::~source_imp() {
    // flush event queue
    event e;
    while (eventqueue_.try_pop(e)){
        if (e.type_ == kAooEventFormatRequest){
            auto mem = memory_block::from_bytes((void *)e.format.format);
            memory_block::free(mem);
        }
    }
}

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

#define CHECKARG(type) assert(size == sizeof(type))

#define GETSINKARG \
    sink_lock lock(sinks_);             \
    auto sink = get_sink_arg(index);    \
    if (!sink) {                        \
        return kAooErrorUnknown;   \
    }                                   \

AOO_API AooError AOO_CALL AooSource_control(
        AooSource *src, AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    return src->control(ctl, index, ptr, size);
}

AooError AOO_CALL aoo::source_imp::control(
        AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    switch (ctl){
    // add sink
    case kAooCtlAddSink:
    {
        auto ep = (const AooEndpoint *)index;
        if (!ep){
            return kAooErrorUnknown;
        }
        return add_sink(*ep, 0); // ignore flags
    }
    // remove sink(s)
    case kAooCtlRemoveSink:
    {
        auto ep = (const AooEndpoint *)index;
        if (ep){
            // single sink
            return remove_sink(*ep);
        } else {
            // all sinks
            sink_lock lock(sinks_);
            sinks_.clear();
            return kAooOk;
        }
    }
    // start
    case kAooCtlStartStream:
        state_.store(stream_state::start);
        break;
    // stop
    case kAooCtlStopStream:
        state_.store(stream_state::stop);
        break;
    // set/get format
    case kAooCtlSetFormat:
        CHECKARG(AooFormat);
        return set_format(as<AooFormat>(ptr));
    case kAooCtlGetFormat:
    {
        assert(size >= sizeof(AooFormat));
        shared_lock lock(update_mutex_); // read lock!
        if (encoder_){
            return encoder_->get_format(as<AooFormat>(ptr), size);
        } else {
            return kAooErrorUnknown;
        }
    }
    // set/get channel onset
    case kAooCtlSetChannelOnset:
    {
        CHECKARG(int32_t);
        GETSINKARG;
        auto chn = as<int32_t>(ptr);
        sink->channel.store(chn);
        LOG_VERBOSE("aoo_source: send to sink " << sink->id
                    << " on channel " << chn);
        break;
    }
    case kAooCtlGetChannelOnset:
    {
        CHECKARG(int32_t);
        GETSINKARG;
        as<int32_t>(ptr) = sink->channel.load();
        break;
    }
    // id
    case kAooCtlSetId:
    {
        auto newid = as<int32_t>(ptr);
        if (id_.exchange(newid) != newid){
            // if playing, restart
            auto expected = stream_state::play;
            state_.compare_exchange_strong(expected, stream_state::start);
        }
        break;
    }
    case kAooCtlGetId:
        CHECKARG(int32_t);
        as<AooId>(ptr) = id();
        break;
    // set/get buffersize
    case kAooCtlSetBufferSize:
    {
        CHECKARG(AooSeconds);
        auto bufsize = std::max<AooSeconds>(as<AooSeconds>(ptr), 0);
        if (buffersize_.exchange(bufsize) != bufsize){
            scoped_lock lock(update_mutex_); // writer lock!
            update_audioqueue();
        }
        break;
    }
    case kAooCtlGetBufferSize:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = buffersize_.load();
        break;
    // set/get packetsize
    case kAooCtlSetPacketSize:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = kAooMsgDataHeaderSize + 64;
        auto packetsize = as<int32_t>(ptr);
        if (packetsize < minpacketsize){
            LOG_WARNING("packet size too small! setting to " << minpacketsize);
            packetsize_.store(minpacketsize);
        } else if (packetsize > AOO_MAX_PACKET_SIZE){
            LOG_WARNING("packet size too large! setting to " << AOO_MAX_PACKET_SIZE);
            packetsize_.store(AOO_MAX_PACKET_SIZE);
        } else {
            packetsize_.store(packetsize);
        }
        break;
    }
    case kAooCtlGetPacketSize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packetsize_.load();
        break;
    // set/get timer check
    case kAooCtlSetTimerCheck:
        CHECKARG(AooBool);
        timer_check_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetTimerCheck:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = timer_check_.load();
        break;
    // set/get dynamic resampling
    case kAooCtlSetDynamicResampling:
        CHECKARG(AooBool);
        dynamic_resampling_.store(as<AooBool>(ptr));
        timer_.reset(); // !
        break;
    case kAooCtlGetDynamicResampling:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = dynamic_resampling_.load();
        break;
    // set/get time DLL filter bandwidth
    case kAooCtlSetDllBandwidth:
        CHECKARG(float);
        // time filter
        dll_bandwidth_.store(as<float>(ptr));
        timer_.reset(); // will update
        break;
    case kAooCtlGetDllBandwidth:
        CHECKARG(float);
        as<float>(ptr) = dll_bandwidth_.load();
        break;
    // get real samplerate
    case kAooCtlGetRealSampleRate:
        CHECKARG(double);
        as<double>(ptr) = realsr_.load(std::memory_order_relaxed);
        break;
    // set/get ping interval
    case kAooCtlSetPingInterval:
    {
        CHECKARG(AooSeconds);
        auto interval = std::max<AooSeconds>(0, as<AooSeconds>(ptr));
        ping_interval_.store(interval);
        break;
    }
    case kAooCtlGetPingInterval:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = ping_interval_.load() * 1000.0;
        break;
    // set/get resend buffer size
    case kAooCtlSetResendBufferSize:
    {
        CHECKARG(AooSeconds);
        // empty buffer is allowed! (no resending)
        auto bufsize = std::max<AooSeconds>(as<AooSeconds>(ptr), 0);
        if (resend_buffersize_.exchange(bufsize) != bufsize){
            scoped_lock lock(update_mutex_); // writer lock!
            update_historybuffer();
        }
        break;
    }
    case kAooCtlGetResendBufferSize:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = resend_buffersize_.load();
        break;
    // set/get redundancy
    case kAooCtlSetRedundancy:
    {
        CHECKARG(int32_t);
        // limit it somehow, 16 times is already very high
        auto redundancy = std::max<int32_t>(1, std::min<int32_t>(16, as<int32_t>(ptr)));
        redundancy_.store(redundancy);
        break;
    }
    case kAooCtlGetRedundancy:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = redundancy_.load();
        break;
#if USE_AOO_NET
    case kAooCtlSetClient:
        client_ = reinterpret_cast<AooClient *>(index);
        break;
#endif
    // unknown
    default:
        LOG_WARNING("aoo_source: unsupported control " << ctl);
        return kAooErrorNotImplemented;
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_setup(
        AooSource *src, AooSampleRate samplerate,
        AooInt32 blocksize, AooInt32 nchannels){
    return src->setup(samplerate, blocksize, nchannels);
}

AooError AOO_CALL aoo::source_imp::setup(
        AooSampleRate samplerate, AooInt32 blocksize, AooInt32 nchannels){
    scoped_lock lock(update_mutex_); // writer lock!
    if (samplerate > 0 && blocksize > 0 && nchannels > 0)
    {
        if (samplerate != samplerate_ || blocksize != blocksize_ ||
            nchannels != nchannels_)
        {
            nchannels_ = nchannels;
            samplerate_ = samplerate;
            blocksize_ = blocksize;

            realsr_.store(samplerate);

            if (encoder_){
                update_audioqueue();

                if (need_resampling()){
                    update_resampler();
                }

                update_historybuffer();
            }

            start_new_stream();
        }

        // always reset timer + time DLL filter
        timer_.setup(samplerate_, blocksize_, timer_check_.load());

        return kAooOk;
    } else {
        return kAooErrorUnknown;
    }
}

AOO_API AooError AOO_CALL AooSource_handleMessage(
        AooSource *src, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen) {
    return src->handleMessage(data, size, address, addrlen);
}

// /aoo/src/<id>/format <sink>
AooError AOO_CALL aoo::source_imp::handleMessage(
        const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen){
    AooType type;
    AooId src;
    AooInt32 onset;
    auto err = aoo_parsePattern(data, size, &type, &src, &onset);
    if (err != kAooOk){
        LOG_WARNING("aoo_source: not an AoO message!");
        return kAooErrorBadArgument;
    }
    if (type != kAooTypeSource){
        LOG_WARNING("aoo_source: not a source message!");
        return kAooErrorBadArgument;
    }
    if (src != id()){
        LOG_WARNING("aoo_source: wrong source ID!");
        return kAooErrorBadArgument;
    }

    ip_address addr((const sockaddr *)address, addrlen);

    if (data[0] == 0){
        // binary message
        auto cmd = aoo::from_bytes<int16_t>(data + kAooBinMsgDomainSize + 2);
        switch (cmd){
        case kAooBinMsgCmdData:
            handle_data_request(data + onset, size - onset, addr);
            return kAooOk;
        default:
            return kAooErrorBadArgument;
        }
    } else {
        try {
            osc::ReceivedPacket packet((const char *)data, size);
            osc::ReceivedMessage msg(packet);

            auto pattern = msg.AddressPattern() + onset;
            if (!strcmp(pattern, kAooMsgFormat)){
                handle_format_request(msg, addr);
            } else if (!strcmp(pattern, kAooMsgData)){
                handle_data_request(msg, addr);
            } else if (!strcmp(pattern, kAooMsgInvite)){
                handle_invite(msg, addr);
            } else if (!strcmp(pattern, kAooMsgUninvite)){
                handle_uninvite(msg, addr);
            } else if (!strcmp(pattern, kAooMsgPing)){
                handle_ping(msg, addr);
            } else {
                LOG_WARNING("unknown message " << pattern);
                return kAooErrorUnknown;
            }
            return kAooOk;
        } catch (const osc::Exception& e){
            LOG_ERROR("aoo_source: exception in handle_message: " << e.what());
            return kAooErrorUnknown;
        }
    }
}

AOO_API AooError AOO_CALL AooSource_send(
        AooSource *src, AooSendFunc fn, void *user) {
    return src->send(fn, user);
}

// This method reads audio samples from the ringbuffer,
// encodes them and sends them to all sinks.
AooError AOO_CALL aoo::source_imp::send(AooSendFunc fn, void *user) {
    if (state_.load() != stream_state::play){
        return kAooOk; // nothing to do
    }

    sendfn reply(fn, user);

    send_format(reply);

    send_data(reply);

    resend_data(reply);

    send_ping(reply);

    if (!sinks_.try_free()){
        // LOG_DEBUG("AooSource: try_free() would block");
    }

    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_process(
        AooSource *src, const AooSample **data, AooInt32 n, AooNtpTime t) {
    return src->process(data, n, t);
}

#define NO_SINKS_IDLE 1

AooError AOO_CALL aoo::source_imp::process(
        const AooSample **data, AooInt32 nsamples, AooNtpTime t) {
    auto state = state_.load();
    if (state == stream_state::stop){
        return kAooErrorIdle; // pausing
    } else if (state == stream_state::start){
        // start -> play
        // the mutex should be uncontended most of the time.
        // although it is repeatedly locked in send(), the latter
        // returns early if we're not already playing.
        unique_lock lock(update_mutex_, std::try_to_lock_t{}); // writer lock!
        if (!lock.owns_lock()){
            LOG_VERBOSE("aoo_source: process would block");
            // no need to call xrun()!
            return kAooErrorWouldBlock;
        }

        start_new_stream();

        // check if we have been stopped in the meantime
        auto expected = stream_state::start;
        if (!state_.compare_exchange_strong(expected, stream_state::play)){
            return kAooErrorIdle; // pausing
        }
    }

    // update timer
    // always do this, even if there are no sinks.
    // do it *before* trying to lock the mutex
    bool dynamic_resampling = dynamic_resampling_.load(std::memory_order_relaxed);
    double error;
    auto timerstate = timer_.update(t, error);
    if (timerstate == timer::state::reset){
        LOG_DEBUG("setup time DLL filter for source");
        auto bw = dll_bandwidth_.load(std::memory_order_relaxed);
        dll_.setup(samplerate_, blocksize_, bw, 0);
        realsr_.store(samplerate_, std::memory_order_relaxed);
        // it is safe to set 'lastpingtime' after updating
        // the timer, because in the worst case the ping
        // is simply sent the next time.
        lastpingtime_.store(-1e007); // force first ping
    } else if (timerstate == timer::state::error){
        // calculate xrun blocks
        double nblocks = error * (double)samplerate_ / (double)blocksize_;
    #if NO_SINKS_IDLE
        // only when we have sinks, to avoid accumulating empty blocks
        if (!sinks_.empty())
    #endif
        {
            add_xrun(nblocks);
        }
        LOG_DEBUG("xrun: " << nblocks << " blocks");

        event e(kAooEventXRun);
        e.xrun.count = nblocks + 0.5; // ?
        send_event(e, kAooThreadLevelAudio);

        timer_.reset();
    } else if (dynamic_resampling){
        // update time DLL, but only if n matches blocksize!
        auto elapsed = timer_.get_elapsed();
        if (nsamples == blocksize_){
            dll_.update(elapsed);
        #if AOO_DEBUG_DLL
            DO_LOG_DEBUG("time elapsed: " << elapsed << ", period: "
                      << dll_.period() << ", samplerate: " << dll_.samplerate());
        #endif
        } else {
            // reset time DLL with nominal samplerate
            auto bw = dll_bandwidth_.load(std::memory_order_relaxed);
            dll_.setup(samplerate_, blocksize_, bw, elapsed);
        }
        realsr_.store(dll_.samplerate(), std::memory_order_relaxed);
    }

#if NO_SINKS_IDLE
    // users might run the source passively (= waiting for invitations),
    // so there's a good chance that the start will be active, but
    // without sinks. we can save some CPU by returning early.
    if (sinks_.empty()){
        // nothing to do. users still have to check for pending events,
        // but there is no reason to call send()
        return kAooErrorIdle;
    }
#endif

    // the mutex should be available most of the time.
    // it is only locked exclusively when setting certain options,
    // e.g. changing the buffer size.
    shared_lock lock(update_mutex_, std::try_to_lock); // reader lock!
    if (!lock.owns_lock()){
        LOG_VERBOSE("aoo_source: process would block");
        add_xrun(1);
        return kAooErrorWouldBlock; // ?
    }

    if (!encoder_){
        return kAooErrorIdle;
    }

    // non-interleaved -> interleaved
    // only as many channels as current format needs
    auto nfchannels = encoder_->nchannels();
    auto insize = nsamples * nfchannels;
    auto buf = (AooSample *)alloca(insize * sizeof(AooSample));
    for (int i = 0; i < nfchannels; ++i){
        if (i < nchannels_){
            for (int j = 0; j < nsamples; ++j){
                buf[j * nfchannels + i] = data[i][j];
            }
        } else {
            // zero remaining channel
            for (int j = 0; j < nsamples; ++j){
                buf[j * nfchannels + i] = 0;
            }
        }
    }

    double sr;
    if (dynamic_resampling){
        sr = realsr_.load(std::memory_order_relaxed) / (double)samplerate_
                * (double)encoder_->samplerate();
    } else {
        sr = encoder_->samplerate();
    }

    auto outsize = nfchannels * encoder_->blocksize();
#if AOO_DEBUG_AUDIO_BUFFER
    auto resampler_size = resampler_.size() / (double)(nchannels_ * blocksize_);
    LOG_DEBUG("audioqueue: " << audioqueue_.read_available() / resampler_.ratio()
              << ", resampler: " << resampler_size / resampler_.ratio()
              << ", capacity: " << audioqueue_.capacity() / resampler_.ratio());
#endif
    if (need_resampling()){
        // *first* try to move samples from resampler to audiobuffer
        while (audioqueue_.write_available()){
            // copy audio samples
            auto ptr = (block_data *)audioqueue_.write_data();
            if (!resampler_.read(ptr->data, outsize)){
                break;
            }
            // push samplerate
            ptr->sr = sr;

            audioqueue_.write_commit();
        }
        // now try to write to resampler
        if (!resampler_.write(buf, insize)){
            LOG_WARNING("aoo_source: send buffer overflow");
            add_xrun(1);
            // don't return kAooErrorIdle, otherwise the send thread
            // wouldn't drain the buffer.
            return kAooErrorUnknown;
        }
    } else {
        // bypass resampler
        if (audioqueue_.write_available()){
            auto ptr = (block_data *)audioqueue_.write_data();
            // copy audio samples
            std::copy(buf, buf + outsize, ptr->data);
            // push samplerate
            ptr->sr = sr;

            audioqueue_.write_commit();
        } else {
            LOG_WARNING("aoo_source: send buffer overflow");
            add_xrun(1);
            // don't return kAooErrorIdle, otherwise the send thread
            // wouldn't drain the buffer.
            return kAooErrorUnknown;
        }
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_setEventHandler(
        AooSource *src, AooEventHandler fn,
        void *user, AooEventMode mode)
{
    return src->setEventHandler(fn, user, mode);
}

AooError AOO_CALL aoo::source_imp::setEventHandler(
        AooEventHandler fn, void *user, AooEventMode mode){
    eventhandler_ = fn;
    eventcontext_ = user;
    eventmode_ = mode;
    return kAooOk;
}

AOO_API AooBool AOO_CALL AooSource_eventsAvailable(AooSource *src){
    return src->eventsAvailable();
}

AooBool AOO_CALL aoo::source_imp::eventsAvailable(){
    return !eventqueue_.empty();
}

AOO_API AooError AOO_CALL AooSource_pollEvents(AooSource *src){
    return src->pollEvents();
}

AooError AOO_CALL aoo::source_imp::pollEvents(){
    // always thread-safe
    event e;
    while (eventqueue_.try_pop(e)) {
        eventhandler_(eventcontext_, &e.event_, kAooThreadLevelUnknown);
        // some memory use extra memory
        if (e.type_ == kAooEventFormatRequest){
            memory_.free(memory_block::from_bytes((void *)e.format.format));
        }
    }
    return kAooOk;
}

namespace aoo {

/*///////////////////////// source ////////////////////////////////*/

sink_desc * source_imp::find_sink(const ip_address& addr, AooId id){
    for (auto& sink : sinks_){
        if (sink.address == addr && sink.id == id){
            return &sink;
        }
    }
    return nullptr;
}

aoo::sink_desc * source_imp::get_sink_arg(intptr_t index){
    auto ep = (const AooEndpoint *)index;
    if (!ep){
        LOG_ERROR("AooSink: missing sink argument");
        return nullptr;
    }
    ip_address addr((const sockaddr *)ep->address, ep->addrlen);
    auto sink = find_sink(addr, ep->id);
    if (!sink){
        LOG_ERROR("AooSink: couldn't find sink");
    }
    return sink;
}

AooError source_imp::add_sink(const AooEndpoint& ep, uint32_t flags)
{
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);

    sink_lock lock(sinks_);
    // check if sink exists!
    if (find_sink(addr, ep.id)){
        LOG_WARNING("aoo_source: sink already added!");
        return kAooErrorUnknown;
    }
    // add sink descriptor
#if USE_AOO_NET
    // check if the peer needs to be relayed
    if (client_){
        AooBool relay;
        if (client_->control(kAooCtlNeedRelay,
                             reinterpret_cast<intptr_t>(&ep),
                             &relay, sizeof(relay)) == kAooOk)
        {
            if (relay == kAooTrue){
                LOG_DEBUG("sink " << addr << " needs to be relayed");
                flags |= kAooEndpointRelay;
            }
        }
    }
#endif
    sinks_.emplace_front(addr, ep.id, flags);
    needformat_.store(true, std::memory_order_release); // !

    return kAooOk;
}

AooError source_imp::remove_sink(const AooEndpoint& ep){
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);

    sink_lock lock(sinks_);
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it){
        if (it->address == addr && it->id == ep.id){
            sinks_.erase(it);
            return kAooOk;
        }
    }
    LOG_WARNING("aoo_source: sink not found!");
    return kAooErrorUnknown;
}

AooError source_imp::set_format(AooFormat &f){
    std::unique_ptr<encoder> new_encoder;

    // create a new encoder if necessary
    // This is the only thread where the decoder can possibly
    // change, so we don't need a lock to safely *read* it!
    if (!encoder_ || strcmp(encoder_->name(), f.codec)){
        auto codec = aoo::find_codec(f.codec);
        if (codec){
            new_encoder = codec->create_encoder(nullptr);
            if (!new_encoder){
                LOG_ERROR("couldn't create encoder!");
                return kAooErrorUnknown;
            }
        } else {
            LOG_ERROR("codec '" << f.codec << "' not supported!");
            return kAooErrorUnknown;
        }
    }

    scoped_lock lock(update_mutex_); // writer lock!
    if (new_encoder){
        encoder_ = std::move(new_encoder);
    }

    // always set the format
    auto err = encoder_->set_format(f);
    if (err == kAooOk){
        update_audioqueue();

        if (need_resampling()){
            update_resampler();
        }

        update_historybuffer();

        // we need to start a new stream while holding the lock.
        // it might be tempting to just (atomically) set 'state_'
        // to 'stream_start::start', but then the send() method
        // could answer a format request by an existing stream with
        // the wrong format, before process() starts the new stream.
        //
        // NOTE: there's a slight race condition because 'xrun_'
        // might be incremented right afterwards, but I'm not
        // sure if this could cause any real problems..
        start_new_stream();
    }
    return err;
}

int32_t source_imp::make_stream_id(){
    thread_local std::random_device dev;
    thread_local std::mt19937 mt(dev());
    std::uniform_int_distribution<int32_t> dist;
    return dist(mt);
}

bool source_imp::need_resampling() const {
#if 1
    // always go through resampler, so we can use a variable block size
    return true;
#else
    return blocksize_ != encoder_->blocksize() || samplerate_ != encoder_->samplerate();
#endif
}

void source_imp::send_event(const event& e, AooThreadLevel level){
    switch (eventmode_){
    case kAooEventModePoll:
        eventqueue_.push(e);
        break;
    case kAooEventModeCallback:
        eventhandler_(eventcontext_, &e.event_, level);
        break;
    default:
        break;
    }
}

// must be real-time safe because it might be called in process()!
// always called with update lock!
void source_imp::start_new_stream(){
    // implicitly reset time DLL to be on the safe side
    timer_.reset();

    // Start new sequence and resend format.
    // We naturally want to do this when setting the format,
    // but it's good to also do it in setup() to eliminate
    // any timing gaps.
    stream_id_ = make_stream_id();
    sequence_ = 0;
    xrun_.store(0.0); // !

    // remove audio from previous stream
    resampler_.reset();

    audioqueue_.reset();

    history_.clear(); // !

    // reset encoder to avoid garbage from previous stream
    encoder_->reset();

    sink_lock lock(sinks_);
    for (auto& s : sinks_){
        s.reset();
        s.request_format();
    }
    needformat_.store(true, std::memory_order_release); // !
}

void source_imp::add_xrun(float n){
    // add with CAS loop
    auto current = xrun_.load(std::memory_order_relaxed);
    while (!xrun_.compare_exchange_weak(current, current + n))
        ;
}

void source_imp::update_audioqueue(){
    if (encoder_ && samplerate_ > 0){
        // recalculate buffersize from seconds to samples
        int32_t bufsize = buffersize_.load() * encoder_->samplerate();
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        // minimum buffer size depends on resampling and reblocking!
        auto downsample = (double)encoder_->samplerate() / (double)samplerate_;
        auto reblock = (double)encoder_->blocksize() / (double)blocksize_;
        int32_t minblocks = std::ceil(downsample * reblock);
        nbuffers = std::max<int32_t>(nbuffers, minblocks);
        LOG_DEBUG("aoo_source: buffersize (ms): " << (buffersize_.load() * 1000.0)
                  << ", samples: " << bufsize << ", nbuffers: " << nbuffers
                  << ", minimum: " << minblocks);

        // resize audio buffer
        auto nsamples = encoder_->blocksize() * encoder_->nchannels();
        auto nbytes = sizeof(block_data::sr) + nsamples * sizeof(AooSample);
        // align to 8 bytes
        nbytes = (nbytes + 7) & ~7;
        audioqueue_.resize(nbytes, nbuffers);
    }
}

void source_imp::update_resampler(){
    if (encoder_ && samplerate_ > 0){
        resampler_.setup(blocksize_, encoder_->blocksize(),
                         samplerate_, encoder_->samplerate(),
                         encoder_->nchannels());
    }
}

void source_imp::update_historybuffer(){
    if (encoder_){
        // bufsize can also be 0 (= don't resend)!
        int32_t bufsize = resend_buffersize_.load() * encoder_->samplerate();
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        history_.resize(nbuffers);
        LOG_DEBUG("aoo_source: history buffersize (ms): "
                  << (resend_buffersize_.load() * 1000.0)
                  << ", samples: " << bufsize << ", nbuffers: " << nbuffers);

    }
}

void source_imp::send_format(const sendfn& fn){
    if (!needformat_.exchange(false, std::memory_order_acquire)){
        return;
    }

    shared_lock updatelock(update_mutex_); // reader lock!

    if (!encoder_){
        return;
    }

    int32_t stream_id = stream_id_;

    AooFormatStorage f;
    if (encoder_->get_format(f.header, sizeof(AooFormatStorage)) != kAooOk){
        return;
    }

    // serialize format
    AooByte options[kAooCodecMaxSettingSize];
    AooInt32 size = sizeof(options);
    if (encoder_->serialize(f.header, options, size) != kAooOk){
        return;
    }

    updatelock.unlock();

    // we only free sources in this thread, so we don't have to lock
#if 0
    // this is not a real lock, so we don't have worry about dead locks
    sink_lock lock(sinks_);
#endif
    for (auto& s : sinks_){
        if (s.need_format()){
            // /aoo/sink/<id>/format <src> <version> <stream_id>
            // <numchannels> <samplerate> <blocksize> <codec> <options> <flags>

            LOG_DEBUG("send format to " << s.id << " (stream_id = " << stream_id << ")");

            char buf[AOO_MAX_PACKET_SIZE];
            osc::OutboundPacketStream msg(buf, sizeof(buf));

            const int32_t max_addr_size = kAooMsgDomainLen
                    + kAooMsgSinkLen + 16 + kAooMsgFormatLen;
            char address[max_addr_size];
            snprintf(address, sizeof(address), "%s%s/%d%s",
                     kAooMsgDomain, kAooMsgSink, s.id, kAooMsgFormat);

            msg << osc::BeginMessage(address) << id() << (int32_t)make_version()
                << stream_id << f.header.numChannels << f.header.sampleRate
                << f.header.blockSize << f.header.codec << osc::Blob(options, size)
                << (int32_t)s.flags << osc::EndMessage;

            fn((const AooByte *)msg.Data(), msg.Size(), s.address, s.flags);
        }
    }
}

#define XRUN_THRESHOLD 0.1

void source_imp::send_data(const sendfn& fn){
    int32_t last_sequence = 0;

    // NOTE: we have to lock *before* calling 'read_available'
    // on the audio queue!
    shared_lock updatelock(update_mutex_); // reader lock

    // *first* check for dropped blocks
    if (xrun_.load(std::memory_order_relaxed) > XRUN_THRESHOLD){
        // calculate number of xrun blocks (after resampling)
        float drop = xrun_.exchange(0.0) * (float)resampler_.ratio();
        // round up
        int nblocks = std::ceil(drop);
        // subtract diff with a CAS loop
        float diff = (float)nblocks - drop;
        auto current = xrun_.load(std::memory_order_relaxed);
        while (!xrun_.compare_exchange_weak(current, current - diff))
            ;
        // drop blocks
        LOG_DEBUG("aoo_source: send " << nblocks << " empty blocks for "
                  "xrun (" << (int)drop << " blocks)");
        while (nblocks--){
            // check the encoder and make snapshost of stream_id
            // in every iteration because we release the lock
            if (!encoder_){
                return;
            }
            int32_t stream_id = stream_id_;
            // send empty block
            // NOTE: we're the only thread reading 'sequence_', so we can increment
            // it even while holding a reader lock!
            data_packet d;
            d.sequence = last_sequence = sequence_++;
            d.samplerate = encoder_->samplerate(); // use nominal samplerate
            d.channel = 0;
            d.totalsize = 0;
            d.nframes = 0;
            d.frame = 0;
            d.data = nullptr;
            d.size = 0;
            // now we can unlock
            updatelock.unlock();

            // we only free sources in this thread, so we don't have to lock
        #if 0
            // this is not a real lock, so we don't have worry about dead locks
            sink_lock lock(sinks_);
        #endif
            // send block to all sinks
            send_packet(fn, stream_id, d, binary_.load(std::memory_order_relaxed));

            updatelock.lock();
        }
    }

    // now send audio
    while (audioqueue_.read_available()){
        if (!encoder_){
            return;
        }

        if (!sinks_.empty()){
            int32_t stream_id = stream_id_; // make snapshot

            auto ptr = (block_data *)audioqueue_.read_data();

            data_packet d;
            d.samplerate = ptr->sr;

            // copy and convert audio samples to blob data
            auto nchannels = encoder_->nchannels();
            auto blocksize = encoder_->blocksize();
            auto nsamples = nchannels * blocksize;
        #if 0
            Log log;
            for (int i = 0; i < nsamples; ++i){
                log << ptr->data[i] << " ";
            }
        #endif

            sendbuffer_.resize(sizeof(double) * nsamples); // overallocate

            AooInt32 size = sendbuffer_.size();
            auto err = encoder_->encode(ptr->data, nsamples,
                sendbuffer_.data(), size);
            d.totalsize = size;

            audioqueue_.read_commit(); // always commit!

            if (err != kAooOk){
                LOG_WARNING("aoo_source: couldn't encode audio data!");
                return;
            }

            // NOTE: we're the only thread reading 'sequence_', so we can increment
            // it even while holding a reader lock!
            d.sequence = last_sequence = sequence_++;

            // calculate number of frames
            bool binary = binary_.load(std::memory_order_relaxed);
            auto packetsize = packetsize_.load(std::memory_order_relaxed);
            auto maxpacketsize = packetsize -
                    (binary ? kAooBinMsgDataHeaderSize : kAooMsgDataHeaderSize);
            auto dv = div(d.totalsize, maxpacketsize);
            d.nframes = dv.quot + (dv.rem != 0);

            // save block (if we have a history buffer)
            if (history_.capacity() > 0){
                history_.push()->set(d.sequence, d.samplerate, sendbuffer_.data(),
                                     d.totalsize, d.nframes, maxpacketsize);
            }

            // unlock before sending!
            updatelock.unlock();

            // from here on we don't hold any lock!

            // send a single frame to all sinks
            // /AoO/<sink>/data <src> <stream_id> <seq> <sr> <channel_onset>
            // <totalsize> <numpackets> <packetnum> <data>
            auto dosend = [&](int32_t frame, const AooByte* data, auto n){
                d.frame = frame;
                d.data = data;
                d.size = n;
                // send block to all sinks
                send_packet(fn, stream_id, d, binary);
            };

            auto ntimes = redundancy_.load();
            for (auto i = 0; i < ntimes; ++i){
                auto ptr = sendbuffer_.data();
                // send large frames (might be 0)
                for (int32_t j = 0; j < dv.quot; ++j, ptr += maxpacketsize){
                    dosend(j, ptr, maxpacketsize);
                }
                // send remaining bytes as a single frame (might be the only one!)
                if (dv.rem){
                    dosend(dv.quot, ptr, dv.rem);
                }
            }

            updatelock.lock();
        } else {
            // drain buffer anyway
            audioqueue_.read_commit();
        }
    }

    // handle overflow (with 64 samples @ 44.1 kHz this happens every 36 days)
    // for now just force a reset by changing the stream_id, LATER think how to handle this better
    if (last_sequence == INT32_MAX){
        updatelock.unlock();
        // not perfectly thread-safe, but shouldn't cause problems AFAICT....
        scoped_lock lock(update_mutex_);
        sequence_ = 0;
        stream_id_ = make_stream_id();
    }
}

void source_imp::resend_data(const sendfn &fn){
    shared_lock updatelock(update_mutex_); // reader lock for history buffer!
    if (!history_.capacity()){
        return;
    }
    int32_t stream_id = stream_id_; // cache stream_id!

    // we only free sources in this thread, so we don't have to lock
#if 0
    // this is not a real lock, so we don't have worry about dead locks
    sink_lock lock(sinks_);
#endif
    // send block to sinks
    for (auto& sink : sinks_){
        data_request request;
        while (sink.data_requests.try_pop(request)){
            auto block = history_.find(request.sequence);
            if (block){
                bool binary = binary_.load(std::memory_order_relaxed);

                aoo::data_packet d;
                d.sequence = block->sequence;
                d.samplerate = block->samplerate;
                d.channel = sink.channel.load(std::memory_order_relaxed);
                d.totalsize = block->size();
                d.nframes = block->num_frames();
                // We use a buffer on the heap because blocks and even frames
                // can be quite large and we don't want them to sit on the stack.
                if (request.frame < 0){
                    // Copy whole block and save frame pointers.
                    sendbuffer_.resize(d.totalsize);
                    AooByte *buf = sendbuffer_.data();
                    AooByte *frameptr[256];
                    int32_t framesize[256];
                    int32_t onset = 0;

                    for (int i = 0; i < d.nframes; ++i){
                        auto nbytes = block->get_frame(i, buf + onset, d.totalsize - onset);
                        if (nbytes > 0){
                            frameptr[i] = buf + onset;
                            framesize[i] = nbytes;
                            onset += nbytes;
                        } else {
                            LOG_ERROR("empty frame!");
                        }
                    }
                    // unlock before sending
                    updatelock.unlock();

                    // send frames to sink
                    for (int i = 0; i < d.nframes; ++i){
                        d.frame = i;
                        d.data = frameptr[i];
                        d.size = framesize[i];
                        if (binary){
                            send_packet_bin(fn, sink, stream_id, d);
                        } else {
                            send_packet_osc(fn, sink, stream_id, d);
                        }
                    }

                    // lock again
                    updatelock.lock();
                } else {
                    // Copy a single frame
                    if (request.frame >= 0 && request.frame < d.nframes){
                        int32_t size = block->frame_size(request.frame);
                        sendbuffer_.resize(size);
                        block->get_frame(request.frame, sendbuffer_.data(), size);
                        // unlock before sending
                        updatelock.unlock();

                        // send frame to sink
                        d.frame = request.frame;
                        d.data = sendbuffer_.data();
                        d.size = size;
                        if (binary){
                            send_packet_bin(fn, sink, stream_id, d);
                        } else {
                            send_packet_osc(fn, sink, stream_id, d);
                        }

                        // lock again
                        updatelock.lock();
                    } else {
                        LOG_ERROR("frame number " << request.frame << " out of range!");
                    }
                }
            }
        }
    }
}

void source_imp::send_packet(const sendfn &fn, int32_t stream_id,
                             data_packet& d, bool binary) {
    if (binary){
        AooByte buf[AOO_MAX_PACKET_SIZE];
        int32_t size;

        write_bin_data(nullptr, stream_id, d, buf, size);

        // we only free sources in this thread, so we don't have to lock
    #if 0
        // this is not a real lock, so we don't have worry about dead locks
        sink_lock lock(sinks_);
    #endif
        for (auto& sink : sinks_){
            // overwrite id and channel!
            aoo::to_bytes(sink.id, buf + 8);

            auto channel = sink.channel.load(std::memory_order_relaxed);
            aoo::to_bytes<int16_t>(channel, buf + kAooBinMsgHeaderSize + 12);

        #if AOO_DEBUG_DATA
            LOG_DEBUG("send block: seq = " << d.sequence << ", sr = "
                      << d.samplerate << ", chn = " << channel << ", totalsize = "
                      << d.totalsize << ", nframes = " << d.nframes
                      << ", frame = " << d.frame << ", size " << d.size);
        #endif
            fn(buf, size, sink.address, sink.flags);
        }
    } else {
        // we only free sources in this thread, so we don't have to lock
    #if 0
        // this is not a real lock, so we don't have worry about dead locks
        sink_lock lock(sinks_);
    #endif
        for (auto& sink : sinks_){
            // set channel!
            d.channel = sink.channel.load(std::memory_order_relaxed);
            send_packet_osc(fn, sink, stream_id, d);
        }
    }
}

// /aoo/sink/<id>/data <src> <stream_id> <seq> <sr> <channel_onset> <totalsize> <nframes> <frame> <data>

void source_imp::send_packet_osc(const sendfn& fn, const endpoint& ep,
                                 int32_t stream_id, const aoo::data_packet& d) const {
    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSinkLen + 16 + kAooMsgDataLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             kAooMsgDomain, kAooMsgSink, ep.id, kAooMsgData);

    msg << osc::BeginMessage(address) << id() << stream_id << d.sequence << d.samplerate
        << d.channel << d.totalsize << d.nframes << d.frame << osc::Blob(d.data, d.size)
        << osc::EndMessage;

#if AOO_DEBUG_DATA
    LOG_DEBUG("send block: seq = " << d.sequence << ", sr = "
              << d.samplerate << ", chn = " << d.channel << ", totalsize = "
              << d.totalsize << ", nframes = " << d.nframes
              << ", frame = " << d.frame << ", size " << d.size);
#endif
    fn((const AooByte *)msg.Data(), msg.Size(), ep.address, ep.flags);
}

// binary data message:
// id (int32), stream_id (int32), seq (int32), channel (int16), flags (int16),
// [total (int32), nframes (int16), frame (int16)],  [sr (float64)],
// size (int32), data...

void source_imp::write_bin_data(const endpoint* ep, int32_t stream_id,
                                const data_packet& d, AooByte *buf, int32_t& size) const
{
    int16_t flags = 0;
    if (d.samplerate != 0){
        flags |= kAooBinMsgDataSampleRate;
    }
    if (d.nframes > 1){
        flags |= kAooBinMsgDataFrames;
    }

    auto it = buf;
    // write header
    memcpy(it, kAooBinMsgDomain, kAooBinMsgDomainSize);
    it += kAooBinMsgDomainSize;
    aoo::write_bytes<int16_t>(kAooTypeSink, it);
    aoo::write_bytes<int16_t>(kAooBinMsgCmdData, it);
    if (ep){
        aoo::write_bytes<int32_t>(ep->id, it);
    } else {
        // skip
        it += sizeof(int32_t);
    }
    // write arguments
    aoo::write_bytes<int32_t>(id(), it);
    aoo::write_bytes<int32_t>(stream_id, it);
    aoo::write_bytes<int32_t>(d.sequence, it);
    aoo::write_bytes<int16_t>(d.channel, it);
    aoo::write_bytes<int16_t>(flags, it);
    if (flags & kAooBinMsgDataFrames){
        aoo::write_bytes<int32_t>(d.totalsize, it);
        aoo::write_bytes<int16_t>(d.nframes, it);
        aoo::write_bytes<int16_t>(d.frame, it);
    }
    if (flags & kAooBinMsgDataSampleRate){
         aoo::write_bytes<double>(d.samplerate, it);
    }
    aoo::write_bytes<int32_t>(d.size, it);
    // write audio data
    memcpy(it, d.data, d.size);
    it += d.size;

    size = it - buf;
}

void source_imp::send_packet_bin(const sendfn& fn, const endpoint& ep,
                                 int32_t stream_id, const aoo::data_packet& d) const {
    AooByte buf[AOO_MAX_PACKET_SIZE];
    int32_t size;

    write_bin_data(&ep, stream_id, d, buf, size);

#if AOO_DEBUG_DATA
    LOG_DEBUG("send block: seq = " << d.sequence << ", sr = "
              << d.samplerate << ", chn = " << d.channel << ", totalsize = "
              << d.totalsize << ", nframes = " << d.nframes
              << ", frame = " << d.frame << ", size " << d.size);
#endif

    fn((const AooByte *)buf, size, ep.address, ep.flags);
}

void source_imp::send_ping(const sendfn& fn){
    // if stream is stopped, the timer won't increment anyway
    auto elapsed = timer_.get_elapsed();
    auto pingtime = lastpingtime_.load();
    auto interval = ping_interval_.load(); // 0: no ping
    if (interval > 0 && (elapsed - pingtime) >= interval){
        auto tt = timer_.get_absolute();
        // we only free sources in this thread, so we don't have to lock
    #if 0
        // this is not a real lock, so we don't have worry about dead locks
        sink_lock lock(sinks_);
    #endif
        // send ping to sinks
        for (auto& sink : sinks_){
            // /aoo/sink/<id>/ping <src> <time>
            LOG_DEBUG("send ping to " << sink.id);

            char buf[AOO_MAX_PACKET_SIZE];
            osc::OutboundPacketStream msg(buf, sizeof(buf));

            const int32_t max_addr_size = kAooMsgDomainLen
                    + kAooMsgSinkLen + 16 + kAooMsgPingLen;
            char address[max_addr_size];
            snprintf(address, sizeof(address), "%s%s/%d%s",
                     kAooMsgDomain, kAooMsgSink, sink.id, kAooMsgPing);

            msg << osc::BeginMessage(address) << id() << osc::TimeTag(tt)
                << osc::EndMessage;

            fn((const AooByte *)msg.Data(), msg.Size(), sink.address, sink.flags);
        }

        lastpingtime_.store(elapsed);
    }
}

// /format <id> <version>
void source_imp::handle_format_request(const osc::ReceivedMessage& msg,
                                       const ip_address& addr)
{
    LOG_DEBUG("handle format request");

    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    auto version = (it++)->AsInt32();

    // LATER handle this in the sink_desc (e.g. not sending data)
    if (!check_version(version)){
        LOG_ERROR("aoo_source: sink version not supported");
        return;
    }

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);

    if (sink){
        if (it != msg.ArgumentsEnd()){
            // requested another format
            int32_t stream_id = (it++)->AsInt32();
            // ignore outdated requests
            // this can happen because format requests are sent repeatedly
            // by the sink until a) the source replies or b) the timeout is reached.
            // if the network latency is high, the sink might sent a format request
            // right before receiving a /format message (as a result of the previous request).
            // until the source re
            shared_lock lock(update_mutex_);
            if (stream_id != stream_id_){
                LOG_DEBUG("ignoring outdated format request");
                return;
            }
            lock.unlock();

            // get format from arguments
            AooFormat f;
            f.numChannels = (it++)->AsInt32();
            f.sampleRate = (it++)->AsInt32();
            f.blockSize = (it++)->AsInt32();
            f.codec = (it++)->AsString();
            f.size = sizeof(AooFormat);
            const void *settings;
            osc::osc_bundle_element_size_t size;
            (it++)->AsBlob(settings, size);

            auto c = aoo::find_codec(f.codec);
            if (c){
                AooFormatStorage fmt;
                if (c->deserialize(f, (const AooByte *)settings, size, fmt.header,
                                   sizeof(AooFormatStorage)) == kAooOk){
                    // send format event
                    event e(kAooEventFormatRequest, addr, id);

                    if (eventmode_ == kAooEventModeCallback){
                        // synchronous: use stack
                        e.format.format = &fmt.header;
                    } if (eventmode_ == kAooEventModePoll){
                        // asynchronous: use heap
                        auto mem = memory_.alloc(fmt.header.size);
                        memcpy(mem->data(), &fmt, fmt.header.size);
                        e.format.format = (const AooFormat *)mem->data();
                    }
                    send_event(e, kAooThreadLevelAudio);
                }
            } else {
                LOG_WARNING("handle_format_request: codec '"
                            << f.codec << "' not supported");
            }
        } else {
            // resend current format
            sink->request_format();
            needformat_.store(true, std::memory_order_release);
        }
    } else {
        LOG_VERBOSE("ignoring '" << kAooMsgFormat << "' message: sink not found");
    }
}

void source_imp::handle_data_request(const osc::ReceivedMessage& msg,
                                     const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    auto id = (it++)->AsInt32();
    auto stream_id = (it++)->AsInt32(); // we can ignore the stream_id

    LOG_DEBUG("handle data request");

    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        // get pairs of sequence + frame
        int npairs = (msg.ArgumentCount() - 2) / 2;
        while (npairs--){
            int32_t sequence = (it++)->AsInt32();
            int32_t frame = (it++)->AsInt32();
            sink->data_requests.push(sequence, frame);
        }
    } else {
        LOG_VERBOSE("ignoring '" << kAooMsgData << "' message: sink not found");
    }
}

// (header), id (int32), stream_id (int32), count (int32),
// seq1 (int32), frame1(int32), seq2(int32), frame2(seq), etc.

void source_imp::handle_data_request(const AooByte *msg, int32_t n,
                                     const ip_address& addr)
{
    // check size (id, stream_id, count)
    if (n < 12){
        LOG_ERROR("handle_data_request: header too small!");
        return;
    }

    auto it = msg;

    auto id = aoo::read_bytes<int32_t>(it);
    auto stream_id = aoo::read_bytes<int32_t>(it); // we can ignore the stream_id

    LOG_DEBUG("handle data request");

    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        // get pairs of sequence + frame
        int count = aoo::read_bytes<int32_t>(it);
        if (n < (12 + count * sizeof(int32_t) * 2)){
            LOG_ERROR("handle_data_request: bad 'count' argument!");
            return;
        }
        while (count--){
            int32_t sequence = aoo::read_bytes<int32_t>(it);
            int32_t frame = aoo::read_bytes<int32_t>(it);
            sink->data_requests.push(sequence, frame);
        }
    } else {
        LOG_VERBOSE("ignoring '" << kAooMsgData << "' message: sink not found");
    }
}

void source_imp::handle_invite(const osc::ReceivedMessage& msg,
                               const ip_address& addr)
{
    auto id = msg.ArgumentsBegin()->AsInt32();

    LOG_DEBUG("handle invitation by " << addr << " " << id);

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (!sink){
        // push "invite" event
        event e(kAooEventInvite, addr, id);
        send_event(e, kAooThreadLevelAudio);
    } else {
        LOG_VERBOSE("ignoring '" << kAooMsgInvite << "' message: sink already added");
    }
}

void source_imp::handle_uninvite(const osc::ReceivedMessage& msg,
                                 const ip_address& addr)
{
    auto id = msg.ArgumentsBegin()->AsInt32();

    LOG_DEBUG("handle uninvitation by " << addr.name()
              << " " << addr.port() << " " << id);

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    if (find_sink(addr, id)){
        // push "uninvite" event
        event e(kAooEventUninvite, addr, id);
        send_event(e, kAooThreadLevelAudio);
    } else {
        LOG_VERBOSE("ignoring '" << kAooMsgUninvite << "' message: sink not found");
    }
}

void source_imp::handle_ping(const osc::ReceivedMessage& msg,
                             const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    AooId id = (it++)->AsInt32();
    time_tag tt1 = (it++)->AsTimeTag();
    time_tag tt2 = (it++)->AsTimeTag();
    int32_t lost_blocks = (it++)->AsInt32();

    LOG_DEBUG("handle ping");

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    if (find_sink(addr, id)){
        // push "ping" event
        event e(kAooEventPing, addr, id);
        e.ping.tt1 = tt1;
        e.ping.tt2 = tt2;
        e.ping.lostBlocks = lost_blocks;
    #if 0
        e.ping.tt3 = timer_.get_absolute(); // use last stream time
    #else
        e.ping.tt3 = aoo::time_tag::now(); // use real system time
    #endif
        send_event(e, kAooThreadLevelAudio);
    } else {
        LOG_VERBOSE("ignoring '" << kAooMsgPing << "' message: sink not found");
    }
}

} // aoo
