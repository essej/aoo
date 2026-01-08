/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "source.hpp"

#include <cstring>
#include <algorithm>
#include <cmath>
#include <array>

// avoid processing if there are no sinks.
// Currently, this is turned off because it messes up the timing
// for stream timestamps and stream messages.
#define IDLE_IF_NO_SINKS 0

namespace aoo {

// OSC data message
const int32_t kDataMaxAddrSize = kAooMsgDomainLen + kAooMsgSinkLen + 16 + kAooMsgDataLen;
// typetag string: max. 12 bytes
// args (without blob data): 48 bytes
const int32_t kDataHeaderSize = kDataMaxAddrSize + 52;

// binary data message:
// args: 40 bytes max. (12 bytes min.)
const int32_t kBinDataHeaderSize = kAooBinMsgLargeHeaderSize + 40;

//-------------------- sink_desc ------------------------//

// called while locked
void sink_desc::start(){
    // clear requests, just to make sure we don't resend
    // frames with a previous format.
    data_requests_.clear();
    if (is_active()){
        stream_id_.store(get_random_id());
        LOG_DEBUG("AooSource: " << ep << ": start new stream (" << stream_id() << ")");
        notify_start();
    }
}

// called while locked
void sink_desc::stop(Source& s, int32_t offset){
    if (is_active()){
        LOG_DEBUG("AooSource: " << ep << ": stop stream (" << stream_id() << ")");
        sink_request r(request_type::stop, ep);
        r.stop.stream = stream_id();
        r.stop.offset = offset;
        s.push_request(r);
    }
}

bool sink_desc::need_invite(AooId token){
    // avoid redundant invitation events
    if (token != invite_token_){
        invite_token_ = token;
        LOG_DEBUG("AooSource: " << ep << ": received invitation ("
                  << token << ")");
        return true;
    } else {
        LOG_DEBUG("AooSource: " << ep << ": invitation already received ("
                  << token << ")");
        return false;
    }
}

void sink_desc::handle_invite(Source &s, AooId token, bool accept){
    if (accept){
        stream_id_.store(token); // activates sink
        LOG_DEBUG("AooSource: " << ep << ": accept invitation (" << token << ")");
        if (s.is_running()){
            notify_start();
            s.notify_start();
        } else {
            LOG_WARNING("AooSource: " << ep << ": accept invitation without running stream");
        }
    } else {
        LOG_DEBUG("AooSource: " << ep << ": decline invitation (" << token << ")");
        // NB: always send /decline message, even if we are not streaming!
        sink_request r(request_type::decline, ep);
        r.decline.token = token;
        s.push_request(r);
    }
}

bool sink_desc::need_uninvite(AooId token){
    // avoid redundant invitation events
    if (token != uninvite_token_){
        uninvite_token_ = token;
        LOG_DEBUG("AooSource: " << ep << ": received uninvitation ("
                  << token << ")");
        return true;
    } else {
        LOG_DEBUG("AooSource: " << ep << ": uninvitation already received ("
                  << token << ")");
        return false;
    }
}

void sink_desc::handle_uninvite(Source &s, AooId token, bool accept){
    if (accept) {
        stream_id_.store(kAooIdInvalid); // deactivates sink
        LOG_DEBUG("AooSource: " << ep << ": accept uninvitation (" << token << ")");
        if (s.is_running()){
            sink_request r(request_type::stop, ep);
            r.stop.stream = token;
            r.stop.offset = 0;
            s.push_request(r);
        }
    } else {
        // nothing to do, just let the remote side timeout
        LOG_DEBUG("AooSource: " << ep << ": decline uninvitation (" << token << ")");
    }
}

void sink_desc::activate(Source& s, bool b) {
    if (b){
        stream_id_.store(get_random_id());
        LOG_DEBUG("AooSource: " << ep << ": activate (" << stream_id() << ")");
        if (s.is_running()){
            notify_start();
            s.notify_start();
        }
    } else {
        auto stream = stream_id_.exchange(kAooIdInvalid);
        LOG_DEBUG("AooSource: " << ep << ": deactivate (" << stream << ")");
        if (s.is_running()){
            sink_request r(request_type::stop, ep);
            r.stop.stream = stream;
            r.stop.offset = 0;
            s.push_request(r);
        }
    }
}

} // namespace aoo

//---------------------- Source -------------------------//

AOO_API AooSource * AOO_CALL AooSource_new(AooId id) {
    try {
        return aoo::construct<aoo::Source>(id);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

aoo::Source::Source(AooId id)
    : id_(id) {}

AOO_API void AOO_CALL AooSource_free(AooSource *src){
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::Source *>(src));
}

aoo::Source::~Source() {
    // free previous (unaccepted) metadata, if any
    free_metadata(stream_state_.load());
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
        return kAooErrorNotFound;       \
    }                                   \

AOO_API AooError AOO_CALL AooSource_control(
        AooSource *src, AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    return src->control(ctl, index, ptr, size);
}

AooError AOO_CALL aoo::Source::control(
        AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    switch (ctl){
    // activate sink
    case kAooCtlActivate:
    {
        CHECKARG(AooBool);
        GETSINKARG
        bool active = as<AooBool>(ptr);
        sink->activate(*this, active);

        break;
    }
    // check if sink is active
    case kAooCtlIsActive:
    {
        CHECKARG(AooBool);
        GETSINKARG
        as<AooBool>(ptr) = sink->is_active();
        break;
    }
    // set/get format
    case kAooCtlSetFormat:
        assert(size >= sizeof(AooFormat));
        return set_format(as<AooFormat>(ptr));
    case kAooCtlGetFormat:
        assert(size >= sizeof(AooFormat));
        return get_format(as<AooFormat>(ptr), size);
    // set/get channel onset
    case kAooCtlSetSinkChannelOffset:
    {
        CHECKARG(int32_t);
        GETSINKARG
        auto chn = as<int32_t>(ptr);
        sink->set_channel(chn);
        LOG_INFO("AooSource: send to sink " << sink->ep
                 << " on channel " << chn);
        break;
    }
    case kAooCtlGetSinkChannelOffset:
    {
        CHECKARG(int32_t);
        GETSINKARG
        as<int32_t>(ptr) = sink->channel();
        break;
    }
    // set/get id
    case kAooCtlSetId:
    {
        auto newid = as<int32_t>(ptr);
        if (id_.exchange(newid) != newid){
            restart_stream();
        }
        break;
    }
    case kAooCtlGetId:
        CHECKARG(int32_t);
        as<AooId>(ptr) = id();
        break;
    // reset source
    case kAooCtlReset:
    {
        // TODO: what should this do exactly?
        scoped_lock lock(update_mutex_); // writer lock!
        resampler_.reset();
        audio_queue_.reset();
        if (encoder_) {
            AooEncoder_reset(encoder_.get());
        }
        LOG_DEBUG("AooSource: reset after no sinks");
        reset_timer();
        break;
    }
    // set/get buffersize
    case kAooCtlSetBufferSize:
    {
        CHECKARG(AooSeconds);
        auto bufsize = std::max<AooSeconds>(as<AooSeconds>(ptr), 0);
        if (buffersize_.exchange(bufsize) != bufsize){
            scoped_lock lock(update_mutex_); // writer lock!
            update_audio_queue();
            if (!resampler_.bypass()) {
                // reset resampler, see process()!
                resampler_.reset();
            }
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
        const int32_t minpacketsize = kDataHeaderSize + 64;
        auto packetsize = as<int32_t>(ptr);
        if (packetsize < minpacketsize){
            LOG_WARNING("AooSource: packet size too small! setting to " << minpacketsize);
            packet_size_.store(minpacketsize);
        } else if (packetsize > AOO_MAX_PACKET_SIZE){
            LOG_WARNING("AooSource: packet size too large! setting to " << AOO_MAX_PACKET_SIZE);
            packet_size_.store(AOO_MAX_PACKET_SIZE);
        } else {
            packet_size_.store(packetsize);
        }
        break;
    }
    case kAooCtlGetPacketSize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packet_size_.load();
        break;
    // report xruns
    case kAooCtlReportXRun:
        CHECKARG(int32_t);
        handle_xrun(as<int32_t>(ptr));
        break;
    // set/get resample mode
    case kAooCtlSetResampleMethod:
    {
        CHECKARG(AooResampleMethod);
        auto method = as<AooResampleMethod>(ptr);
        if (method < 0) {
            return kAooErrorBadArgument;
        } else if (method >= kAooResampleMethodEnd) {
            return kAooErrorNotImplemented;
        }
        scoped_lock lock(update_mutex_); // writer lock!
        resample_method_.store(method);
        // update resampler! otherwise we would have to
        // wait for the next setup or format change.
        update_resampler();
        reset_timer(); // ?
        break;
    }
    case kAooCtlGetResampleMethod:
        CHECKARG(AooResampleMethod);
        as<AooResampleMethod>(ptr) = resample_method_.load();
        break;
    // set/get dynamic resampling
    case kAooCtlSetDynamicResampling:
    {
        CHECKARG(AooBool);
        bool b = as<AooBool>(ptr);
        dynamic_resampling_.store(b);
        reset_timer();
        break;
    }
    case kAooCtlGetDynamicResampling:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = dynamic_resampling_.load();
        break;
    // set/get time DLL filter bandwidth
    case kAooCtlSetDllBandwidth:
    {
        CHECKARG(float);
        auto bw = std::max<double>(0, std::min<double>(1, as<float>(ptr)));
        dll_bandwidth_.store(bw);
        reset_timer();
        break;
    }
    case kAooCtlGetDllBandwidth:
        CHECKARG(float);
        as<float>(ptr) = dll_bandwidth_.load();
        break;
    case kAooCtlResetDll:
        reset_timer();
        break;
    // get real samplerate
    case kAooCtlGetRealSampleRate:
        CHECKARG(double);
        as<double>(ptr) = realsr_.load();
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
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = ping_interval_.load();
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
    case kAooCtlSetBinaryFormat:
        CHECKARG(AooBool);
        binary_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetBinaryFormat:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = binary_.load();
        break;
    case kAooCtlSetStreamTimeSendInterval:
        CHECKARG(AooSeconds);
        tt_interval_.store(as<AooSeconds>(ptr));
        break;
    case kAooCtlGetStreamTimeSendInterval:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = tt_interval_.load();
        break;
#if AOO_NET
    case kAooCtlSetClient:
        client_ = reinterpret_cast<AooClient *>(index);
        break;
#endif
    // unknown
    default:
        LOG_WARNING("AooSource: unsupported control " << ctl);
        return kAooErrorNotImplemented;
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_codecControl(
        AooSource *source, const AooChar *codec, AooCtl ctl,
        AooIntPtr index, void *data, AooSize size)
{
    return source->codecControl(codec, ctl, index, data, size);
}

AooError AOO_CALL aoo::Source::codecControl(const AooChar *codec,
        AooCtl ctl, AooIntPtr index, void *data, AooSize size) {
    if (index != 0) {
        // per-sink encoders are not implemented (yet)
        return kAooErrorNotImplemented;
    }
    // we don't know which controls are setters and which
    // are getters, so we just take a writer lock either way.
    unique_lock lock(update_mutex_);
    if (encoder_) {
        if (!strcmp(encoder_->cls->name, codec)) {
            return AooEncoder_control(encoder_.get(), ctl, data, size);
        } else {
            LOG_ERROR("AooSource: tried to pass '" << codec << "' codec option to '"
                      << encoder_->cls->name << "' encoder");
            return kAooErrorBadArgument;
        }
    } else {
        return kAooErrorNotInitialized;
    }
}

AOO_API AooError AOO_CALL AooSource_setup(
        AooSource *src, AooInt32 nchannels, AooSampleRate samplerate,
        AooInt32 blocksize, AooFlag flags) {
    return src->setup(nchannels, samplerate, blocksize, flags);
}

AooError AOO_CALL aoo::Source::setup(
        AooInt32 nchannels, AooSampleRate samplerate,
        AooInt32 blocksize, AooFlag flags) {
    scoped_lock lock(update_mutex_); // writer lock!
    if (nchannels >= 0 && samplerate > 0 && blocksize > 0)
    {
        if (nchannels != nchannels_ || samplerate != samplerate_ ||
            blocksize != blocksize_ || flags != flags_)
        {
            flags_ = flags;
            nchannels_ = nchannels;
            blocksize_ = blocksize;
            samplerate_ = samplerate;

            realsr_.store(samplerate);

            if (encoder_) {
                update_audio_queue();

                update_resampler();

                update_historybuffer();

                sequence_ = invalid_stream;
            }

            restart_stream();
        }

        reset_timer(); // always reset!

        return kAooOk;
    } else {
        return kAooErrorBadArgument;
    }
}

AOO_API AooError AOO_CALL AooSource_handleMessage(
        AooSource *src, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen) {
    return src->handleMessage(data, size, address, addrlen);
}

// /aoo/src/<id>/format <sink>
AooError AOO_CALL aoo::Source::handleMessage(
        const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen){
    AooMsgType type;
    AooId src;
    AooInt32 onset;
    auto err = aoo_parsePattern(data, size, &type, &src, &onset);
    if (err != kAooOk){
        LOG_WARNING("AooSource: not an AoO message!");
        return kAooErrorBadArgument;
    }
    if (type != kAooMsgTypeSource){
        LOG_WARNING("AooSource: not a source message!");
        return kAooErrorBadArgument;
    }
    if (src != id()){
        LOG_WARNING("AooSource: wrong source ID!");
        return kAooErrorBadArgument;
    }

    ip_address addr((const sockaddr *)address, addrlen);

    if (aoo::binmsg_check(data, size)){
        // binary message
        auto cmd = aoo::binmsg_cmd(data, size);
        auto id = aoo::binmsg_from(data, size);
        switch (cmd){
        case kAooBinMsgCmdData:
            handle_data_request(data + onset, size - onset, id, addr);
            return kAooOk;
        default:
            LOG_WARNING("AooSink: unsupported binary message");
            return kAooErrorBadArgument;
        }
    } else {
        // OSC message
        try {
            osc::ReceivedPacket packet((const char *)data, size);
            osc::ReceivedMessage msg(packet);

            std::string_view pattern = msg.AddressPattern() + onset;
            if (pattern == kAooMsgStart) {
                handle_start_request(msg, addr);
            } else if (pattern == kAooMsgStop) {
                handle_stop_request(msg, addr);
            } else if (pattern == kAooMsgData) {
                handle_data_request(msg, addr);
            } else if (pattern == kAooMsgInvite) {
                handle_invite(msg, addr);
            } else if (pattern == kAooMsgUninvite) {
                handle_uninvite(msg, addr);
            } else if (pattern == kAooMsgPing) {
                handle_ping(msg, addr);
            } else if (pattern == kAooMsgPong) {
                handle_pong(msg, addr);
            } else {
                LOG_WARNING("AooSource: unknown message " << pattern);
                return kAooErrorNotImplemented;
            }
            return kAooOk;
        } catch (const osc::Exception& e){
            LOG_ERROR("AooSource: exception in handle_message: " << e.what());
            return kAooErrorBadFormat;
        }
    }
}

// find out if sendto() blocks
#define DEBUG_SEND_TIME 0

AOO_API AooError AOO_CALL AooSource_send(
        AooSource *src, AooSendFunc fn, void *user) {
    return src->send(fn, user);
}

AooError AOO_CALL aoo::Source::send(AooSendFunc fn, void *user) {
#if 1
    // avoid locking if stream is not running, see process().
    // NB: we must also check for requests, otherwise this would
    // break the /stop message.
    if (stream_state() != stream_state::run && requests_.empty()) {
        return kAooOk; // nothing to do
    }
#endif
    sendfn reply(fn, user);

    // *first* dispatch requests (/stop messages)
    dispatch_requests(reply);

    send_start(reply);

#if DEBUG_SEND_TIME
    auto t1 = aoo::time_tag::now();
#endif
    send_data(reply);
#if DEBUG_SEND_TIME
    auto t2 = aoo::time_tag::now();
    auto delta = (t2 - t1).to_seconds() * 1000.0;
    if (delta > 1.0) {
        LOG_DEBUG("AooSource: send_data() took " << delta << " ms");
    }
#endif

    resend_data(reply);

    send_ping(reply);

    if (sinks_.reclaim()){
        LOG_DEBUG("AooSource: free stale sinks");
    }

    return kAooOk;
}

/** \copydoc AooSource::addMessage() */
AOO_API AooError AOO_CALL AooSource_addStreamMessage(
        AooSource *src, const AooStreamMessage *message) {
    if (message) {
        return src->addStreamMessage(*message);
    } else {
        return kAooErrorBadArgument;
    }
}

AooError AOO_CALL aoo::Source::addStreamMessage(const AooStreamMessage& message) {
    if (message.size > kAooStreamMessageMaxSize) {
        return kAooErrorOverflow; // TODO: better error code?
    }
#if 1
    // avoid piling up stream messages
    if (stream_state() == stream_state::idle) {
        LOG_DEBUG("AooSource: ignore stream message (type: "
                  << aoo_dataTypeToString(message.type) << ", size: " << message.size
                  << ", offset: " << message.sampleOffset << ") while idle");
        return kAooErrorIdle;
    }
#endif
#if IDLE_IF_NO_SINKS
    if (sinks_.empty()) {
        return kAooErrorIdle;
    }
#endif
    uint64_t time;
    if (stream_state() == stream_state::start) {
        // This is the first block after startStream(), so we know that
        // we start from zero. NB: the stream will only be reset in the
        // process() function, so we must not use process_samples_!
        time = message.sampleOffset;
    } else {
        time = process_samples_ + message.sampleOffset;
    }
    message_queue_.emplace(time, message.channel, message.type,
                        (char *)message.data, message.size);
#if AOO_DEBUG_STREAM_MESSAGE
    LOG_DEBUG("AooSource: add stream message "
              << "(type: " << aoo_dataTypeToString(message.type)
              << ", channel: " << message.channel << ", size: " << message.size
              << ", offset: " << message.sampleOffset << ", time: " << time << ")");
#endif
    return kAooErrorNone;
}

AOO_API AooError AOO_CALL AooSource_process(
        AooSource *src, AooSample **data, AooInt32 n, AooNtpTime t) {
    return src->process(data, n, t);
}

AooError AOO_CALL aoo::Source::process(
        AooSample **data, AooInt32 nsamples, AooNtpTime t) {
    // check nsamples
    assert((flags_ & kAooFixedBlockSize) ? nsamples == blocksize_
                                         : nsamples <= blocksize_);

    auto full_state = stream_state_.load(std::memory_order_relaxed);
    auto state = full_state & stream_state_mask;
    if (state == stream_state::idle){
        if (!requests_.empty()) {
            return kAooOk; // user needs to call send()!
        } else {
            return kAooErrorIdle; // pausing
        }
    } else if (state == stream_state::stop) {
        int32_t offset = (full_state & metadata_mask) >> stream_state_bits;
        sink_lock lock(sinks_);
        for (auto& s : sinks_){
            s.stop(*this, offset);
        }

        // make sure that we haven't been started in the meantime
        stream_state_.compare_exchange_strong(full_state, stream_state::idle);
        // don't return kAooIdle because we want to send the /stop messages !
        return kAooOk;
    } else if (state == stream_state::start){
        // start -> play
        // The mutex should be uncontended most of the time. It is repeatedly
        // locked in the send() method, but only if the stream is running.
        unique_lock lock(update_mutex_, sync::try_to_lock); // writer lock!
        if (!lock.owns_lock()){
            LOG_DEBUG("AooSource: process would block");
            // no need to call xrun()!
            return kAooErrorIdle; // ?
        }

        // check if we have been stopped in the meantime; otherwise we
        // atomically exchange the state and obtain the metadata, if any.
        if (!stream_state_.compare_exchange_strong(full_state, stream_state::run)){
            return kAooErrorIdle; // pausing
        }
        // now we own the metadata!
        auto md = reinterpret_cast<AooData*>(full_state & metadata_mask);
        make_new_stream(t, md);
    }

    // Always update timers, even if there are no sinks.
    // Do it *before* trying to lock the mutex.
    // (The DLL is only ever touched in this method.)
    // NB: dynamic resampling requires fixed blocksize!
    bool dynamic_resampling = dynamic_resampling_.load() && (flags_ & kAooFixedBlockSize);
    if (need_reset_timer_.exchange(false, std::memory_order_relaxed)) {
        LOG_DEBUG("AooSource: start timer");
        start_tt_ = t;
        elapsed_time_.store(0);
        // it is safe to set 'last_ping_time' after updating
        // the timer, because in the worst case the ping
        // is simply sent the next time.
        last_ping_time_.store(-1e007); // force first ping
        // reset time DLL filter
        auto bw = dll_bandwidth_.load();
        dll_.setup(samplerate_, blocksize_, bw, 0);
        realsr_.store(samplerate_);
    } else {
        // advance timer
        auto elapsed = aoo::time_tag::duration(start_tt_, t);
        auto prev_elapsed = elapsed_time_.exchange(elapsed, std::memory_order_relaxed);
        if (dynamic_resampling) {
            if (flags_ & kAooPreciseTimestamp) {
                // directly calculate samplerate from timestamp
                auto delta = elapsed - prev_elapsed;
                if (delta > 0) {
                    auto realsr = blocksize_ / delta;
                    realsr_.store(realsr);
                } else {
                    LOG_WARNING("AooSource: bad time delta");
                }
            } else {
                // update time DLL
                assert(nsamples == blocksize_);
                dll_.update(elapsed);
            #if AOO_DEBUG_DLL
                LOG_DEBUG("AooSource: time elapsed: " << elapsed << ", period: "
                          << dll_.period() << ", samplerate: " << dll_.samplerate());
            #endif
                realsr_.store(dll_.samplerate());
            }
        }
    }

#if IDLE_IF_NO_SINKS
    if (sinks_.empty() && !sinks_.need_update() && requests_.empty()){
        // nothing to do. users still have to check for pending events,
        // but there is no reason to call send()
        return kAooErrorIdle;
    }
#endif

    // the mutex should be available most of the time.
    // it is only locked exclusively when setting certain options,
    // e.g. changing the buffer size.
    shared_lock lock(update_mutex_, sync::try_to_lock); // reader lock!
    if (!lock.owns_lock()){
        LOG_DEBUG("AooSource: process would block");
        add_xrun(nsamples);
        return kAooErrorIdle; // ?
    }

    if (!encoder_){
        return kAooErrorIdle;
    }
    assert(format_ != nullptr);

    // non-interleaved -> interleaved
    // only as many channels as current format needs
    auto nfchannels = format_->numChannels;
    auto bufsize = nsamples * nfchannels;
    assert(bufsize > 0);
    auto buf = (AooSample *)alloca(bufsize * sizeof(AooSample));
    if (data) {
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
    } else {
        // no buffers -> fill with zeros
        std::fill(buf, buf + bufsize, 0);
    }

    double sr;
    if (dynamic_resampling){
        sr = realsr_.load() / (double)samplerate_ * (double)format_->sampleRate;
    } else {
        sr = 0; // don't send samplerate! see send_data()
    }

#if AOO_DEBUG_AUDIO_BUFFER
    auto resampler_available = resampler_.balance() / (double)blocksize_;
    LOG_DEBUG("AooSource: audio_queue: " << audio_queue_.read_available() / resampler_.ratio()
              << ", resampler: " << resampler_size / resampler_.ratio()
              << ", capacity: " << audio_queue_.capacity() / resampler_.ratio());
#endif
    process_samples_ += nsamples;
    if (resampler_.bypass()) {
        assert(nsamples == format_->blockSize);
        // bypass resampler
        if (audio_queue_.write_available()){
            auto ptr = (block_data *)audio_queue_.write_data();
            // copy audio samples
            std::copy(buf, buf + bufsize, ptr->data);
            // push samplerate
            ptr->sr = sr;

            audio_queue_.write_commit();
        } else {
            LOG_WARNING("AooSource: send buffer overflow");
            add_xrun(nsamples);
            // NB: clients are still supposed to call send() to drain the buffer
            return kAooErrorOverflow;
        }
    } else {
        auto drain_resampler = [this, sr]() {
            bool success = false;

            while (audio_queue_.write_available()){
                // copy audio samples
                auto ptr = (block_data *)audio_queue_.write_data();
                if (!resampler_.read(ptr->data, format_->blockSize))
                    break;

                // push samplerate
                ptr->sr = sr;

                audio_queue_.write_commit();

                success = true;
            }

            return success;
        };

        // try to fill the resampler
        while (!resampler_.write(buf, nsamples)) {
            // try to make room in the resampler, in case we couldn't drain it
            // the last time (because the send thread was busy)
            if (!drain_resampler()) {
                // otherwise return overflow error
                // NB: clients are still supposed to call send() to drain the ring buffer
                LOG_WARNING("AooSource: send buffer overflow");
                add_xrun(nsamples);
                return kAooErrorOverflow;
            }
        }

        // try to move samples from resampler to ring buffer
        drain_resampler();
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_setEventHandler(
        AooSource *src, AooEventHandler fn,
        void *user, AooEventMode mode)
{
    return src->setEventHandler(fn, user, mode);
}

AooError AOO_CALL aoo::Source::setEventHandler(
        AooEventHandler fn, void *user, AooEventMode mode){
    event_handler_ = fn;
    event_context_ = user;
    event_mode_ = mode;
    return kAooOk;
}

AOO_API AooBool AOO_CALL AooSource_eventsAvailable(AooSource *src){
    return src->eventsAvailable();
}

AooBool AOO_CALL aoo::Source::eventsAvailable(){
    return !event_queue_.empty();
}

AOO_API AooError AOO_CALL AooSource_pollEvents(AooSource *src){
    return src->pollEvents();
}

AooError AOO_CALL aoo::Source::pollEvents(){
    // always thread-safe
    event_ptr e;
    while (event_queue_.pop(e)) {
        event_handler_(event_context_, &e->cast(), kAooThreadLevelUnknown);
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_startStream(
        AooSource *source, AooInt32 sampleOffset, const AooData *metadata)
{
    return source->startStream(sampleOffset, metadata);
}

AooError AOO_CALL aoo::Source::startStream(AooInt32 sampleOffset, const AooData *metadata) {
    // check metadata
    if (metadata) {
        // check data size
        if (metadata->size == 0){
            LOG_ERROR("AooSource: stream metadata cannot be empty!");
            return kAooErrorBadArgument;
        }
        LOG_DEBUG("AooSource: start stream with " << aoo_dataTypeToString(metadata->type)
                  << " metadata at sample offset " << sampleOffset);
    } else {
        LOG_DEBUG("AooSource: start stream at sample offset " << sampleOffset);
    }

    // copy metadata
    AooData *md = nullptr;
    if (sampleOffset > 0 || (metadata && metadata->size > 0)) {
        auto size = metadata ? flat_metadata_size(*metadata) : sizeof(AooData);
        md = (AooData *)aoo::rt_allocate(size);
        assert(((stream_state_type)md & stream_state_mask) == 0);
        if (metadata) {
            flat_metadata_copy(*metadata, *md);
        } else {
            // dummy metadata
            md->type = kAooDataUnspecified;
            md->size = 0;
            md->data = nullptr;
        }
        // HACK: bash offset into data pointer
        md->data = reinterpret_cast<AooByte*>((intptr_t)sampleOffset);
    }

    // combine metadata pointer with stream state
    auto state = stream_state::start | (stream_state_type)md;
    auto oldstate = stream_state_.exchange(state);
    // free previous (unaccepted) metadata, if any
    free_metadata(oldstate);

    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_stopStream(
        AooSource *source, AooInt32 sampleOffset) {
    return source->stopStream(sampleOffset);
}

AooError AOO_CALL aoo::Source::stopStream(AooInt32 sampleOffset) {
    LOG_DEBUG("AooSource: stop stream at sample offset " << sampleOffset);
    // combine metadata pointer with sample offset
    auto offset = (stream_state_type)sampleOffset << stream_state_bits;
    auto state = stream_state::stop | offset;
    auto oldstate = stream_state_.exchange(state);
    // free previous (unaccepted) metadata, if any
    free_metadata(oldstate);
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_addSink(
        AooSource *source, const AooEndpoint *sink, AooBool active)
{
    if (sink) {
        return source->addSink(*sink, active);
    } else {
        return kAooErrorBadArgument;
    }
}

AooError AOO_CALL aoo::Source::addSink(const AooEndpoint& ep, AooBool active) {
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);
    // NB: sinks can be added/removed from different threads,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock1(sink_mutex_);
    sink_lock lock2(sinks_);
    // check if sink exists!
    if (find_sink(addr, ep.id)){
        LOG_WARNING("AooSource: sink already added!");
        return kAooErrorAlreadyExists;
    }
    AooId stream = active ? get_random_id() : kAooIdInvalid;
    do_add_sink(addr, ep.id, stream);
    // always succeeds
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_removeSink(
        AooSource *source, const AooEndpoint *sink)
{
    if (sink) {
        return source->removeSink(*sink);
    } else {
        return kAooErrorNotFound;
    }
}

AooError AOO_CALL aoo::Source::removeSink(const AooEndpoint& ep) {
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);
    // NB: sinks can be added/removed from different threads,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock1(sink_mutex_);
    sink_lock lock2(sinks_);
    if (do_remove_sink(addr, ep.id)){
        return kAooOk;
    } else {
        return kAooErrorNotFound;
    }
}

AOO_API AooError AOO_CALL AooSource_removeAllSinks(AooSource *source)
{
    return source->removeAllSinks();
}

AooError AOO_CALL aoo::Source::removeAllSinks() {
    bool running = is_running();
    // NB: sinks can be added/removed from different threads,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock1(sink_mutex_);
    sink_lock lock2(sinks_);
    // just lock once for all stream ids, see do_remove_sink().
    scoped_shared_lock lock3(update_mutex_);
    // send /stop messages
    for (auto& s : sinks_){
        if (running && s.is_active()){
            sink_request r(request_type::stop, s.ep);
            r.stop.stream = s.stream_id();
            r.stop.offset = 0;
            push_request(r);
        }
    }
    sinks_.clear();
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSource_handleInvite(
        AooSource *source, const AooEndpoint *sink, AooId token, AooBool accept)
{
    if (sink) {
        return source->handleInvite(*sink, token, accept);
    } else {
        return kAooErrorBadArgument;
    }
}

AooError AOO_CALL aoo::Source::handleInvite(const AooEndpoint& ep, AooId token, AooBool accept) {
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, ep.id);
    if (sink){
        sink->handle_invite(*this, token, accept);
        return kAooOk;
    } else {
        LOG_ERROR("AooSource: couldn't find sink");
        return kAooErrorBadArgument;
    }
}

AOO_API AooError AOO_CALL AooSource_handleUninvite(
        AooSource *source, const AooEndpoint *sink, AooId token, AooBool accept)
{
    if (sink) {
        return source->handleUninvite(*sink, token, accept);
    } else {
        return kAooErrorBadArgument;
    }
}

AooError AOO_CALL aoo::Source::handleUninvite(const AooEndpoint& ep, AooId token, AooBool accept) {
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, ep.id);
    if (sink){
        sink->handle_uninvite(*this, token, accept);
        return kAooOk;
    } else {
        LOG_ERROR("AooSource: couldn't find sink");
        return kAooErrorBadArgument;
    }
}

//------------------------- source --------------------------------//

namespace aoo {

void Source::free_metadata(stream_state_type state) {
    auto ptr = reinterpret_cast<AooData*>(state & metadata_mask);
    if (ptr) {
        auto s = state & stream_state_mask;
        if (s == stream_state::start) {
            auto size = flat_metadata_size(*ptr);
            aoo::rt_deallocate(ptr, size);
        } else if (s != stream_state::stop) {
            // NB: the 'stop' state directly contains the sample offset, see stopStream()
            LOG_ERROR("AooSource: unknown stream state data");
        }
    }
}

sink_desc * Source::find_sink(const ip_address& addr, AooId id){
    for (auto& sink : sinks_){
        if (sink.ep.address == addr && sink.ep.id == id){
            return &sink;
        }
    }
    return nullptr;
}

aoo::sink_desc * Source::get_sink_arg(intptr_t index){
    auto ep = (const AooEndpoint *)index;
    if (!ep){
        LOG_ERROR("AooSource: missing sink argument");
        return nullptr;
    }
    ip_address addr((const sockaddr *)ep->address, ep->addrlen);
    auto sink = find_sink(addr, ep->id);
    if (!sink){
        LOG_ERROR("AooSource: couldn't find sink");
    }
    return sink;
}

// always called with sink mutex locked
// NB: do not call with update mutex locked!
sink_desc * Source::do_add_sink(const ip_address& addr, AooId id, AooId stream_id)
{
#if IDLE_IF_NO_SINKS
    // reset everything if sinks have been empty. For efficiency reasons we do not
    // process if there are no sinks; instead we return kAooErrorIdle, so the user
    // might not notify the send thread. This means that the resampler, audio buffer
    // and encoder might contain garbage.
    if (sinks_.empty()) {
        // we really want to avoid this lock...
        scoped_lock lock(update_mutex_); // writer lock!
        resampler_.reset();
        audio_queue_.reset();
        if (encoder_) {
            AooEncoder_reset(encoder_.get());
        }
        LOG_DEBUG("AooSource: reset after no sinks");
    }
#endif

#if AOO_NET
    ip_address relay;
    // check if the peer needs to be relayed
    if (client_){
        AooBool b;
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        if (client_->control(kAooCtlNeedRelay, reinterpret_cast<intptr_t>(&ep),
                             &b, sizeof(b)) == kAooOk) {
            if (b == kAooTrue){
                LOG_DEBUG("AooSource: sink " << addr << "|" << ep.id
                          << " needs to be relayed");
                // get relay address
                client_->control(kAooCtlGetRelayAddress,
                                 reinterpret_cast<intptr_t>(&ep),
                                 &relay, sizeof(relay));

            }
        }
    }
    auto it = sinks_.emplace_front(addr, relay, id, binary_.load(), stream_id);
#else
    auto it = sinks_.emplace_front(addr, id, binary_.load(), stream_id);
#endif
    // send /start if needed!
    if (is_running() && it->is_active()){
        it->notify_start();
        notify_start();
    }

    return &(*it);
}

// always called with sink mutex locked
// NB: do not call with update mutex locked!
bool Source::do_remove_sink(const ip_address& addr, AooId id){
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it){
        if (it->ep.address == addr && it->ep.id == id){
            // send /stop if needed!
            if (is_running() && it->is_active()) {
                sink_request r(request_type::stop, it->ep);
                {
                    scoped_shared_lock lock(update_mutex_);
                    r.stop.stream = it->stream_id();
                    r.stop.offset = 0;
                }
                push_request(r);
            }

            sinks_.erase(it);

            return true;
        }
    }
    LOG_WARNING("AooSource: sink not found!");
    return false;
}

AooError Source::set_format(AooFormat &f){
    auto codec = aoo::find_codec(f.codecName);
    if (!codec){
        LOG_ERROR("AooSource: codec '" << f.codecName << "' not supported!");
        return kAooErrorNotInitialized;
    }

    scoped_lock lock(update_mutex_); // writer lock!

    // create new encoder if necessary
    if (!encoder_ || strcmp(encoder_->cls->name, f.codecName)) {
        encoder_.reset(codec->encoderNew());
    }

    // setup encoder - will validate format!
    if (auto err = AooEncoder_setup(encoder_.get(), &f); err != kAooOk) {
        encoder_ = nullptr;
        format_ = nullptr;
        LOG_ERROR("AooSource: couldn't setup encoder!");
        return err;
    }

    // save validated format
    auto fmt = (AooFormat*)aoo::allocate(f.structSize);
    memcpy(fmt, &f, f.structSize);
    format_.reset(fmt);
    format_id_ = get_random_id();

    update_audio_queue();

    update_resampler();

    update_historybuffer();

    // restart stream if playing, but invalidate current stream!
    restart_stream();
    sequence_ = invalid_stream;

    return kAooOk;
}

AooError Source::get_format(AooFormat &fmt, size_t size){
    shared_lock lock(update_mutex_); // read lock!
    if (format_) {
        if (size >= format_->structSize){
            memcpy(&fmt, format_.get(), format_->structSize);
            return kAooOk;
        } else {
            return kAooErrorInsufficientBuffer;
        }
    } else {
        return kAooErrorNotInitialized;
    }
}

void Source::push_request(const sink_request &r){
    requests_.push(r);
}

void Source::notify_start(){
    LOG_DEBUG("AooSource: notify_start()");
    need_start_.exchange(true, std::memory_order_release);
}

void Source::send_event(event_ptr e, AooThreadLevel level){
    switch (event_mode_){
    case kAooEventModePoll:
        event_queue_.push(std::move(e));
        break;
    case kAooEventModeCallback:
        event_handler_(event_context_, &e->cast(), level);
        break;
    default:
        break;
    }
}

void Source::restart_stream() {
    // if playing, restart.
    // NB: metadata always goes together with stream_state::start,
    // but we only overwrite the state if it is stream_state::run!
    stream_state_type expected = stream_state::run;
    stream_state_.compare_exchange_strong(expected, stream_state::start);
}

// must be real-time safe; always called with update (try-)lock!
void Source::make_new_stream(aoo::time_tag tt, AooData *md) {
    stream_tt_ = tt;
    sequence_ = 0;
    xrunblocks_.store(0.0); // !
    reset_timer();

    // "accept" and store stream metadata
    metadata_.reset(md);

    // remove audio from previous stream
    resampler_.reset();

    audio_queue_.reset();

    history_.clear(); // !

    // NB: don't clear message_queue_ because it would break
    // addStreamMessage() in the first process block...
    // In practice, this shouldn't be an issue because messages
    // are almost immediately transferred to message_prio_queue_
    // on the network thread.
#if 0
    message_queue_.clear();
#endif
    message_prio_queue_.clear();
    process_samples_ = 0;
    stream_samples_ = 0;

    // reset encoder to avoid garbage from previous stream
    if (encoder_) {
        AooEncoder_reset(encoder_.get());
    }

    sink_lock lock(sinks_);
    for (auto& s : sinks_){
        s.start();
    }

    notify_start();
}

void Source::add_xrun(int32_t nsamples) {
    // add with CAS loop
    auto nblocks = (double)nsamples / (double)blocksize_;
    auto current = xrunblocks_.load(std::memory_order_relaxed);
    while (!xrunblocks_.compare_exchange_weak(current, current + nblocks))
        ;
    // NB: advance process samples, so we don't break stream timestamp
    process_samples_ += nsamples;
}

void Source::handle_xrun(int32_t nsamples) {
    LOG_DEBUG("AooSource: handle xrun (" << nsamples << " samples)");
    add_xrun(nsamples);
    // also reset time DLL!
    reset_timer();
}

void Source::update_audio_queue() {
    if (encoder_ && samplerate_ > 0) {
        assert(format_ != nullptr);
        // convert buffersize from seconds to samples
        auto buffersize = buffersize_.load();
        int32_t buffersamples = buffersize * (double)samplerate_;
        auto d = std::div(buffersamples, format_->blockSize);
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        // minimum buffer size depends on resampling and reblocking!
        auto resample = (double)format_->sampleRate / (double)samplerate_;
        auto reblock = (double)format_->blockSize / (double)blocksize_;
        int32_t minblocks = std::ceil(resample / reblock);
        nbuffers = std::max<int32_t>(nbuffers, minblocks);
        LOG_DEBUG("AooSource: buffersize (ms): " << (buffersize * 1000.0)
                  << ", samples: " << buffersamples << ", nbuffers: " << nbuffers
                  << ", minimum: " << minblocks);

        // resize audio buffer
        auto nsamples = format_->blockSize * format_->numChannels;
        auto nbytes = block_data::header_size + nsamples * sizeof(AooSample);
        // align to 8 bytes
        nbytes = (nbytes + 7) & ~7;
        audio_queue_.resize(nbytes, nbuffers);
    #if 1
        audio_queue_.shrink_to_fit();
    #endif
    }
}

void Source::update_resampler() {
    if (format_ && samplerate_ > 0) {
        resampler_.setup(blocksize_, format_->blockSize, flags_ & kAooFixedBlockSize,
                         samplerate_, format_->sampleRate, true, // always fixed sr!
                         format_->numChannels, resample_method_.load());
        if (resampler_.bypass()) {
            LOG_DEBUG("AooSource: bypass resampler");
        }
    }
}

void Source::update_historybuffer(){
    if (encoder_){
        // bufsize can also be 0 (= don't resend)!
        int32_t bufsize = resend_buffersize_.load() * format_->sampleRate;
        auto d = std::div(bufsize, format_->blockSize);
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        history_.resize(nbuffers);
        LOG_DEBUG("AooSource: history buffersize (ms): "
                  << (resend_buffersize_.load() * 1000.0)
                  << ", samples: " << bufsize << ", nbuffers: " << nbuffers);

    }
}

// /aoo/sink/<id>/start <src> <version> <stream_id> <seq_start>
// <format_id> <nchannels> <samplerate> <blocksize> <codec> <extension>
// <tt> <latency> <codec_delay> (<metadata_type) (metadata_content>) <offset>
void send_start_msg(const endpoint& ep, int32_t id, int32_t stream_id,
                    int32_t seq_start, int32_t format_id, const AooFormat& f,
                    const AooByte *extension, AooInt32 size,
                    aoo::time_tag tt, int32_t latency, int32_t codec_delay,
                    const AooData* metadata, int32_t offset, const sendfn& fn) {
    LOG_DEBUG("AooSource: send " kAooMsgStart " to " << ep
              << " (stream = " << stream_id << ")");

    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSinkLen + 16 + kAooMsgStartLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSink, (int)ep.id, kAooMsgStart);

    msg << osc::BeginMessage(address) << id << aoo_getVersionString()
        << stream_id << seq_start << format_id
        << f.numChannels << f.sampleRate << f.blockSize
        << f.codecName << osc::Blob(extension, size)
        << osc::TimeTag(tt) << latency << codec_delay
        << metadata_view(metadata) << offset
        << osc::EndMessage;

    ep.send(msg, fn);
}

// /aoo/sink/<id>/stop <src> <stream_id> <last_seq> <offset>
void send_stop_msg(const endpoint& ep, int32_t id, int32_t stream,
                   int32_t last_seq, int32_t offset, const sendfn& fn) {
    LOG_DEBUG("AooSource: send " kAooMsgStop " to " << ep
              << " (stream = " << stream << ")");

    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSinkLen + 16 + kAooMsgStopLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSink, (int)ep.id, kAooMsgStop);

    msg << osc::BeginMessage(address) << id << stream << last_seq << offset
        << osc::EndMessage;

    ep.send(msg, fn);
}

// /aoo/sink/<id>/decline <src> <token>
void send_decline_msg(const endpoint& ep, int32_t id, int32_t token, const sendfn& fn) {
    LOG_DEBUG("AooSource: send " kAooMsgDecline " to " << ep
              << " (stream = " << token << ")");

    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSinkLen + 16 + kAooMsgDeclineLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSink, (int)ep.id, kAooMsgDecline);

    msg << osc::BeginMessage(address) << id << token << osc::EndMessage;

    ep.send(msg, fn);
}

// /aoo/sink/<id>/pong <src> <tt1> <tt2> <tt3>
void send_pong_msg(const endpoint& ep, int32_t id, aoo::time_tag tt1,
                   aoo::time_tag tt2, const sendfn& fn) {
    LOG_DEBUG("AooSource: send " kAooMsgPong " to " << ep);

    auto tt3 = aoo::time_tag::now(); // local send time

    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSinkLen + 16 + kAooMsgDeclineLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSink, (int)ep.id, kAooMsgPong);

    msg << osc::BeginMessage(address) << id
        << osc::TimeTag(tt1) << osc::TimeTag(tt2) << osc::TimeTag(tt3)
        << osc::EndMessage;

    ep.send(msg, fn);
}

void Source::dispatch_requests(const sendfn& fn){
    sink_request r;
    while (requests_.pop(r)) {
        switch (r.type) {
        case request_type::stop:
        {
            int32_t last_seq;
            int32_t offset;
            {
                shared_lock updatelock(update_mutex_); // reader lock!
                offset = r.stop.offset * resampler_.ratio();
                // TODO: the stream might have changed in the meantime,
                // in which case the sequence number would not be valid...
                last_seq = sequence_;
            }
            send_stop_msg(r.ep, id(), r.stop.stream, last_seq, offset, fn);
            break;
        }
        case request_type::decline:
            send_decline_msg(r.ep, id(), r.decline.token, fn);
            break;
        case request_type::pong:
            send_pong_msg(r.ep, id(), r.pong.tt1, r.pong.tt2, fn);
            break;
        default:
            LOG_ERROR("AooSource: unknown request type");
        }
    }
}

void Source::send_start(const sendfn& fn){
#if 0
    // a) send /start message as soon as possible
    if (!need_start_.exchange(false, std::memory_order_acquire)) {
        return;
    }

    shared_lock updatelock(update_mutex_); // reader lock!

    if (!encoder_ || sequence_ == invalid_stream){
        return;
    }
#else
    // b) send /start message only when audio is ready to be send
    if (!need_start_.load(std::memory_order_relaxed)) {
        return;
    }

    shared_lock updatelock(update_mutex_); // reader lock!

    // wait until we have data to send
    if (audio_queue_.read_available() == 0) {
        return;
    }

    if (!encoder_ || sequence_ == invalid_stream){
        return;
    }

    // now we can finally send
    if (!need_start_.exchange(false, std::memory_order_acquire)) {
        return;
    }
#endif
    assert(format_ != nullptr);

    // calculate stream start time.
    auto tt = stream_tt_ + aoo::time_tag::from_seconds(stream_samples_ / (double)format_->sampleRate);
    // calculate reblocking/resampling latency
    auto ratio = resampler_.ratio();
    auto reblock = std::max<double>(0.0, (double)format_->blockSize - (double)blocksize_ * ratio);
    auto latency = static_cast<int32_t>(reblock + resampler_.latency() * ratio);
    // get codec delay
    AooInt32 codec_delay = 0;
    AooEncoder_control(encoder_.get(), kAooCodecCtlGetLatency, AOO_ARG(codec_delay));

    // cache sequence number start
    auto seq_start = sequence_;

    // cache stream format
    auto format_id = format_id_;
    assert(format_id >= 0);

    AooFormatStorage f;
    memcpy(&f, format_.get(), format_->structSize);

    // serialize format extension
    AooByte extension[kAooFormatExtMaxSize];
    AooInt32 size = kAooFormatExtMaxSize;

    if (encoder_->cls->serialize(&f.header, extension, &size) != kAooOk) {
        return;
    }

    // obtain stream metadata and/or sample offset
    AooData *md = nullptr;
    int32_t offset = 0;
    if (metadata_) {
        // offset has been bashed into data pointer, see startStream()
        offset = reinterpret_cast<intptr_t>(metadata_->data) * ratio;
        if (metadata_->size > 0) {
            // restore data pointer!
            metadata_->data = (AooByte *)metadata_.get() + sizeof(AooData);
            // copy metadata
            auto mdsize = flat_metadata_size(*metadata_);
            md = (AooData *)alloca(mdsize);
            flat_metadata_copy(*metadata_, *md);
        }
    }
#if IDLE_IF_NO_SINKS
    // cache sinks that need to send a /start message
    if (cached_sinks_.empty()) {
        // if there were no (active) sinks, we need to reset the encoder to
        // prevent nasty artifacts. (For efficiency reasons, we skip the encoding
        // process in send_data() if there are no active sinks.)
        AooEncoder_reset(encoder_.get());
        LOG_DEBUG("AooSource: sinks previously empty/inactive - reset encoder");
    }
#endif
    cached_sinks_.clear();
    sink_lock lock(sinks_);
    for (auto& s : sinks_){
        if (s.need_start()){
            cached_sinks_.emplace_back(s);
        }
    }

    // send messages without lock!
    updatelock.unlock();

    for (auto& s : cached_sinks_){
        send_start_msg(s.ep, id(), s.stream_id, seq_start, format_id, f.header,
                       extension, size, tt, latency, codec_delay, md, offset, fn);
    }
}

// binary data message:
// stream_id (int32), seq (int32), channel (uint8), flags (uint8), data_size (uint16),
// [total (int32), nframes (int16), frame (int16)], [msgsize (int32)], [sr (float64)],
// [tt, (uint64)], data...

AooSize write_bin_data(AooByte *buffer, AooSize size,
                       AooId stream_id, const data_packet& d)
{
    assert(size >= 32);

    // write arguments
    auto it = buffer;
    aoo::write_bytes<int32_t>(stream_id, it);
    aoo::write_bytes<int32_t>(d.sequence, it);
    aoo::write_bytes<uint8_t>(d.channel, it);
    aoo::write_bytes<uint8_t>(d.flags, it);
    aoo::write_bytes<uint16_t>(d.size, it);
    if (d.flags & kAooBinMsgDataFrames) {
        aoo::write_bytes<uint32_t>(d.total_size, it);
        aoo::write_bytes<uint16_t>(d.num_frames, it);
        aoo::write_bytes<uint16_t>(d.frame_index, it);
    }
    if (d.flags & kAooBinMsgDataStreamMessage) {
        aoo::write_bytes<uint32_t>(d.msg_size, it);
    }
    if (d.flags & kAooBinMsgDataSampleRate) {
        aoo::write_bytes<double>(d.samplerate, it);
    }
    if (d.flags & kAooBinMsgDataTimeStamp) {
        aoo::write_bytes<uint64_t>(d.tt, it);
    }
    // write audio data
    if (d.size > 0) {
        memcpy(it, d.data, d.size);
    }
    it += d.size;

    return (it - buffer);
}

// /aoo/sink/<id>/data <src> <stream_id> <seq> (<tt>) (<sr>) <channel_onset>
// <totalsize> (<msgsize>) (<nframes>) (<frame>) (<data>)

void send_packet_osc(const endpoint& ep, AooId id, int32_t stream_id,
                     const data_packet& d, const sendfn& fn) {
    char buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    char address[kDataMaxAddrSize];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSink, (int)ep.id, kAooMsgData);

    msg << osc::BeginMessage(address) << id << stream_id << d.sequence;
    if (d.flags & kAooBinMsgDataTimeStamp) {
        msg << osc::TimeTag(d.tt);
    } else {
        msg << osc::Nil;
    }
    if (d.flags & kAooBinMsgDataSampleRate) {
        msg << d.samplerate;
    } else {
        msg << osc::Nil;
    }
    msg << d.channel << d.total_size;
    if (d.flags & kAooBinMsgDataStreamMessage) {
        msg << d.msg_size;
    } else {
        msg << osc::Nil;
    }
    if (d.flags & kAooBinMsgDataFrames) {
        msg << d.num_frames << d.frame_index;
    } else {
        msg << osc::Nil << osc::Nil;
    }
    if (d.flags & kAooBinMsgDataXRun) {
        msg << osc::Nil;
    } else {
        msg << osc::Blob(d.data, d.size);
    }
    msg << osc::EndMessage;

#if AOO_DEBUG_DATA
    LOG_DEBUG("AooSource: send block: seq = " << d.sequence << ", tt = " << d.tt << ", sr = "
              << d.samplerate << ", chn = " << d.channel << ", totalsize = " << d.totalsize
              << ", msgsize = " << d.msgsize << ", nframes = " << d.nframes
              << ", frame = " << d.frame << ", size " << d.size);
#endif
    ep.send(msg, fn);
}

// binary data message:
// stream_id (int32), seq (int32), channel (uint8), flags (uint8), size (uint16)
// [total (int32), nframes (int16), frame (int16)], [msgsize (int32)], [sr (float64)],
// [tt (uint64)], data...

void send_packet_bin(const endpoint& ep, AooId id, AooId stream_id,
                     const data_packet& d, const sendfn& fn) {
    AooByte buf[AOO_MAX_PACKET_SIZE];

    auto onset = aoo::binmsg_write_header(buf, sizeof(buf), kAooMsgTypeSink,
                                          kAooBinMsgCmdData, ep.id, id);
    auto argsize = write_bin_data(buf + onset, sizeof(buf) - onset, stream_id, d);
    auto size = onset + argsize;

#if AOO_DEBUG_DATA
    LOG_DEBUG("AooSource: send block: seq = " << d.sequence << ", tt = " << d.tt
              << ", sr = " << d.samplerate << ", chn = " << s.channel << ", msgsize = "
              << d.msgsize << ", totalsize = " << d.totalsize << ", nframes = "
              << d.nframes << ", frame = " << d.frame << ", size " << d.size);
#endif

    ep.send(buf, size, fn);
}

void send_packet(const aoo::vector<cached_sink>& sinks, const AooId id,
                 data_packet& d, const sendfn &fn, bool binary) {
    if (binary){
        AooByte buf[AOO_MAX_PACKET_SIZE];

        // start at max. header size
        auto args = buf + kAooBinMsgLargeHeaderSize;
        auto argsize = write_bin_data(args, sizeof(buf) - kAooBinMsgLargeHeaderSize,
                                      kAooIdInvalid, d);
        auto end = args + argsize;

        for (auto& s : sinks) {
        #if AOO_DEBUG_DATA
            LOG_DEBUG("AooSource: send block: seq = " << d.sequence << << ", tt = " << d.tt
                      << ", sr = " << d.samplerate << ", chn = " << s.channel << ", msgsize = "
                      << d.msgsize << ", totalsize = " << d.totalsize << ", nframes = "
                      << d.nframes << ", frame = " << d.frame << ", size " << d.size);
        #endif
            // write header
            bool large =  s.ep.id > 255 || id > 255;
            auto start = large ? buf : (args - kAooBinMsgHeaderSize);
            aoo::binmsg_write_header(start, args - start, kAooMsgTypeSink,
                                     kAooBinMsgCmdData, s.ep.id, id);
            // replace stream ID and channel
            aoo::to_bytes(s.stream_id, args);
            args[8] = s.channel;

            s.ep.send(start, end - start, fn);
        }
    } else {
        for (auto& s : sinks){
            // set channel!
            d.channel = s.channel;
            send_packet_osc(s.ep, id, s.stream_id, d, fn);
        }
    }
}

#define XRUN_THRESHOLD 0.5

#define XRUN_FLOOR 0
#define XRUN_CEIL 1
#define XRUN_ROUND 2
// NB: for now we always use the XRUN_FLOOR method to prevent
// stream_samples_ from running ahead of process_samples_!
#ifndef XRUN_METHOD
# define XRUN_METHOD XRUN_FLOOR
#endif

#define SKIP_OUTDATED_MESSAGES 1

void Source::send_xruns(const sendfn &fn) {
    // *first* check for dropped blocks
    if (xrunblocks_.load(std::memory_order_relaxed) > XRUN_THRESHOLD){
        shared_lock updatelock(update_mutex_); // reader lock
        // send empty stream blocks for xrun blocks to fill up the missing time.
        auto xrunblocks = xrunblocks_.exchange(0.0);
        auto convert = resampler_.ratio() * (double)blocksize_ / (double)format_->blockSize;
        // convert xrunblocks to stream blocks.
        // If the format uses a larger blocksize, the stream might run a little bit ahead
        // of time. To mitigate this problem, the block difference is subtracted from
        // xrunblocks_ so that processing may catch up with subsequent calls to add_xrun().
        // NB: if the method is XRUN_FLOOR or XRUN_ROUND, the difference may be negative,
        // in which case it is effectively added back to xrunblocks_.
    #if XRUN_METHOD == XRUN_FLOOR
        int stream_blocks = std::floor(xrunblocks * convert);
    #elif XRUN_METHOD == XRUN_CEIL
        int stream_blocks = std::ceil(xrunblocks * convert);
    #elif XRUN_METHOD == XRUN_ROUND
        int stream_blocks = xrunblocks * convert + 0.5;
    #else
        #error "unknown xrun method"
    #endif
        auto process_blocks = stream_blocks / convert;
        auto diff = process_blocks - xrunblocks;
    #if XRUN_BLOCKS_CEIL
        assert(diff >= 0);
    #endif
        // subtract diff with a CAS loop
        auto current = xrunblocks_.load(std::memory_order_relaxed);
        while (!xrunblocks_.compare_exchange_weak(current, current - diff))
            ;
        // advance stream time, see add_xrun()
        stream_samples_ += process_blocks * blocksize_;

        // cache sinks
        cached_sinks_.clear();
        sink_lock lock(sinks_);
        for (auto& s : sinks_){
            if (s.is_active()){
                cached_sinks_.emplace_back(s);
            }
        }
        lock.unlock();
        // if we don't have any (active) sinks, we do not actually need to send anything!
        if (cached_sinks_.empty()) {
            return;
        }

        // send empty blocks
        if (stream_blocks > 0) {
            LOG_DEBUG("AooSource: send " << stream_blocks << " empty blocks for "
                      "xrun (" << xrunblocks << " blocks)");
        }
        while (stream_blocks--){
            // check the encoder and make snapshost of stream_id
            // in every iteration because we release the lock
            if (!encoder_ || sequence_ == invalid_stream){
                return;
            }
            // send empty block
            // NOTE: we're the only thread reading 'sequence_', so we can increment
            // it even while holding a reader lock!
            data_packet d;
            d.sequence = sequence_++;
            d.tt = 0; // omit
            d.samplerate = 0; // use nominal samplerate
            d.channel = 0;
            d.total_size = 0;
            d.msg_size = 0;
            d.num_frames = 0;
            d.frame_index = 0;
            d.data = nullptr;
            d.size = 0;
            // omit all other flags!
            d.flags = kAooBinMsgDataXRun;

            // wrap around to prevent signed integer overflow
            if (sequence_ == INT32_MAX) {
                sequence_ = 0;
            }

            // save block (if we have a history buffer)
            if (history_.capacity() > 0) {
                history_.push()->set(d, 0);
            }

            // now we can unlock
            updatelock.unlock();

            // send block to all sinks
            send_packet(cached_sinks_, id(), d, fn, binary_.load());

            updatelock.lock();
        }
    }
}

// This method reads audio samples from the ringbuffer,
// encodes them and sends them to all sinks.
void Source::send_data(const sendfn& fn){
    // *first* handle xruns
    send_xruns(fn);

    // then send audio
    shared_lock updatelock(update_mutex_); // reader lock
    while (audio_queue_.read_available()) {
        // NB: recheck one every iteration because we temporarily release the lock!
        if (!encoder_ || sequence_ == invalid_stream) {
            return;
        }
        assert(format_ != nullptr);

        // reset and reserve space for message count
        sendbuffer_.resize(4);
        uint32_t msg_count = 0;
        double deadline = stream_samples_ + (double)format_->blockSize / resampler_.ratio();
        aoo::time_tag tt;
        // calculate stream timestamp (if required)
        if (auto interval = tt_interval_.load(); interval > 0) {
            // convert interval to blocks
            auto blocks = std::max<int32_t>(1, interval * (double)format_->sampleRate / (double)format_->blockSize);
            if ((sequence_ % blocks) == 0) {
                tt = stream_tt_ + aoo::time_tag::from_seconds(stream_samples_ / (double)samplerate_);
            }
        }

        // handle stream messages.
        // Copy into priority queue to avoid draining the RT memory pool
        // when scheduling many messages in the future.
        // NB: we have to pop messages in sync with the audio queue!
        message_queue_.consume_all([&](const auto& msg) {
            auto offset = (int64_t)msg.time - (int64_t)stream_samples_;
        #if SKIP_OUTDATED_MESSAGES
            if (offset < 0) {
                // skip outdated message; can happen with xrun blocks
                LOG_INFO("AooSource: skip stream message (offset: " << offset << ")");
            } else
        #endif
            message_prio_queue_.emplace(msg.time, msg.channel, msg.type, msg.data, msg.size);
        #if AOO_DEBUG_STREAM_MESSAGE
            LOG_DEBUG("AooSource: schedule stream message "
                      << "(type: " << aoo_dataTypeToString(msg.type)
                      << ", channel: " << msg.channel << ", size: " << msg.size
                      << ", time: " << msg.time << ")");
        #endif
        });
        // dispatch scheduled stream messages
        while (!message_prio_queue_.empty()) {
            auto& msg = message_prio_queue_.top();
            if (msg.time < (uint64_t)deadline) {
                // add header
                std::array<char, 8> buffer;
                auto offset = ((int64_t)msg.time - (int64_t)stream_samples_) * resampler_.ratio();
            #if SKIP_OUTDATED_MESSAGES
                assert(offset >= 0);
            #else
                offset = std::max(0.0, offset);
            #endif
                aoo::to_bytes<uint16_t>(offset, &buffer[0]);
                aoo::to_bytes<uint16_t>(msg.channel, &buffer[2]);
                aoo::to_bytes<uint16_t>(msg.type, &buffer[4]);
                aoo::to_bytes<uint16_t>(msg.size, &buffer[6]);
                sendbuffer_.insert(sendbuffer_.end(), buffer.begin(), buffer.end());
                // add data
                sendbuffer_.insert(sendbuffer_.end(), msg.data, msg.data + msg.size);
                // add padding bytes (total size is rounded up to 4 bytes.)
                auto remainder = msg.size & 3;
                if (remainder > 0) {
                    sendbuffer_.resize(sendbuffer_.size() + 4 - remainder);
                }
            #if AOO_DEBUG_STREAM_MESSAGE
                LOG_DEBUG("AooSource: send stream message "
                          << "(type: " << aoo_dataTypeToString(msg.type)
                          << ", size: " << msg.size
                          << ", offset: " << offset << ")");
            #endif
                msg_count++;
                message_prio_queue_.pop();
            } else {
                break;
            }
        }
        if (msg_count > 0) {
            // finally write message count
            aoo::to_bytes<uint32_t>(msg_count, sendbuffer_.data());
        } else {
            // no stream message data
            sendbuffer_.clear();
        }

        stream_samples_ = deadline;

        // cache sinks
        bool had_sinks = cached_sinks_.size();
        cached_sinks_.clear();
        sink_lock lock(sinks_);
        for (auto& s : sinks_){
            if (s.is_active()){
                cached_sinks_.emplace_back(s);
            }
        }
        lock.unlock();
        // if we don't have any (active) sinks, we do not actually need
        // to encode and send the data!
        if (cached_sinks_.empty()) {
            audio_queue_.read_commit(); // !
            if (had_sinks) {
                assert(encoder_);
                LOG_DEBUG("AooSource: clear encoder (no sinks)");
                // reset encoder to prevent artifacts
                AooEncoder_reset(encoder_.get());
            }
            continue;
        }

        auto ptr = (block_data *)audio_queue_.read_data();

        data_packet d;
        d.tt = tt;
        d.samplerate = ptr->sr;
        d.channel = 0;
        d.flags = 0;
        d.msg_size = sendbuffer_.size();
        // message size must be a multiple of 4!
        assert((d.msg_size & 3) == 0);

        // copy and convert audio samples to blob data
        auto nchannels = format_->numChannels;
        auto framesize = format_->blockSize;
        auto nsamples = nchannels * framesize;
    #if 0
        Log log;
        for (int i = 0; i < nsamples; ++i){
            log << ptr->data[i] << " ";
        }
    #endif

        int32_t audio_size = sizeof(double) * nsamples; // overallocate
        sendbuffer_.resize(d.msg_size + audio_size);

        auto err = AooEncoder_encode(encoder_.get(), ptr->data, framesize,
            sendbuffer_.data() + d.msg_size, &audio_size);
        d.total_size = d.msg_size + audio_size;

        audio_queue_.read_commit(); // always commit!

        if (err != kAooOk){
            LOG_WARNING("AooSource: couldn't encode audio data!");
            return;
        }

        // NOTE: we're the only thread reading 'sequence_', so we can increment
        // it even while holding a reader lock!
        d.sequence = sequence_++;
        // wrap around to prevent signed integer overflow
        if (sequence_ == INT32_MAX) {
            sequence_ = 0;
        }

        // calculate number of frames
        bool binary = binary_.load();
        auto packetsize = packet_size_.load();
        auto maxpacketsize = packetsize -
                (binary ? kBinDataHeaderSize : kDataHeaderSize);
        auto dv = std::div(d.total_size, maxpacketsize);
        d.num_frames = dv.quot + (dv.rem != 0);

        // make flags
        if (d.samplerate != 0){
            d.flags |= kAooBinMsgDataSampleRate;
        }
        if (d.num_frames > 1){
            d.flags |= kAooBinMsgDataFrames;
        }
        if (d.msg_size > 0){
            d.flags |= kAooBinMsgDataStreamMessage;
        }
        if (!d.tt.is_empty()) {
            d.flags |= kAooBinMsgDataTimeStamp;
        }

        // save block (if we have a history buffer)
        if (history_.capacity() > 0){
            d.data = sendbuffer_.data();
            history_.push()->set(d, maxpacketsize);
        }

        // unlock before sending!
        updatelock.unlock();

        // from here on we don't hold any lock!

        // send a single frame to all sinks
        // /aoo/<sink>/data <src> <stream_id> <seq> <sr> <channel_onset>
        // <totalsize> <msgsize> <numframes> <frame> <data>
        auto dosend = [&](int32_t frame, const AooByte* data, auto n){
            d.frame_index = frame;
            d.data = data;
            d.size = n;
            // send block to all sinks
            send_packet(cached_sinks_, id(), d, fn, binary);
        };

        auto ntimes = redundancy_.load();
        for (auto i = 0; i < ntimes; ++i){
            auto ptr = sendbuffer_.data();
            // send large frames (might be 0)
            for (int32_t j = 0; j < dv.quot; ++j, ptr += maxpacketsize){
                dosend(j, ptr, maxpacketsize);
            }
            // send remaining bytes as a single frame (might be the only one!)
            // also make sure to send frames encoded with null codec.
            if (dv.rem || d.total_size == 0){
                dosend(dv.quot, ptr, dv.rem);
            }
        }

        updatelock.lock();
    }
}

void Source::resend_data(const sendfn &fn) {
    shared_lock updatelock(update_mutex_); // reader lock for history buffer!
    if (!history_.capacity()){
        // NB: there should not be any requests if resending is disabled,
        // see handle_data_request().
        return;
    }

    struct frame_data {
        int32_t index;
        int32_t size;
        AooByte* data;
    };

    std::array<frame_data, 16> small_framevec;
    std::vector<frame_data> large_framevec;

    // send block to sinks
    sink_lock lock(sinks_);
    for (auto& s : sinks_) {
        int count = 0;
        data_request r;
        while (s.get_data_request(r)) {
        #if AOO_DEBUG_RESEND && 0
            LOG_DEBUG("AooSource: dispatch data request (" << r.sequence
                      << " " << r.offset << " " << r.bitset << ")");
        #endif
            auto block = history_.find(r.sequence);
            if (block) {
                bool binary = binary_.load();

                auto stream_id = s.stream_id();

                data_packet d;
                d.sequence = block->sequence;
                d.msg_size = block->message_size;
                d.samplerate = block->samplerate;
                d.channel = s.channel();
                d.total_size = block->size();
                d.num_frames = block->num_frames;
                d.flags = block->flags;

                // We need to copy all (requested) frames before sending
                // because we temporarily release the update lock!
                // We use a buffer on the heap because blocks and even frames
                // can be quite large and we don't want them to sit on the stack.
                sendbuffer_.resize(d.total_size);
                AooByte *buf = sendbuffer_.data();

                // Keep track of the frames we will eventually send.
                frame_data* framevec;
                if (d.num_frames <= small_framevec.size()) {
                    framevec = small_framevec.data();
                } else {
                    large_framevec.resize(d.num_frames);
                    framevec = large_framevec.data();
                }
                int32_t numframes = 0;
                int32_t buf_offset = 0;

                if (d.num_frames > 0) {
                    // copy and send frames
                    auto copy_frame = [&](int32_t index) {
                        auto nbytes = block->get_frame(index, buf + buf_offset,
                                                       d.total_size - buf_offset);
                        if (nbytes > 0) {
                            auto& frame = framevec[numframes];
                            frame.index = index;
                            frame.size = nbytes;
                            frame.data = buf + buf_offset;

                            buf_offset += nbytes;
                            numframes++;
                        } else {
                            LOG_ERROR("AooSource: empty frame!");
                        }
                    };

                    if (r.offset < 0) {
                        // a) whole block: copy all frames
                        for (int i = 0; i < d.num_frames; ++i){
                            copy_frame(i);
                        }
                    } else {
                        // b) only copy requested frames
                        uint16_t bitset = r.bitset;
                        for (int i = 0; bitset != 0; ++i, bitset >>= 1) {
                            if (bitset & 1) {
                                auto index = r.offset + i;
                                if (index < d.num_frames) {
                                    copy_frame(index);
                                } else {
                                    LOG_ERROR("AooSource: frame number " << index << " out of range!");
                                }
                            }
                        }
                    }
                } else {
                    // resend empty block
                    auto& frame = framevec[0];
                    frame.index = 0;
                    frame.size = 0;
                    frame.data = nullptr;
                    numframes = 1;
                }
                // unlock before sending
                updatelock.unlock();

                // send frames to sink
                for (int i = 0; i < numframes; ++i) {
                    auto& frame = framevec[i];
                    d.frame_index = frame.index;
                    d.size = frame.size;
                    d.data = frame.data;
                #if AOO_DEBUG_RESEND
                    LOG_DEBUG("AooSource: resend " << d.sequence
                              << " (" << d.frame << " / " << d.nframes << ")");
                #endif
                    if (binary){
                        send_packet_bin(s.ep, id(), stream_id, d, fn);
                    } else {
                        send_packet_osc(s.ep, id(), stream_id, d, fn);
                    }
                }

                count += numframes;

                // lock again
                updatelock.lock();
            } else {
            #if AOO_DEBUG_RESEND
                LOG_DEBUG("AooSource: cannot find block " << r.sequence);
            #endif
            }
        }

        if (count > 0) {
            auto e = make_event<frame_resend_event>(s.ep, count);
            updatelock.unlock();
            send_event(std::move(e), kAooThreadLevelNetwork);
            updatelock.lock();
        }
    }
}

void Source::send_ping(const sendfn& fn){
    // if stream is stopped, the timer won't increment anyway
    auto elapsed = elapsed_time_.load();
    auto pingtime = last_ping_time_.load();
    auto interval = ping_interval_.load(); // 0: no ping
    if (interval > 0 && (elapsed - pingtime) >= interval){
        auto tt = aoo::time_tag::now();
        // send ping to sinks
        sink_lock lock(sinks_);
        for (auto& sink : sinks_){
            if (sink.is_active()){
                // /aoo/sink/<id>/ping <src> <time>
                LOG_DEBUG("AooSource: send " kAooMsgPing " to " << sink.ep);

                char buf[AOO_MAX_PACKET_SIZE];
                osc::OutboundPacketStream msg(buf, sizeof(buf));

                const int32_t max_addr_size = kAooMsgDomainLen
                        + kAooMsgSinkLen + 16 + kAooMsgPingLen;
                char address[max_addr_size];
                snprintf(address, sizeof(address), "%s/%d%s",
                         kAooMsgDomain kAooMsgSink, (int)sink.ep.id, kAooMsgPing);

                msg << osc::BeginMessage(address) << id() << osc::TimeTag(tt)
                    << osc::EndMessage;

                sink.ep.send(msg, fn);
            }
        }

        last_ping_time_.store(elapsed);
    }
}

// /start <id> <version>
void Source::handle_start_request(const osc::ReceivedMessage& msg,
                                  const ip_address& addr)
{
    LOG_DEBUG("AooSource: handle start request");

    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    auto version = (it++)->AsString();

    // LATER handle this in the sink_desc (e.g. not sending data)
    if (auto err = check_version(version); err != kAooOk){
        if (err == kAooErrorVersionNotSupported) {
            LOG_ERROR("AooSource: sink version not supported");
        } else {
            LOG_ERROR("AooSource: bad sink version format");
        }
        return;
    }

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        if (sink->is_active()){
            // just resend /start message
            sink->notify_start();

            notify_start();
        } else {
            LOG_INFO("AooSource: ignoring '" << kAooMsgStart << "' message: sink not active");
        }
    } else {
        LOG_INFO("AooSource: ignoring '" << kAooMsgStart << "' message: sink not found");
    }
}

// /stop <id> <stream>
void Source::handle_stop_request(const osc::ReceivedMessage& msg,
                                 const ip_address& addr)
{
    LOG_DEBUG("AooSource: handle stop request");

    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    auto stream = (it++)->AsInt32();

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink) {
        // A stream can be considered stopped if the source is stopped (idle)
        // and/or the sink is deactivated.
        auto state = stream_state();
        if (state == stream_state::idle || !sink->is_active()){
            // resend /stop message
            sink_request r(request_type::stop, sink->ep);
            r.stop.stream = stream; // use original stream ID!
            r.stop.offset = 0;
            push_request(r);
        } else {
            LOG_INFO("AooSource: ignoring '" << kAooMsgStop << "' message: sink is active");
        }
    } else {
        // TODO: should we still send /stop message?
        LOG_INFO("AooSource: ignoring '" << kAooMsgStop<< "' message: sink not found");
    }
}

// /aoo/src/<id>/data <id> <stream_id> <seq1> <frame1> <seq2> <frame2> etc.

void Source::handle_data_request(const osc::ReceivedMessage& msg,
                                 const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    auto id = (it++)->AsInt32();
    auto stream_id = (it++)->AsInt32(); // we can ignore the stream_id

    if (resend_buffersize_.load() <= 0) {
    #if AOO_DEBUG_RESEND
        LOG_DEBUG("AooSource: ignore data request");
    #endif
        return;
    }

#if AOO_DEBUG_RESEND
    LOG_DEBUG("AooSource: handle data request");
#endif

    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        if (sink->stream_id() != stream_id){
            LOG_INFO("ignoring '" << kAooMsgData
                        << "' message: stream ID mismatch (outdated?)");
            return;
        }
        if (sink->is_active()){
            // get pairs of sequence + frame
            int npairs = (msg.ArgumentCount() - 2) / 2;
            while (npairs--){
                data_request r;
                r.sequence = (it++)->AsInt32();
                r.offset = (it++)->AsInt32(); // -1: whole block
                r.bitset = r.offset >= 0; // only first bit
                sink->push_data_request(r);
            }
        } else {
            LOG_INFO("AooSource: ignoring '" << kAooMsgData << "' message: sink not active");
        }
    } else {
        LOG_INFO("AooSource: ignoring '" << kAooMsgData << "' message: sink not found");
    }
}

// (header), stream_id (int32), count (int32),
// seq1 (int32), offset1 (int16), bitset1 (uint16), ... // offset < 0 -> all

void Source::handle_data_request(const AooByte *msg, int32_t n,
                                 AooId id, const ip_address& addr)
{
    // check size (stream_id, count)
    if (n < 8){
        LOG_ERROR("AooSource: binary data message too small!");
        return;
    }

    if (resend_buffersize_.load() <= 0) {
    #if AOO_DEBUG_RESEND
        LOG_DEBUG("AooSource: ignore data request");
    #endif
        return;
    }

    auto it = msg;
    auto end = it + n;

    auto stream_id = aoo::read_bytes<int32_t>(it);

#if AOO_DEBUG_RESEND
    LOG_DEBUG("AooSource: handle data request");
#endif

    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        if (sink->stream_id() != stream_id){
            LOG_INFO("AooSource: ignore binary data message: stream ID mismatch (outdated?)");
            return;
        }
        if (sink->is_active()){
            // get pairs of sequence + frame
            int count = aoo::read_bytes<int32_t>(it);
            if ((end - it) < (count * sizeof(int32_t) * 2)){
                LOG_ERROR("AooSource: bad 'count' argument for binary data message!");
                return;
            }
            while (count--){
                data_request r;
                r.sequence = aoo::read_bytes<int32_t>(it);
                r.offset = aoo::read_bytes<int16_t>(it);
                r.bitset = aoo::read_bytes<uint16_t>(it);
                sink->push_data_request(r);
            }
        } else {
            LOG_INFO("AooSource: ignore binary data message: sink not active");
        }
    } else {
        LOG_INFO("AooSource: ignore binary data message: sink not found");
    }
}

// /aoo/src/<id>/invite <id> <stream_id> [<metadata_type> <metadata_content>]

void Source::handle_invite(const osc::ReceivedMessage& msg,
                           const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    auto token = (it++)->AsInt32();
    auto metadata = osc_read_metadata(it); // optional

    LOG_DEBUG("AooSource: handle invitation by " << addr << "|" << id);

    event_ptr e1;

    // NB: sinks can be added/removed from different threads,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::unique_lock<sync::mutex> lock1(sink_mutex_);
    sink_lock lock2(sinks_);
    auto sink = find_sink(addr, id);
    if (!sink){
        // the sink is initially deactivated.
        sink = do_add_sink(addr, id, kAooIdInvalid);
        // push "add" event
        e1 = make_event<sink_add_event>(addr, id);
    }
    // make sure that the event is only sent once per invitation.
    if (sink->need_invite(token)) {
        // push "invite" event
        auto e2 = make_event<invite_event>(addr, id, token, metadata);

        lock1.unlock(); // unlock before sending events

        if (e1) {
            send_event(std::move(e1), kAooThreadLevelNetwork);
        }
        send_event(std::move(e2), kAooThreadLevelNetwork);
    } else {
        assert(e1 == nullptr);
        // either the sink resent its /invite message before it received our
        // /start resp. /decline message, or our /decline message has been lost;
        // in the latter case we just let the invitation time out.
        // TODO: figure out a good way to resend /decline messages.
        // NOTE: when we accept an invitation, we essentially start a new stream.
        // Even if our initial /start messages was lost, the subsequent /data messages
        // (with a matching stream ID) would tell the sink that the invitation has been accepted.
        LOG_INFO("AooSource: " << sink->ep << ": ignore redundant/outdated invitation");
    }
}

// /aoo/src/<id>/uninvite <id> <stream_id>

void Source::handle_uninvite(const osc::ReceivedMessage& msg,
                             const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();

    auto token = (it++)->AsInt32();

    LOG_DEBUG("AooSource: handle uninvitation by " << addr << "|" << id);

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink) {
        if (sink->is_active()) {
            if (sink->stream_id() == token) {
                // push "uninvite" event
                if (sink->need_uninvite(token)) {
                    auto e = make_event<uninvite_event>(addr, id, token);
                    send_event(std::move(e), kAooThreadLevelNetwork);
                }
                // let the user handle the uninvitation, don't send a /stop message yet!
                return;
            } else {
                LOG_INFO("AooSource: ignoring '" << kAooMsgUninvite
                          << "' message: stream token mismatch (outdated?)");
                // TODO: should we just return instead?
            }
        } else {
            // if the sink is inactive, it probably means that we have accepted the
            // uninvitation, but the /stop message got lost or hasn't arrived in time.
            LOG_DEBUG("AooSource: ignoring '" << kAooMsgUninvite << "' message: "
                      << " sink not active (/stop message got lost?)");
        }
    } else {
        LOG_INFO("ignoring '" << kAooMsgUninvite << "' message: sink not found");
        // Don't return because we still want to send a /stop message, see below.
    }
    // Tell the remote side that we have stopped. If the sink is NULL, just use
    // the remote address (this does not work if the sink is relayed!)
    sink_request r(request_type::stop, sink ? sink->ep : endpoint(addr, id, binary_.load()));
    r.stop.stream = token; // use remote stream id!
    r.stop.offset = 0;
    push_request(r);
    LOG_DEBUG("AooSource: resend " << kAooMsgStop << " message");
}

// /aoo/src/<id>/ping <id> <tt1>

void Source::handle_ping(const osc::ReceivedMessage& msg,
                         const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    AooId id = (it++)->AsInt32();
    time_tag tt1 = (it++)->AsTimeTag();

    LOG_DEBUG("AooSource: handle ping");

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink) {
        if (sink->is_active()){
            // push pong request
            sink_request r(request_type::pong, sink->ep);
            r.pong.tt1 = tt1;
            r.pong.tt2 = aoo::time_tag::now(); // local receive time
            push_request(r);
        } else {
            LOG_INFO("AooSource: ignoring '" << kAooMsgPing << "' message: sink not active");
        }
    } else {
        LOG_INFO("AooSource: ignoring '" << kAooMsgPing << "' message: sink not found");
    }
}

// /aoo/src/<id>/pong <id> <tt1> <tt2> <packetloss>

void Source::handle_pong(const osc::ReceivedMessage& msg,
                         const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    AooId id = (it++)->AsInt32();
    time_tag tt1 = (it++)->AsTimeTag(); // source send time
    time_tag tt2 = (it++)->AsTimeTag(); // sink receive time
    time_tag tt3 = (it++)->AsTimeTag(); // sink send time
    float packetloss = (msg.ArgumentCount() >= 5) ? (it++)->AsFloat() : 0;

    LOG_DEBUG("AooSource: handle pong");

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink) {
        if (sink->is_active()){
            auto tt4 = aoo::time_tag::now(); // source receive time
            // send ping event
            auto e = make_event<sink_ping_event>(sink->ep, tt1, tt2, tt3, tt4, packetloss);
            send_event(std::move(e), kAooThreadLevelNetwork);
        } else {
            LOG_INFO("AooSource: ignoring '" << kAooMsgPong << "' message: sink not active");
        }
    } else {
        LOG_INFO("AooSource: ignoring '" << kAooMsgPong << "' message: sink not found");
    }
}

} // aoo
