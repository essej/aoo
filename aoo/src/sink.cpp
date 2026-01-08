/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "sink.hpp"

#include <algorithm>
#include <cmath>

// use packet-loss-conceilment for buffering.
// while it reduces audible artifacts, it has the disadvantage that
// buffering is effectively quantized to the format blocksize,
// which might cause higher latency.
// TODO: make this an official compile time option, or even a runtime option.
#ifndef BUFFER_PLC
# define BUFFER_PLC 1
#endif

// Wait until the jitter buffer has a certain number of blocks.
#define BUFFER_BLOCKS 0
// Wait for a certain amount of time.
// NB: with low latencies this has a tendency to get stuck in a cycle
// of overrun and underruns...
#define BUFFER_SAMPLES 1
// Wait for a certain amount of time OR until the jitter buffer has a certain
// number of blocks.
#define BUFFER_BLOCKS_OR_SAMPLES 2
// Wait for a certain amount of time AND until the jitter buffer has a certain
// number of blocks or number of samples.
// NB: with low latencies this has a tendency to get stuck in a cycle
// of overrun and underruns...
#define BUFFER_BLOCKS_AND_SAMPLES 3

// TODO: make this an official compile time option, or even a runtime option?
#ifndef BUFFER_METHOD
# define BUFFER_METHOD BUFFER_BLOCKS_AND_SAMPLES
#endif

namespace aoo {

// OSC data message
const int32_t kDataMaxAddrSize = kAooMsgDomainLen + kAooMsgSourceLen + 16 + kAooMsgDataLen;
// typetag string: 4 bytes
// args: 8 bytes (sink ID + stream ID)
const int32_t kDataHeaderSize = kDataMaxAddrSize + 8;

// binary data message:
// args: 8 bytes (stream ID + count)
const int32_t kBinDataHeaderSize = kAooBinMsgLargeHeaderSize + 8;

} // aoo

//------------------------- Sink ------------------------------//

AOO_API AooSink * AOO_CALL AooSink_new(AooId id) {
    try {
        return aoo::construct<aoo::Sink>(id);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

aoo::Sink::Sink(AooId id)
    : id_(id) {}

AOO_API void AOO_CALL AooSink_free(AooSink *sink) {
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::Sink *>(sink));
}

aoo::Sink::~Sink(){
    // free remaining source requests
    source_request r;
    while (request_queue_.pop(r)) {
        if (r.type == request_type::invite){
            // free metadata
            auto md = r.invite.metadata;
            if (md != nullptr){
                auto size = flat_metadata_size(*md);
                aoo::rt_deallocate(md, size);
            }
        }
    }
}

AOO_API AooError AOO_CALL AooSink_setup(
        AooSink *sink, AooInt32 nchannels, AooSampleRate samplerate,
        AooInt32 blocksize, AooFlag flags) {
    return sink->setup(nchannels, samplerate, blocksize, flags);
}

AooError AOO_CALL aoo::Sink::setup(
        AooInt32 nchannels, AooSampleRate samplerate,
        AooInt32 blocksize, AooFlag flags) {
    if (nchannels >= 0 && samplerate > 0 && blocksize > 0)
    {
        // nchannels_, samplerate_ and blocksize_ are atomic so that
        // concurrent calls to source_desc::update() will never see teared
        // values. Apart from that, there should not be any race conditions
        // since the individual source_descs are protected with a mutex.
        // However, setup() must not be called concurrently with process()!
        if (nchannels != nchannels_ || samplerate != samplerate_ ||
            blocksize != blocksize_ || flags != flags_)
        {
            flags_ = flags;
            nchannels_ = nchannels;
            blocksize_ = blocksize;
            samplerate_ = samplerate;

            realsr_.store(samplerate);

            reset_sources();
        }

        reset_timer(); // always reset!

        return kAooOk;
    } else {
        return kAooErrorBadArgument;
    }
}

namespace aoo {

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

} // aoo

#define CHECKARG(type) assert(size == sizeof(type))

#define GETSOURCEARG \
    source_lock lock(sources_);         \
    auto src = get_source_arg(index);   \
    if (!src) {                         \
        return kAooErrorNotFound;       \
    }                                   \

AOO_API AooError AOO_CALL AooSink_control(
        AooSink *sink, AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    return sink->control(ctl, index, ptr, size);
}

AooError AOO_CALL aoo::Sink::control(
        AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    switch (ctl){
    // id
    case kAooCtlSetId:
    {
        CHECKARG(int32_t);
        auto newid = as<int32_t>(ptr);
        if (id_.exchange(newid) != newid){
            // LATER clear source list here
        }
        break;
    }
    case kAooCtlGetId:
        CHECKARG(AooId);
        as<AooId>(ptr) = id();
        break;
    // reset
    case kAooCtlReset:
    {
        if (index != 0){
            GETSOURCEARG
            src->reset(*this);
        } else {
            reset_sources();
            reset_timer();
        }
        break;
    }
    // get format
    case kAooCtlGetFormat:
    {
        assert(size >= sizeof(AooFormat));
        GETSOURCEARG
        return src->get_format(as<AooFormat>(ptr), size);
    }
    // latency
    case kAooCtlSetLatency:
    {
        CHECKARG(AooSeconds);
        auto bufsize = std::max<AooSeconds>(0, as<AooSeconds>(ptr));
        if (latency_.exchange(bufsize) != bufsize){
            reset_sources();
        }
        break;
    }
    case kAooCtlGetLatency:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = latency_.load();
        break;
    // buffer size
    case kAooCtlSetBufferSize:
    {
        CHECKARG(AooSeconds);
        auto bufsize = std::max<AooSeconds>(0, as<AooSeconds>(ptr));
        if (buffersize_.exchange(bufsize) != bufsize){
            reset_sources();
        }
        break;
    }
    case kAooCtlGetBufferSize:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = buffersize_.load();
        break;
    // get buffer fill ratio
    case kAooCtlGetBufferFillRatio:
    {
        CHECKARG(double);
        GETSOURCEARG
        as<double>(ptr) = src->get_buffer_fill_ratio();
        break;
    }
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
        if (resample_method_.exchange(method) != method) {
            reset_sources();
        }
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
        // only takes effect on subsequent streams
        dynamic_resampling_.store(b);
        reset_timer();
        break;
    }
    case kAooCtlGetDynamicResampling:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = dynamic_resampling_.load();
        break;
    // time DLL filter bandwidth
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
    // real samplerate
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
    // packetsize
    case kAooCtlSetPacketSize:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = kDataHeaderSize + 64;
        auto packetsize = as<int32_t>(ptr);
        if (packetsize < minpacketsize){
            LOG_WARNING("AooSink: packet size too small! setting to " << minpacketsize);
            packet_size_.store(minpacketsize);
        } else if (packetsize > AOO_MAX_PACKET_SIZE){
            LOG_WARNING("AooSink: packet size too large! setting to " << AOO_MAX_PACKET_SIZE);
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
    // resend data
    case kAooCtlSetResendData:
        CHECKARG(AooBool);
        resend_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetResendData:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = resend_.load();
        break;
    // resend interval
    case kAooCtlSetResendInterval:
    {
        CHECKARG(AooSeconds);
        auto interval = std::max<AooSeconds>(0, as<AooSeconds>(ptr));
        resend_interval_.store(interval);
        break;
    }
    case kAooCtlGetResendInterval:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = resend_interval_.load();
        break;
    // resend limit
    case kAooCtlSetResendLimit:
    {
        CHECKARG(int32_t);
        auto limit = std::max<int32_t>(1, as<int32_t>(ptr));
        resend_limit_.store(limit);
        break;
    }
    case kAooCtlGetResendLimit:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_limit_.load();
        break;
    // source timeout
    case kAooCtlSetSourceTimeout:
    {
        CHECKARG(AooSeconds);
        auto timeout = as<AooSeconds>(ptr);
        if (timeout < 0) {
            timeout = kAooInfinite;
        }
        source_timeout_.store(timeout);
        break;
    }
    case kAooCtlGetSourceTimeout:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = source_timeout_.load();
        break;
    case kAooCtlSetInviteTimeout:
    {
        CHECKARG(AooSeconds);
        auto timeout = as<AooSeconds>(ptr);
        if (timeout < 0) {
            timeout = kAooInfinite;
        }
        invite_timeout_.store(timeout);
        break;
    }
    case kAooCtlGetInviteTimeout:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = invite_timeout_.load();
        break;
    case kAooCtlSetBinaryFormat:
        CHECKARG(AooBool);
        binary_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetBinaryFormat:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = binary_.load();
        break;
#if AOO_NET
    case kAooCtlSetClient:
        client_ = reinterpret_cast<AooClient *>(index);
        break;
#endif
    // unknown
    default:
        LOG_WARNING("AooSink: unsupported control " << ctl);
        return kAooErrorNotImplemented;
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSink_codecControl(
        AooSink *sink, const AooChar *codec, AooCtl ctl,
        AooIntPtr index, void *data, AooSize size)
{
    return sink->codecControl(codec, ctl, index, data, size);
}

AooError AOO_CALL aoo::Sink::codecControl(const AooChar *codec,
        AooCtl ctl, AooIntPtr index, void *data, AooSize size) {
    source_lock lock(sources_);
    auto src = get_source_arg(index);
    if (!src) {
        return kAooErrorNotFound;
    }
    return src->codec_control(codec, ctl, data, size);
}

AOO_API AooError AOO_CALL AooSink_handleMessage(
        AooSink *sink, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen) {
    return sink->handleMessage(data, size, address, addrlen);
}

AooError AOO_CALL aoo::Sink::handleMessage(
        const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen)
{
    if (samplerate() == 0){
        return kAooErrorNotInitialized; // not setup yet
    }

    AooMsgType type;
    AooId sinkid;
    AooInt32 onset;
    auto err = aoo_parsePattern(data, size, &type, &sinkid, &onset);
    if (err != kAooOk){
        LOG_WARNING("AooSink: not an AOO message!");
        return kAooErrorBadFormat;
    }

    if (type != kAooMsgTypeSink){
        LOG_WARNING("AooSink: not a sink message!");
        return kAooErrorBadArgument;
    }
    if (sinkid != id()){
        LOG_WARNING("AooSink: wrong sink ID!");
        return kAooErrorBadArgument;
    }

    ip_address addr((const sockaddr *)address, addrlen);

    if (aoo::binmsg_check(data, size)){
        // binary message
        auto cmd = aoo::binmsg_cmd(data, size);
        auto id = aoo::binmsg_from(data, size);
        switch (cmd){
        case kAooBinMsgCmdData:
            return handle_data_message(data + onset, size - onset, id, addr);
        default:
            LOG_WARNING("AooSink: unsupported binary message");
            return kAooErrorNotImplemented;
        }
    } else {
        // OSC message
        try {
            osc::ReceivedPacket packet((const char *)data, size);
            osc::ReceivedMessage msg(packet);

            std::string_view pattern = msg.AddressPattern() + onset;
            if (pattern == kAooMsgStart) {
                return handle_start_message(msg, addr);
            } else if (pattern == kAooMsgStop) {
                return handle_stop_message(msg, addr);
            } else if (pattern == kAooMsgDecline) {
                return handle_decline_message(msg, addr);
            } else if (pattern == kAooMsgData) {
                return handle_data_message(msg, addr);
            } else if (pattern == kAooMsgPing) {
                return handle_ping_message(msg, addr);
            } else if (pattern == kAooMsgPong) {
                return handle_pong_message(msg, addr);
            } else {
                LOG_WARNING("AooSink: unknown message " << pattern);
                return kAooErrorNotImplemented;
            }
        } catch (const osc::Exception& e){
            LOG_ERROR("AooSink: exception in handle_message: " << e.what());
            return kAooErrorBadFormat;
        }
    }
}

AOO_API AooError AOO_CALL AooSink_send(
        AooSink *sink, AooSendFunc fn, void *user){
    return sink->send(fn, user);
}

AooError AOO_CALL aoo::Sink::send(AooSendFunc fn, void *user){
    dispatch_requests();

    sendfn reply(fn, user);

    source_lock lock(sources_);
    for (auto& s : sources_){
        s.send(*this, reply);
    }
    lock.unlock(); // !

    // free unused sources
    if (sources_.reclaim()){
        LOG_DEBUG("AooSink: free stale sources");
    }

    return kAooOk;
}

AOO_API AooError AOO_CALL AooSink_process(
        AooSink *sink, AooSample **data, AooInt32 nsamples, AooNtpTime t,
        AooStreamMessageHandler messageHandler, void *user) {
    return sink->process(data, nsamples, t, messageHandler, user);
}

AooError AOO_CALL aoo::Sink::process(
        AooSample **data, AooInt32 nsamples, AooNtpTime t,
        AooStreamMessageHandler messageHandler, void *user) {
    // check nsamples
    assert(fixed_blocksize() ? nsamples == blocksize_ : nsamples <= blocksize_);
    // Always update timers, even if there are no sources.
    // Do it *before* trying to lock the mutex.
    // (The DLL is only ever touched in this method.)
    if (need_reset_timer_.exchange(false, std::memory_order_relaxed)) {
        // start timer
        LOG_DEBUG("AooSink: start timer");
        start_tt_ = t;
        elapsed_time_.store(0);
        // reset DLL
        auto bw = dll_bandwidth_.load();
        dll_.setup(samplerate(), blocksize(), bw, 0);
        realsr_.store(samplerate());
    } else {
        // advance timer
        auto elapsed = aoo::time_tag::duration(start_tt_, t);
        auto prev_elapsed = elapsed_time_.exchange(elapsed, std::memory_order_relaxed);
        // NB: dynamic resampling requires fixed blocksize!
        bool dynamic_resampling = dynamic_resampling_.load() && (flags_ & kAooFixedBlockSize);
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
                LOG_DEBUG("AooSink: time elapsed: " << elapsed << ", period: "
                          << dll_.period() << ", samplerate: " << dll_.samplerate());
            #endif
                realsr_.store(dll_.samplerate());
            }
        }
    }

    // clear outputs
    if (data) {
        for (int i = 0; i < nchannels(); ++i){
            std::fill(data[i], data[i] + nsamples, 0);
        }
    }
#if 1
    if (sources_.empty() && !sources_.need_reclaim() && request_queue_.empty()) {
        // nothing to process and no need to call send()
        return kAooErrorIdle;
    }
#endif
    bool didsomething = false;

    // NB: we only remove sources in this thread,
    // so we do not need to lock the source mutex!
    source_lock lock(sources_);
    for (auto it = sources_.begin(); it != sources_.end();){
        if (it->process(*this, data, nsamples, t, messageHandler, user)){
            didsomething = true;
        } else if (!it->check_active(*this)){
            LOG_INFO("AooSink: removed inactive source " << it->ep);
            auto e = make_event<source_event>(kAooEventSourceRemove, it->ep);
            send_event(std::move(e), kAooThreadLevelAudio);
            // move source to garbage list (will be freed in send())
            it = sources_.erase(it);
            continue;
        }
        ++it;
    }

    if (didsomething){
    #if AOO_CLIP_OUTPUT
        for (int i = 0; i < nchannels(); ++i){
            auto chn = data[i];
            for (int j = 0; j < nsamples; ++j){
                if (chn[j] > 1.0){
                    chn[j] = 1.0;
                } else if (chn[j] < -1.0){
                    chn[j] = -1.0;
                }
            }
        }
    #endif
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSink_setEventHandler(
        AooSink *sink, AooEventHandler fn, void *user, AooEventMode mode)
{
    return sink->setEventHandler(fn, user, mode);
}

AooError AOO_CALL aoo::Sink::setEventHandler(
        AooEventHandler fn, void *user, AooEventMode mode)
{
    event_handler_ = fn;
    event_context_ = user;
    event_mode_ = mode;
    return kAooOk;
}

AOO_API AooBool AOO_CALL AooSink_eventsAvailable(AooSink *sink){
    return sink->eventsAvailable();
}

AooBool AOO_CALL aoo::Sink::eventsAvailable(){
    return !event_queue_.empty();
}

AOO_API AooError AOO_CALL AooSink_pollEvents(AooSink *sink){
    return sink->pollEvents();
}


AooError AOO_CALL aoo::Sink::pollEvents(){
    event_ptr e;
    while (event_queue_.pop(e)){
        event_handler_(event_context_, &e->cast(), kAooThreadLevelUnknown);
    }
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSink_inviteSource(
        AooSink *sink, const AooEndpoint *source, const AooData *metadata)
{
    if (sink) {
        return sink->inviteSource(*source, metadata);
    } else {
        return kAooErrorBadArgument;
    }
}

AooError AOO_CALL aoo::Sink::inviteSource(
        const AooEndpoint& ep, const AooData *md) {
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);

    AooData *metadata = nullptr;
    if (md) {
        auto size = flat_metadata_size(*md);
        metadata = (AooData *)aoo::rt_allocate(size);
        flat_metadata_copy(*md, *metadata);
    }

    source_request r(request_type::invite, addr, ep.id);
    r.invite.token = get_random_id();
    r.invite.metadata = metadata;
    push_request(r);

    return kAooOk;
}

AOO_API AooError AOO_CALL AooSink_uninviteSource(
        AooSink *sink, const AooEndpoint *source)
{
    if (source){
        return sink->uninviteSource(*source);
    } else {
        return kAooErrorBadArgument;
    }
}

AooError AOO_CALL aoo::Sink::uninviteSource(const AooEndpoint& ep) {
    ip_address addr((const sockaddr *)ep.address, ep.addrlen);
    push_request(source_request { request_type::uninvite, addr, ep.id });
    return kAooOk;
}

AOO_API AooError AOO_CALL AooSink_uninviteAll(AooSink *sink)
{
    return sink->uninviteAll();
}

AooError AOO_CALL aoo::Sink::uninviteAll() {
    push_request(source_request { request_type::uninvite_all });
    return kAooOk;
}

namespace aoo {

void Sink::send_event(event_ptr e, AooThreadLevel level) const {
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

void Sink::dispatch_requests(){
    source_request r;
    while (request_queue_.pop(r)) {
        switch (r.type) {
        case request_type::invite:
        {
            // try to find existing source
            // we might want to invite an existing source,
            // e.g. when it is currently uninviting
            // NB: sources can also be added in the network receive
            // thread - see handle_data() or handle_start() -, so we
            // have to lock the source mutex to avoid the ABA problem.
            sync::scoped_lock<sync::mutex> lock1(source_mutex_);
            source_lock lock2(sources_);
            auto src = find_source(r.address, r.id);
            if (!src){
                src = add_source(r.address, r.id);
            }
            src->invite(*this, r.invite.token, r.invite.metadata);
            break;
        }
        case request_type::uninvite:
        {
            // try to find existing source
            source_lock lock(sources_);
            auto src = find_source(r.address, r.id);
            if (src){
                src->uninvite(*this);
            } else {
                LOG_WARNING("AooSink: can't uninvite - source not found");
            }
            break;
        }
        case request_type::uninvite_all:
        {
            source_lock lock(sources_);
            for (auto& src : sources_){
                src.uninvite(*this);
            }
            break;
        }
        default:
            break;
        }
    }
}

aoo::source_desc * Sink::find_source(const ip_address& addr, AooId id){
    for (auto& src : sources_){
        if (src.match(addr, id)){
            return &src;
        }
    }
    return nullptr;
}

aoo::source_desc * Sink::get_source_arg(intptr_t index){
    auto ep = (const AooEndpoint *)index;
    if (!ep){
        LOG_ERROR("AooSink: missing source argument");
        return nullptr;
    }
    ip_address addr((const sockaddr *)ep->address, ep->addrlen);
    auto src = find_source(addr, ep->id);
    if (!src){
        LOG_ERROR("AooSink: couldn't find source");
    }
    return src;
}

// called with source mutex locked
source_desc * Sink::add_source(const ip_address& addr, AooId id){
    // add new source
#if AOO_NET
    ip_address relay;
    // check if the peer needs to be relayed
    if (client_){
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        AooBool b;
        if (client_->control(kAooCtlNeedRelay,
                             reinterpret_cast<intptr_t>(&ep),
                             &b, sizeof(b)) == kAooOk)
        {
            if (b == kAooTrue){
                LOG_DEBUG("AooSink: source " << addr << " needs to be relayed");
                // get relay address
                client_->control(kAooCtlGetRelayAddress,
                                 reinterpret_cast<intptr_t>(&ep),
                                 &relay, sizeof(relay));
            }
        }
    }
    auto it = sources_.emplace_front(addr, relay, id, binary_.load(), elapsed_time());
#else
    auto it = sources_.emplace_front(addr, id, binary_.load(), elapsed_time());
#endif
    return &(*it);
}

void Sink::reset_sources(){
    source_lock lock(sources_);
    for (auto& src : sources_){
        src.reset(*this);
    }
}

void Sink::handle_xrun(int32_t nsamples) {
    LOG_DEBUG("AooSink: handle xrun (" << nsamples << " samples)");
    auto nblocks = (double)nsamples / (double)blocksize();
    source_lock lock(sources_);
    for (auto& src : sources_){
        src.add_xrun(nblocks);
    }
    // also reset time DLL!
    reset_timer();
}

// /aoo/sink/<id>/start <src> <version> <stream_id> <flags> <lastformat>
// <nchannels> <samplerate> <blocksize> <codec> <options>
// (<metadata_type>) (<metadata_content>) <offset>
AooError Sink::handle_start_message(const osc::ReceivedMessage& msg,
                                    const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    AooId id = (it++)->AsInt32();
    auto version = (it++)->AsString();

    // LATER handle this in the source_desc (e.g. ignoring further messages)
    if (auto err = check_version(version); err != kAooOk){
        if (err == kAooErrorVersionNotSupported) {
            LOG_ERROR("AooSource: source version not supported");
        } else {
            LOG_ERROR("AooSource: bad source version format");
        }
        return err;
    }

    AooId stream_id = (it++)->AsInt32();
    AooId seq_start = (it++)->AsInt32();
    AooId format_id = (it++)->AsInt32();
    if (stream_id < 0 || format_id < 0 || seq_start < 0) {
        LOG_ERROR("AooSink: bad arguments for /start message");
        return kAooErrorBadArgument;
    }

    // get stream format
    AooFormat f;
    f.numChannels = (it++)->AsInt32();
    f.sampleRate = (it++)->AsInt32();
    f.blockSize = (it++)->AsInt32();
    snprintf(f.codecName, sizeof(f.codecName), "%s", (it++)->AsString());
    f.structSize = sizeof(AooFormat);

    const void *ext_data;
    osc::osc_bundle_element_size_t ext_size;
    (it++)->AsBlob(ext_data, ext_size);

    aoo::time_tag tt = (it++)->AsTimeTag();
    auto latency = (it++)->AsInt32();
    auto codec_delay = (it++)->AsInt32();

    auto metadata = osc_read_metadata(it); // optional

    int32_t offset = 0;
    // NB: the <offset> argument has been added in 2.0-pre4
    if (it != msg.ArgumentsEnd()) {
        offset = (it++)->AsInt32();
    }

    if (id < 0){
        LOG_WARNING("AooSink: bad ID for " << kAooMsgStart << " message");
        return kAooErrorBadArgument;
    }
    // try to find existing source
    // NB: sources can also be added in the network send thread,
    // so we have to lock the source mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock1(source_mutex_);
    source_lock lock2(sources_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id);
    }
    return src->handle_start(*this, stream_id, seq_start, format_id, f,
                             (const AooByte *)ext_data, ext_size,
                             tt, latency, codec_delay, metadata, offset);
}

// /aoo/sink/<id>/stop <src> <stream> <last_seq> <offset>
AooError Sink::handle_stop_message(const osc::ReceivedMessage& msg,
                                   const ip_address& addr) {
    auto it = msg.ArgumentsBegin();

    AooId id = (it++)->AsInt32();
    AooId stream = (it++)->AsInt32();
    int32_t last_seq = kAooIdInvalid;
    int32_t offset = 0;
    // NB: the <last_seq> and <offset> arguments have been added in 2.0-test4
    if (msg.ArgumentCount() >= 4) {
        last_seq = (it++)->AsInt32();
        offset = (it++)->AsInt32();
    }

    if (id < 0) {
        LOG_WARNING("AooSink: bad ID for " << kAooMsgStop << " message");
        return kAooErrorBadArgument;
    }
    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_stop(*this, stream, last_seq, offset);
    } else {
        return kAooErrorNotFound;
    }
}

// /aoo/sink/<id>/decline <src> <token>
AooError Sink::handle_decline_message(const osc::ReceivedMessage& msg,
                                      const ip_address& addr) {
    auto it = msg.ArgumentsBegin();

    AooId id = (it++)->AsInt32();
    AooId token = (it++)->AsInt32();

    if (id < 0){
        LOG_WARNING("AooSink: bad ID for " << kAooMsgDecline << " message");
        return kAooErrorBadFormat;
    }
    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_decline(*this, token);
    } else {
        return kAooErrorNotFound;
    }
}

// /aoo/sink/<id>/data <src> <stream_id> <seq> (<tt>) (<sr>)
// <channel_onset> <totalsize> (<msgsize>) (<nframes>) (<frame>) (<data>)

AooError Sink::handle_data_message(const osc::ReceivedMessage& msg,
                                   const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();

    // TODO: sanity checks
    net_packet d;
    d.flags = 0;
    d.stream_id = (it++)->AsInt32();
    d.sequence = (it++)->AsInt32();
    // timestamp
    if (it->IsNil()) {
        d.tt = 0; it++;
    } else {
        d.tt = (it++)->AsTimeTag();
        d.flags |= kAooBinMsgDataTimeStamp;
    }
    // samplerate
    if (it->IsNil()) {
        d.samplerate = 0; it++;
    } else {
        d.samplerate = (it++)->AsDouble();
        d.flags |= kAooBinMsgDataSampleRate;
    }
    d.channel = (it++)->AsInt32();
    d.total_size = (it++)->AsInt32();
    // stream message
    if (it->IsNil()) {
        d.msg_size = 0; it++;
    } else {
        d.msg_size = (it++)->AsInt32();
        d.flags |= kAooBinMsgDataStreamMessage;
    }
    // frames
    if (it->IsNil()) {
        d.num_frames = 1; it++;
        d.frame_index = 0; it++;
    } else {
        d.num_frames = (it++)->AsInt32();
        d.frame_index = (it++)->AsInt32();
        d.flags |= kAooBinMsgDataStreamMessage;
    }
    // data
    if (it->IsNil()) {
        // empty block (xrun)
        d.data = nullptr;
        d.size = 0;
        d.flags |= kAooBinMsgDataXRun;
    } else {
        const void *blobdata;
        osc::osc_bundle_element_size_t blobsize;
        (it++)->AsBlob(blobdata, blobsize);
        d.data = (const AooByte *)blobdata;
        d.size = blobsize;
    }

    return handle_data_packet(d, false, addr, id);
}

// binary data message:
// stream_id (int32), seq (int32), channel (uint8), flags (uint8), size (uint16)
// [total (int32), nframes (int16), frame (int16)], [msgsize (int32)], [sr (float64)],
// [tt (uint64)], data...

AooError Sink::handle_data_message(const AooByte *msg, int32_t n,
                                   AooId id, const ip_address& addr)
{
    AooFlag flags;
    net_packet d;
    auto it = msg;
    auto end = it + n;

    // check basic size (stream_id, seq, channel, flags, size)
    if (n < 12){
        goto wrong_size;
    }
    d.stream_id = aoo::read_bytes<int32_t>(it);
    d.sequence = aoo::read_bytes<int32_t>(it);
    d.channel = aoo::read_bytes<uint8_t>(it);
    d.flags = aoo::read_bytes<uint8_t>(it);
    d.size = aoo::read_bytes<uint16_t>(it);
    if (d.flags & kAooBinMsgDataFrames) {
        if ((end - it) < 8) {
            goto wrong_size;
        }
        d.total_size = aoo::read_bytes<uint32_t>(it);
        d.num_frames = aoo::read_bytes<uint16_t>(it);
        d.frame_index = aoo::read_bytes<uint16_t>(it);
    } else {
        d.total_size = d.size;
        d.num_frames = d.size > 0;
        d.frame_index = 0;
    }
    if (d.flags & kAooBinMsgDataStreamMessage) {
        if ((end - it) < 4) {
            goto wrong_size;
        }
        d.msg_size = aoo::read_bytes<uint32_t>(it);
    } else {
        d.msg_size = 0;
    }
    if (d.flags & kAooBinMsgDataSampleRate) {
        if ((end - it) < 8) {
            goto wrong_size;
        }
        d.samplerate = aoo::read_bytes<double>(it);
    } else {
        d.samplerate = 0;
    }
    if (d.flags & kAooBinMsgDataTimeStamp) {
        d.tt = aoo::read_bytes<uint64_t>(it);
    } else {
        d.tt = 0;
    }
    d.data = it;

    if ((end - it) < d.size) {
        goto wrong_size;
    }

    return handle_data_packet(d, true, addr, id);

wrong_size:
    LOG_ERROR("AooSink: binary data message too small!");
    return kAooErrorBadFormat;
}

AooError Sink::handle_data_packet(net_packet& d, bool binary,
                                  const ip_address& addr, AooId id)
{
    if (id < 0){
        LOG_WARNING("AooSink: bad ID for " << kAooMsgData << " message");
        return kAooErrorBadArgument;
    }
    // try to find existing source
    // NOTE: sources can also be added in the network send thread,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock1(source_mutex_);
    source_lock lock2(sources_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id);
    }
    return src->handle_data(*this, d, binary);
}

AooError Sink::handle_ping_message(const osc::ReceivedMessage& msg,
                                   const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    time_tag tt = (it++)->AsTimeTag();

    LOG_DEBUG("AooSink: handle ping");

    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_ping(*this, tt);
    } else {
        LOG_WARNING("AooSink: couldn't find source " << addr << "|" << id
                    << " for " << kAooMsgPing << " message");
        return kAooErrorNotFound;
    }
}

AooError Sink::handle_pong_message(const osc::ReceivedMessage& msg,
                                   const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    AooId id = (it++)->AsInt32();
    time_tag tt1 = (it++)->AsTimeTag(); // sink send time
    time_tag tt2 = (it++)->AsTimeTag(); // source receive time
    time_tag tt3 = (it++)->AsTimeTag(); // source send time

    LOG_DEBUG("AooSink: handle pong");

    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_pong(*this, tt1, tt2, tt3);
    } else {
        LOG_WARNING("AooSink: couldn't find source " << addr << "|" << id
                    << " for " << kAooMsgPong << " message");
        return kAooErrorNotFound;
    }
}

//----------------------- source_desc --------------------------//

#if AOO_NET
source_desc::source_desc(const ip_address& addr, const ip_address& relay,
                         AooId id, bool binary, double time)
    : ep(addr, relay, id, binary), last_packet_time_(time)
#else
source_desc::source_desc(const ip_address& addr, AooId id,
                         bool binary, double time)
    : ep(addr, id, binary), last_packet_time_(time)
#endif
{
    // resendqueue_.reserve(256);
    event_buffer_.reserve(6); // start, stop, active, inactive, overrun, underrun
    LOG_DEBUG("AooSink: source_desc");
}

source_desc::~source_desc() {
    LOG_DEBUG("AooSink: ~source_desc");
    flush_packet_queue();
    // flush stream message queue
    reset_stream();
}

bool source_desc::check_active(const Sink& s) {
    auto timeout = s.source_timeout();
    if (timeout >= 0) {
        // check source idle timeout
        auto elapsed = s.elapsed_time();
        auto delta = elapsed - last_packet_time_.load(std::memory_order_relaxed);
        if (delta >= timeout) {
            return false; // source timeout
        }
    }
    return true;
}

AooError source_desc::get_format(AooFormat &format, size_t size){
    // synchronize with handle_start() and update()!
    scoped_shared_lock lock(mutex_);
    if (format_) {
        if (size >= format_->structSize){
            memcpy(&format, format_.get(), format_->structSize);
            return kAooOk;
        } else {
            return kAooErrorInsufficientBuffer;
        }
    } else {
        return kAooErrorNotInitialized;
    }
}

AooError source_desc::codec_control(const AooChar *codec, AooCtl ctl,
                                    void *data, AooSize size) {
    // we don't know which controls are setters and which
    // are getters, so we just take a writer lock for either way.
    unique_lock lock(mutex_);
    if (decoder_) {
        if (!strcmp(decoder_->cls->name, codec)) {
            return AooEncoder_control(decoder_.get(), ctl, data, size);
        } else {
            LOG_ERROR("AooSource: tried to pass '" << codec << "' codec option to '"
                      << decoder_->cls->name << "' decoder");
            return kAooErrorBadArgument;
        }
    } else {
        return kAooErrorNotInitialized;
    }
}

void source_desc::reset(const Sink& s){
    // take writer lock!
    scoped_lock lock(mutex_);
    update(s);
}

// always called with writer lock!
void source_desc::update(const Sink& s){
    // resize audio ring buffer
    if (format_ && format_->blockSize > 0 && format_->sampleRate > 0) {
        assert(decoder_ != nullptr);
        // calculate latency
        auto convert = (double)format_->sampleRate / (double)format_->blockSize;
        auto latency = s.latency();
        int32_t latency_blocks = std::ceil(latency * convert);
        // minimum buffer size depends on resampling and reblocking!
        auto resample = (double)s.samplerate() / (double)format_->sampleRate;
        auto reblock = (double)s.blocksize() / (double)format_->blockSize;
        auto min_latency_blocks = std::ceil(reblock / resample);
        latency_blocks_ = std::max<int32_t>(latency_blocks, min_latency_blocks);
        // latency samples are not quantized!
        int32_t min_latency_samples = min_latency_blocks / convert * (double)s.samplerate() + 0.5;
        int32_t latency_samples = latency * (double)s.samplerate() + 0.5;
        latency_samples_ = std::max<int32_t>(latency_samples, min_latency_samples);
        // calculate jitter buffer size
        auto buffersize = s.buffersize();
        if (buffersize <= 0) {
            buffersize = latency * 2; // default
        } else if (buffersize < latency) {
            LOG_INFO("AooSink: buffer size (" << (buffersize * 1000)
                     << " ms) smaller than latency (" << (latency * 1000) << " ms)");
            buffersize = latency;
        }
        auto jitter_buffersize = std::max<int32_t>(latency_blocks_, std::ceil(buffersize * convert));
        LOG_DEBUG("AooSink: latency (ms): " << (latency * 1000) << ", latency blocks: " << latency_blocks_
                  << ", min. blocks: " << min_latency_blocks << ", latency samples: " << latency_samples_
                  << ", min. samples: " << min_latency_samples << ", jitter buffersize (ms): "
                  << (buffersize * 1000) << ", num blocks: " << jitter_buffersize);

        // NB: the actual latency is still numbuffers_ because that is the number
        // of buffers we wait before we start decoding, see try_decode_block().
        auto old_buffer_size = jitter_buffer_.capacity();
        jitter_buffer_.resize(jitter_buffersize);
        if (old_buffer_size && old_buffer_size != jitter_buffersize) {
#if 1
            // Release the frame memory, but only if the size has changed!
            // This makes sure that we can repeatedly start and stop
            // streams without hitting the system memory allocator,
            // preventing fragmentation on memory constrained systems.
            flush_packet_queue(); // !
            frame_allocator_.release_memory();
#endif
        }

        reset_stream();

        // setup resampler
        resampler_.setup(format_->blockSize, s.blocksize(), s.fixed_blocksize(),
                         format_->sampleRate, s.samplerate(), !s.dynamic_resampling(),
                         format_->numChannels, s.resample_method());
        if (resampler_.bypass()) {
            LOG_DEBUG("AooSink: bypass resampler");
        }

        channel_ = 0;
        underrun_ = false;
        stopped_ = false;
        did_update_ = true;
        buffer_latency_ = 0; // force latency event
        dropped_blocks_.store(0);

        // reset decoder to avoid garbage from previous stream
        AooDecoder_reset(decoder_.get());
    }
}

void source_desc::invite(const Sink& s, AooId token, AooData *metadata){
    // always invite, even if we're already running!
    invite_token_.store(token);
    // NOTE: the metadata is only read/set in this thread (= send thread)
    // so we don't have to worry about race conditions!
    invite_metadata_.reset(metadata);

    auto elapsed = s.elapsed_time();
    // reset invite timeout, in case we're not running;
    // otherwise this won't do anything.
    last_packet_time_.store(elapsed);
#if 1
    last_invite_time_.store(0.0); // start immediately
#else
    last_invite_time_.store(elapsed); // wait
#endif
    invite_start_time_.store(elapsed);

    state_.store(source_state::invite);

    LOG_DEBUG("AooSink: source_desc: invite");
}

void source_desc::uninvite(const Sink& s){
    // only uninvite when running!
    // state can change in different threads, so we need a CAS loop
    auto state = state_.load(std::memory_order_acquire);
    while (state == source_state::run){
        // reset uninvite timeout, see handle_data()
        invite_start_time_.store(s.elapsed_time());
        if (state_.compare_exchange_weak(state, source_state::uninvite)){
            LOG_DEBUG("AooSink: source_desc: uninvite");
            return;
        }
    }
    LOG_WARNING("AooSink: couldn't uninvite source - not running");
}

// TODO: make this non-blocking
float source_desc::get_buffer_fill_ratio() {
    scoped_shared_lock lock(mutex_);
    if (decoder_){
        // consider samples in resampler!
        auto resampler_available = (double)resampler_.balance() / (double)format_->blockSize;
        auto available = jitter_buffer_.size() + resampler_available;
        auto ratio = available / (double)jitter_buffer_.capacity();
        LOG_DEBUG("AooSink: fill ratio: " << ratio << ", jitter buffer: "
                  << jitter_buffer_.size() << ", resampler: " << resampler_available);
        return std::min<float>(1.0, ratio);
    } else {
        return 0.0;
    }
}

void source_desc::add_xrun(double nblocks){
    xrunblocks_ += nblocks;
}

AooError source_desc::handle_start(const Sink& s, int32_t stream_id, int32_t seq_start,
                                   int32_t format_id, const AooFormat& f,
                                   const AooByte *ext_data, int32_t ext_size,
                                   aoo::time_tag tt, int32_t latency, int32_t codec_delay,
                                   const std::optional<AooData>& md, int32_t offset) {
    LOG_DEBUG("AooSink: handle start (" << stream_id << ")");
    auto state = state_.load(std::memory_order_acquire);
    if (state == source_state::invite) {
        // ignore /start messages that don't match the desired stream id
        if (stream_id != invite_token_.load()) {
            LOG_DEBUG("AooSink: handle_start: doesn't match invite token");
            return kAooOk;
        }
    }
    // ignore redundant /start messages!
    // NOTE: stream_id_ can only change in this thread,
    // so we don't need a lock to safely *read* it!
    if (stream_id == stream_id_) {
        LOG_DEBUG("AooSink: handle_start: ignore redundant /start message");
        return kAooErrorNone;
    }

    // check if format has changed
    // NOTE: format_id_ is only used in this method, so we don't need a lock!
    const AooCodecInterface *codec = nullptr;
    AooFormatStorage fmt;
    bool format_changed = format_id != format_id_;
    if (format_changed) {
        // look up codec
        codec = aoo::find_codec(f.codecName);
        if (!codec){
            LOG_ERROR("AooSink: codec '" << f.codecName << "' not supported!");
            return kAooErrorNotImplemented;
        }

        // copy format header
        memcpy(&fmt, &f, sizeof(AooFormat));

        // try to deserialize format extension
        fmt.header.structSize = sizeof(AooFormatStorage);
        auto err = codec->deserialize(ext_data, ext_size,
                                      &fmt.header, &fmt.header.structSize);
        if (err != kAooOk){
            return err;
        }
    }

    // copy metadata
    rt_metadata_ptr metadata;
    if (md) {
        assert(md->data && md->size > 0);
        LOG_DEBUG("AooSink: stream metadata: "
                  << md->type << ", " << md->size << " bytes");
        // allocate flat metadata
        auto md_size = flat_metadata_size(*md);
        metadata.reset((AooData *)rt_allocate(md_size));
        flat_metadata_copy(*md, *metadata);
    }

    unique_lock lock(mutex_); // writer lock!

    // NOTE: the stream ID must always be in sync with the format,
    // so we have to set it while holding the lock!
    // Also do this if the format turns out to be invalid, so we don't
    // process the same invalid /start message over and over again.
    bool first_stream = stream_id_ == kAooIdInvalid;
    stream_id_ = stream_id;

    if (format_changed) {
        // create new decoder if necessary
        if (!decoder_ || strcmp(decoder_->cls->name, codec->name)) {
            decoder_.reset(codec->decoderNew());
        }

        // setup decoder - will validate format!
        if (auto err = AooDecoder_setup(decoder_.get(), &fmt.header); err != kAooOk) {
            decoder_ = nullptr;
            format_ = nullptr;
            LOG_ERROR("AooSink: couldn't setup decoder!");
            return err;
        }

        // save validated format
        auto fp = aoo::allocate(fmt.header.structSize);
        memcpy(fp, &fmt, fmt.header.structSize);
        format_.reset((AooFormat *)fp);

        // finally save new format ID
        format_id_ = format_id;
    }
    assert(format_ != nullptr);

    // replace metadata
    metadata_ = std::move(metadata);

    // always update!
    update(s);

    if (format_changed) {
        // Let's release data frames when changing formats because
        // different formats have different memory requirements
        // and allocation patterns. Do this *after* the jitter
        // buffer has been reset in update(), to make sure that
        // all frames have been returned.
        flush_packet_queue(); // !
        frame_allocator_.release_memory();
    }

    // set jitter buffer head
    jitter_buffer_.reset_head(seq_start - 1);

    // cache reblock/resample latency and codec delay
    auto resample = (double)s.samplerate() / (double)format_->sampleRate;
    stream_tt_ = tt;
    source_latency_ = latency * resample;
    source_codec_delay_ = codec_delay * resample;
    sink_latency_ = std::max<int32_t>(0, s.blocksize() - format_->blockSize * resample)
                    + resampler_.latency() * resample;
    AooInt32 arg = 0;
    AooDecoder_control(decoder_.get(), kAooCodecCtlGetLatency, AOO_ARG(arg));
    sink_codec_delay_ = arg * resample;

    // save stream sample offset
    sample_offset_ = offset * resample;

    LOG_DEBUG("AooSink: stream start time: " << stream_tt_ << ", latency: "
              << source_latency_ << ", codec delay: " << source_codec_delay_
              << ", sample offset: " << sample_offset_);

    lock.unlock();

    // send "add" event *before* setting the state to avoid
    // possible wrong ordering with subsequent "start" event
    if (first_stream){
        // first /start message -> source added.
        auto e = make_event<source_event>(kAooEventSourceAdd, ep);
        s.send_event(std::move(e), kAooThreadLevelNetwork);
        LOG_DEBUG("AooSink: add new source " << ep);
    }

    if (format_changed){
        // send "format" event
        auto e = make_event<format_change_event>(ep, fmt.header);
        s.send_event(std::move(e), kAooThreadLevelNetwork);
    }

    state_.store(source_state::start);

    return kAooOk;
}

AooError source_desc::handle_stop(const Sink& s, int32_t stream,
                                  int32_t last_seq, int32_t offset) {
    LOG_DEBUG("AooSink: handle stop (" << stream << ")");
    // ignore redundant /stop messages!
    // NOTE: stream_id_ can only change in this thread,
    // so we don't need a lock to safely *read* it!
    // TODO: handle last_seq and sample offset
    if (stream == stream_id_){
        // check if we're already idle to avoid duplicate "stop" events
        auto state = state_.load(std::memory_order_relaxed);
        while (state != source_state::idle){
            if (state_.compare_exchange_weak(state, source_state::stop)){
                return kAooOk;
            }
        }
        LOG_DEBUG("AooSink: handle_stop: already idle");
    } else {
        LOG_DEBUG("AooSink: handle_stop: ignore redundant /stop message");
    }

    return kAooOk;
}

// /aoo/sink/<id>/decline <src> <token>

AooError source_desc::handle_decline(const Sink& s, int32_t token) {
    LOG_DEBUG("AooSink: handle decline (" << token << ")");
    // ignore /decline messages that don't match the token
    if (token != invite_token_.load()){
        LOG_DEBUG("AooSink: /decline message doesn't match invite token");
        return kAooOk;
    }

    auto expected = source_state::invite;
    if (state_.compare_exchange_strong(expected, source_state::timeout)) {
        auto e = make_event<source_event>(kAooEventInviteDecline, ep);
        s.send_event(std::move(e), kAooThreadLevelNetwork);
    } else {
        LOG_DEBUG("AooSink: received /decline while not inviting");
    }

    return kAooOk;
}

// /aoo/sink/<id>/data <src> <stream_id> <seq> <tt> <sr> <channel_onset>
// <totalsize> <msgsize> <numpackets> <packetnum> <data>

AooError source_desc::handle_data(const Sink& s, net_packet& d, bool binary)
{
    // always update packet time to signify that we're receiving packets
    last_packet_time_.store(s.elapsed_time(), std::memory_order_relaxed);

    auto state = state_.load(std::memory_order_acquire);
    if (state == source_state::invite) {
        // ignore data messages that don't match the desired stream id.
        if (d.stream_id != invite_token_.load()){
            LOG_DEBUG("AooSink: /data message doesn't match invite token");
            return kAooOk;
        }
    } else if (state == source_state::uninvite) {
        // ignore data and send uninvite request. only try for a certain
        // amount of time to avoid spamming the source.
        auto timeout = s.invite_timeout();
        auto delta = s.elapsed_time() - invite_start_time_.load(std::memory_order_relaxed);
        if (timeout >= 0 && delta >= timeout) {
            // transition into 'timeout' state, but only if the state
            // hasn't changed in between.
            if (state_.compare_exchange_strong(state, source_state::timeout)) {
                LOG_DEBUG("AooSink: uninvite -> timeout");
            } else {
                LOG_DEBUG("AooSink: uninvite -> timeout failed");
            }
            // always send timeout event
            LOG_INFO("AooSink: " << ep << ": uninvitation timed out");
            auto e = make_event<source_event>(kAooEventUninviteTimeout, ep);
            s.send_event(std::move(e), kAooThreadLevelNetwork);
        } else {
            // TODO: prevent duplicate /uninvite messages in close succession
            LOG_DEBUG("AooSink: request uninvite (elapsed: " << delta << ")");
            request r(request_type::uninvite);
            r.uninvite.token = d.stream_id;
            push_request(r);
        }
        return kAooOk;
    } else if (state == source_state::timeout) {
        // we keep ignoring any data until we
        // a) invite a new stream (change to 'invite' state)
        // b) receive a new stream
        //
        // (if the user doesn't want to receive anything, they
        // would actually have to *deactivate* the source [TODO])
        if (d.stream_id == stream_id_){
            // LOG_DEBUG("AooSink: handle_data: ignore (invite timeout)");
            return kAooOk;
        }
    } else if (state == source_state::idle) {
        if (d.stream_id == stream_id_) {
            // this can happen when /data messages are reordered after
            // a /stop message.
            LOG_DEBUG("AooSink: received /data message for idle stream!");
        #if 1
            // NOTE: during the 'idle' state no packets are being processed,
            // so incoming data messages would pile up indefinitely.
            return kAooOk;
        #endif
        }
    }

    // the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    // NOTE: stream_id_ can only change in this thread!
    if (d.stream_id != stream_id_) {
        LOG_DEBUG("AooSink: received /data message before /start message");
        push_request(request(request_type::start));
        return kAooOk;
    }

    // synchronize with update()!
    scoped_shared_lock lock(mutex_);

    if (!decoder_) {
        // when we receive a /start message with a bad format, we just ignore
        // all subsequent /data messages for that stream ID. See handle_start().
        LOG_DEBUG("AooSink: ignore /data message");
        return kAooErrorNotInitialized;
    }
    assert(format_ != nullptr);
    // check and fix up samplerate
    if (d.samplerate == 0){
        assert(!(d.flags & kAooBinMsgDataSampleRate));
        // no dynamic resampling, just use nominal samplerate
        d.samplerate = format_->sampleRate;
    }

    // copy blob data and push to queue
    if (d.size > 0) {
        auto frame = frame_allocator_.allocate(d.size);
        memcpy(frame->data, d.data, d.size);
        d.frame = frame;
    } else {
        d.frame = nullptr;
    }

    packet_queue_.push(d);

#if AOO_DEBUG_DATA
    LOG_DEBUG("AooSink: got block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << d.channel << ", msgsize = " << d.msgsize
              << ", totalsize = " << d.totalsize << ", nframes = " << d.nframes
              << ", frame = " << d.frame_index << ", size " << d.size);
#endif

    return kAooOk;
}

// /aoo/sink/<id>/ping <src> <time>

AooError source_desc::handle_ping(const Sink& s, time_tag tt){
    LOG_DEBUG("AooSink: handle ping");

#if 1
    // only handle pings if active
    auto state = state_.load(std::memory_order_acquire);
    if (!(state == source_state::start || state == source_state::run)){
        return kAooOk;
    }
#endif

    // push pong request
    request r(request_type::pong);
    r.pong.tt1 = tt;
    r.pong.tt2 = time_tag::now();
    push_request(r);

    return kAooOk;
}


// /aoo/sink/<id>/pong <src> <tt1> <tt2> <tt3>

AooError source_desc::handle_pong(const Sink& s, time_tag tt1,
                                  time_tag tt2, time_tag tt3) {
    LOG_DEBUG("AooSink: handle ping");

#if 1
    // only handle pongs if active
    auto state = state_.load(std::memory_order_acquire);
    if (!(state == source_state::start || state == source_state::run)) {
        return kAooOk;
    }
#endif

    time_tag tt4 = aoo::time_tag::now(); // local receive time

    // send ping event
    auto e = make_event<source_ping_event>(ep, tt1, tt2, tt3, tt4);
    s.send_event(std::move(e), kAooThreadLevelNetwork);

    return kAooOk;
}

void send_uninvitation(const Sink& s, const endpoint& ep, AooId token, const sendfn &fn);

void source_desc::send(const Sink& s, const sendfn& fn){
    // handle requests
    request r;
    while (request_queue_.pop(r)){
        switch (r.type){
        case request_type::pong:
            send_pong(s, r.pong.tt1, r.pong.tt2, fn);
            break;
        case request_type::start:
            send_start_request(s, fn);
            break;
        case request_type::stop:
            send_stop_request(s, r.stop.stream, fn);
            break;
        case request_type::uninvite:
            send_uninvitation(s, ep, r.uninvite.token, fn);
            break;
        default:
            break;
        }
    }

    send_ping(s, fn);

    send_invitations(s, fn);

    send_data_requests(s, fn);
}

#define XRUN_THRESHOLD 0.1
#define STOP_INTERVAL 1.0

bool source_desc::process(const Sink& s, AooSample **buffer, int32_t nsamples,
                          time_tag tt, AooStreamMessageHandler handler, void *user)
{
    // synchronize with update()!
    // the mutex should be uncontended most of the time.
    shared_lock lock(mutex_, sync::try_to_lock);
    if (!lock.owns_lock()) {
        if (stream_state_ != stream_state::inactive) {
            add_xrun(1);
            LOG_DEBUG("AooSink: process would block");
        }
        // how to report this to the client?
        return false;
    }

    if (!decoder_){
        return false;
    }
    assert(format_ != nullptr);

    // store events in buffer and only dispatch at the very end,
    // after we release the lock!
    assert(event_buffer_.empty());

    auto state = state_.load(std::memory_order_acquire);
    // handle state transitions in a CAS loop
    while (state != source_state::run) {
        if (state == source_state::start){
            // start -> run
            if (state_.compare_exchange_weak(state, source_state::run)) {
                LOG_DEBUG("AooSink: start -> run");
            #if 1
                if (stream_state_ != stream_state::inactive) {
                    // send missing /stop message
                    auto e = make_event<stream_stop_event>(ep);
                    event_buffer_.push_back(std::move(e));
                }
            #endif

                // *move* metadata into event
                auto e1 = make_event<stream_start_event>(ep, stream_tt_, metadata_.release());
                event_buffer_.push_back(std::move(e1));

                if (stream_state_ != stream_state::buffering) {
                    stream_state_ = stream_state::buffering;
                    LOG_DEBUG("AooSink: stream buffering");
                    auto e2 = make_event<stream_state_event>(ep, kAooStreamStateBuffering, 0);
                    event_buffer_.push_back(std::move(e2));
                }

                break; // continue processing
            }
        } else if (state == source_state::stop){
            // stop -> run
            if (state_.compare_exchange_weak(state, source_state::run)) {
                LOG_DEBUG("AooSink: stop -> run");
                // wait until the buffer has run out!
                stopped_ = true;

                break; // continue processing
            }
        } else if (state == source_state::uninvite) {
            // NB: we keep processing data during the 'uninvite' state to flush
            // the packet buffer. If we receive a /stop message (= the sink has
            // accepted our uninvitation), we will enter the 'idle' state.
            // Otherwise it will eventually enter the 'timeout' state.
            break;
        } else {
            // invite, timeout or idle
            if (stream_state_ != stream_state::inactive) {
                // deactivate stream immediately
                stream_state_ = stream_state::inactive;

                lock.unlock(); // unlock before sending event!

                auto e = make_event<stream_state_event>(ep, kAooStreamStateInactive, 0);
                s.send_event(std::move(e), kAooThreadLevelAudio);
            }
            return false;
        }
    }

    if (did_update_) {
        did_update_ = false;

        xrunblocks_ = 0; // must do here!

        if (stream_state_ != stream_state::buffering) {
            stream_state_ = stream_state::buffering;
            LOG_DEBUG("AooSink: stream buffering");
            auto e = make_event<stream_state_event>(ep, kAooStreamStateBuffering, 0);
            queue_event(std::move(e));
        }
    }

    stream_stats stats;

    if (!packet_queue_.empty()) {
        // check for buffer underrun (only if packets arrive!)
        if (underrun_){
            handle_underrun(s);
        }

        packet_queue_.consume_all([&](const auto& packet) {
            add_packet(s, packet, stats);
        });
    }

    check_missing_blocks(s);

#if AOO_DEBUG_JITTER_BUFFER
    LOG_DEBUG(jitterbuffer_);
    LOG_DEBUG("oldest: " << jitterbuffer_.last_popped()
            << ", newest: " << jitterbuffer_.last_pushed());
    get_buffer_fill_ratio();
#endif

    auto nchannels = format_->numChannels;
    auto outsize = nsamples * nchannels;
    assert(outsize > 0);
    // allocate a temporary buffer for reading from the resampler
    // or directly decoding block data.
    auto buf = (AooSample *)alloca(outsize * sizeof(AooSample));
#if AOO_DEBUG_STREAM_MESSAGE && 0
    LOG_DEBUG("AooSink: process samples: " << process_samples_
              << ", stream samples: " << stream_samples_
              << ", diff: " << (stream_samples_ - (double)process_samples_)
              << ", resampler size: " << resampler_.size());
#endif
    if (resampler_.bypass()) {
        // bypass the resampler;
        // try_decode_block() writes directly into the stack buffer.
        for (;;) {
            if (!try_decode_block(s, buf, stats)) {
                on_underrun(s);

                lock.unlock(); // unlock before sending event!

                flush_events(s);

                return false;
            }

            // if there have been xruns, skip one block of audio and try again.
            if (xrunblocks_ > XRUN_THRESHOLD){
                LOG_DEBUG("AooSink: skip process block for xrun");
                // decrement xrun counter and advance process time
                xrunblocks_ -= 1.0;
                process_samples_ += nsamples;
            } else {
                // got samples
                break;
            }
        }
    } else {
        // resample/reblock;
        // try to read from the resampler and only decode blocks if necessary.
        for (;;) {
            if (resampler_.read(buf, nsamples)){
                // if there have been xruns, skip one block of audio and try again.
                if (xrunblocks_ > XRUN_THRESHOLD){
                    LOG_DEBUG("AooSink: skip process block for xrun");
                    // decrement xrun counter and advance process time
                    xrunblocks_ -= 1.0;
                    process_samples_ += nsamples;
                } else {
                    // got samples
                    break;
                }
            } else if (!try_decode_block(s, nullptr, stats)) {
                on_underrun(s);

                lock.unlock(); // unlock before sending event!

                flush_events(s);

                return false;
            }
        }
    }

    // Capture stream time on first block after reset.
    // This is used to calculate the local time for the AooEventStreamTime event.
    // Note that the source stream time stamp is calculated based on sample time,
    // so we want to do the same with the local stream time.
    if (process_samples_ == 0) {
        local_tt_ = tt;
        LOG_DEBUG("AooSink: local time: " << tt);
    }

    // send stream state event with correct sample offset.
    // we make sure that the offset lies within [0, nsamples-1]
    if (stream_start_ > 0) {
        auto offset = stream_start_ - process_samples_;
        if (offset < nsamples) {
            if (offset < 0) {
                LOG_ERROR("AooSink: negative stream offset: " << offset);
                offset = 0;
            }

            LOG_DEBUG("AooSink: stream active (offset: " << offset << ")");
            auto e = make_event<stream_state_event>(ep, kAooStreamStateActive, offset);
            queue_event(std::move(e));

            stream_start_ = 0;
        }
    }

    dispatch_stream_messages(s, nsamples, handler, user);

    // sum source into sink (interleaved -> non-interleaved),
    // starting at the desired sink channel offset.
    // out-of-bound source channels are silently ignored.
    if (buffer) {
        auto realnchannels = s.nchannels();
        for (int i = 0; i < nchannels; ++i){
            auto chn = i + channel_;
            if (chn < realnchannels){
                auto out = buffer[chn];
                for (int j = 0; j < nsamples; ++j){
                    out[j] += buf[j * nchannels + i];
                }
            }
        }
    }

    // send events
    lock.unlock();

    flush_events(s);

    if (stats.dropped > 0){
        // add to dropped blocks for packet loss reporting
        dropped_blocks_.fetch_add(stats.dropped, std::memory_order_relaxed);
        // push block dropped event
        auto e = make_event<block_drop_event>(ep, stats.dropped);
        s.send_event(std::move(e), kAooThreadLevelAudio);
    }
    if (stats.resent > 0){
        // push block resent event
        auto e = make_event<block_resend_event>(ep, stats.resent);
        s.send_event(std::move(e), kAooThreadLevelAudio);
    }
    if (stats.xrun > 0){
        // push block xrun event
        auto e = make_event<block_xrun_event>(ep, stats.xrun);
        s.send_event(std::move(e), kAooThreadLevelAudio);
    }

    return true;
}

void source_desc::check_latency(const Sink& s) {
    auto buffer_latency = stream_samples_;
    if (buffer_latency != buffer_latency_) {
        buffer_latency_ = buffer_latency;

        // calculate and report latencies
        auto to_seconds = 1. / (double)s.samplerate();
        // the latencies and codec delays are already resampled, see handle_start()
        auto source_latency = source_latency_ + source_codec_delay_;
        auto source_latency_sec = source_latency * to_seconds;
        auto sink_latency = sink_latency_ + sink_codec_delay_;
        auto sink_latency_sec = sink_latency * to_seconds;
        // the stream samples are already resampled, see try_decode_block()
        auto buffer_latency_sec = buffer_latency * to_seconds;

    #if 1
        LOG_DEBUG("\tsource latency: " << (source_latency_sec * 1000)
                  << " ms, " << source_latency << " samples");
        LOG_DEBUG("\tsink latency: " << (sink_latency_sec * 1000)
                  << " ms, " << sink_latency << " samples");
        LOG_DEBUG("\tbuffer latency: " << (buffer_latency_sec * 1000)
                  << " ms, " << buffer_latency<< " samples");
    #endif

        auto e = make_event<stream_latency_event>(ep, source_latency_sec,
                                                  sink_latency_sec,
                                                  buffer_latency_sec);
        queue_event(std::move(e));
    }
}

void source_desc::on_underrun(const Sink &s) {
    // buffer ran out -> "inactive"
    // TODO: delay "inactive" and "stop" event by codec delay (if non-zero)
    // and read remaining data from decoder.
    if (stream_state_ != stream_state::inactive) {
        stream_state_ = stream_state::inactive;
        LOG_DEBUG("AooSink: stream inactive");
        // TODO: read out partial data from resampler and set sample offset
        auto e = make_event<stream_state_event>(ep, kAooStreamStateInactive, 0);
        queue_event(std::move(e));
    }

    if (stopped_) {
        // we received a /stop message
        auto e = make_event<stream_stop_event>(ep);
        queue_event(std::move(e));

        // try to change source state to idle (if still running!)
        auto expected = source_state::run;
        state_.compare_exchange_strong(expected, source_state::idle);
        LOG_DEBUG("AooSink: run -> idle");
    }

    underrun_ = true;
    // check if we should send a stop request
    if (last_stop_time_ > 0) {
        auto now = s.elapsed_time();
        auto delta = now - last_stop_time_;
        if (delta >= STOP_INTERVAL) {
            request r(request_type::stop);
            r.stop.stream = stream_id_;
            push_request(r);
            last_stop_time_ = now;
        }
    } else {
        // initialize stop request timer.
        // underruns are often temporary and may happen in short succession,
        // so we don't want to immediately send a stop request; instead we
        // we wait one interval as a simple debouncing mechanism.
        last_stop_time_ = s.elapsed_time();
    }
}

void source_desc::handle_underrun(const Sink& s){
    LOG_INFO("AooSink: jitter buffer underrun!");

    if (!jitter_buffer_.empty()) {
        LOG_ERROR("AooSink: bug: jitter buffer not empty");
    }
    // always reset buffer! otherwise add_packet() might try to fill
    // the difference to the last received block with empty blocks!
    jitter_buffer_.reset();

#if 1
    // TODO: maybe not necessary with BUFFER_PLC?
    resampler_.reset(); // !
#endif

    reset_stream();

#if !BUFFER_PLC
    // reset decoder when not using PLC!
    AooDecoder_reset(decoder_.get());
#endif

    {
        auto e = make_event<source_event>(kAooEventBufferUnderrun, ep);
        queue_event(std::move(e));
    }

    if (stream_state_ != stream_state::buffering) {
        stream_state_ = stream_state::buffering;
        LOG_DEBUG("AooSink: stream buffering");
        auto e = make_event<stream_state_event>(ep, kAooStreamStateBuffering, 0);
        queue_event(std::move(e));
    }

    underrun_ = false;
}

void source_desc::handle_overrun(const Sink& s){
    LOG_INFO("AooSink: jitter buffer overrun!");

    jitter_buffer_.reset();

#if 1
    // TODO: maybe not necessary with BUFFER_PLC?
    resampler_.reset(); // !
#endif

    reset_stream();

#if !BUFFER_PLC
    // reset decoder when not using PLC!
    AooDecoder_reset(decoder_.get());
#endif

    {
        auto e = make_event<source_event>(kAooEventBufferOverrun, ep);
        queue_event(std::move(e));
    }

    if (stream_state_ != stream_state::buffering) {
        stream_state_ = stream_state::buffering;
        LOG_DEBUG("AooSink: stream buffering");
        auto e = make_event<stream_state_event>(ep, kAooStreamStateBuffering, 0);
        queue_event(std::move(e));
    }
}

bool source_desc::add_packet(const Sink& s, const net_packet& d,
                             stream_stats& stats) {
    scope_guard guard([&]() noexcept {
        if (d.frame) {
            frame_allocator_.deallocate(d.frame);
        }
    });
    // we have to check the stream_id (again) because the stream
    // might have changed in between!
    if (d.stream_id != stream_id_) {
        LOG_DEBUG("AooSink: ignore data packet from previous stream");
        return false;
    }

    if (d.sequence <= jitter_buffer_.last_popped()) {
        // try to detect wrap around
        if ((jitter_buffer_.last_popped() - d.sequence) >= (INT32_MAX / 2)) {
            LOG_INFO("AooSink: stream sequence has wrapped around!");
            jitter_buffer_.reset();
            // continue!
        } else {
            // block too old, discard!
            LOG_INFO("AooSink: discard old block " << d.sequence);
            LOG_DEBUG("AooSink: oldest: " << jitter_buffer_.last_popped());
            return false;
        }
    }

    auto newest = jitter_buffer_.last_pushed();

    auto block = jitter_buffer_.find(d.sequence);
    if (!block) {
        // add new block
    #if 1
        // can this ever happen!?
        if (d.sequence <= newest){
            LOG_INFO("AooSink: discard outdated block " << d.sequence);
            LOG_DEBUG("AooSink: newest: " << newest);
            return false;
        }
    #endif
        if (newest != jitter_buffer::sentinel) {
            auto numblocks = d.sequence - newest;

            // notify for gap
            if (numblocks > 1) {
                LOG_INFO("AooSink: skipped " << (numblocks - 1) << " blocks");
            }

            // check for jitter buffer overrun.
            // can happen if the latency resp. buffer size is too small.
            auto space = jitter_buffer_.capacity() - jitter_buffer_.size();
            if (numblocks > space){
            #if 1
                handle_overrun(s);
                // only keep most recent packet
                newest = d.sequence - 1;
            #else
                LOG_INFO("AooSink: jitter buffer overrun!");
                // reset the buffer to latency_blocks_, considering both the stored blocks
                // and the incoming block(s).
                // TODO: how does this affect the stream latency?
                auto excess_blocks = numblocks + jitter_buffer_.size() - latency_blocks_;
                assert(excess_blocks > 0);
                // *first* discard stored blocks
                auto discard_stored = std::min(jitter_buffer_.size(), excess_blocks);
                if (discard_stored > 0) {
                    LOG_DEBUG("AooSink: discard " << discard_stored << " of "
                              << jitter_buffer_.size() << " stored blocks");
                    // TODO: consider popping blocks from the back of the queue
                    for (int32_t i = 0; i < discard_stored; ++i) {
                        jitter_buffer_.pop();
                    }
                }
                // then discard incoming blocks (except for the most recent one)
                auto discard_incoming = std::min(numblocks - 1, excess_blocks - discard_stored);
                if (discard_incoming > 0) {
                    LOG_DEBUG("AooSink: discard " << discard_incoming << " incoming blocks");
                    newest += discard_incoming;
                    jitter_buffer_.reset_head(); // !
                }
              #if 0
                // TODO: should we report these as dropped blocks? or only the incomplete ones?
                stats.dropped += discard_stored + discard_incoming;
              #endif
                // finally send event
                auto e = make_event<source_event>(kAooEventBufferOverrun, ep);
                queue_event(std::move(e));
            #endif
            }

            // fill gaps with placeholder blocks
            for (int32_t i = newest + 1; i < d.sequence; ++i) {
                jitter_buffer_.push(i)->init(i);
            }
        }

        // add new block
        block = jitter_buffer_.push(d.sequence);

        block->init(d);
    } else {
        if (block->placeholder()){
            block->init(d);
        } else if (block->empty()) {
            // empty block already received
            LOG_INFO("AooSink: empty block " << d.sequence << " already received");
            return false;
        } else if (block->has_frame(d.frame_index)){
            // frame already received
            LOG_INFO("AooSink: frame " << d.frame_index << " of block " << d.sequence << " already received");
            return false;
        }

        if (d.sequence != newest){
            // out of order or resent
            if (block->resend_count() > 0){
                LOG_DEBUG("AooSink: resent frame " << d.frame_index << " of block " << d.sequence);
                // only record first resent frame!
                if (block->set_resent()) {
                    stats.resent++;
                }
            } else {
                LOG_DEBUG("AooSink: frame " << d.frame_index << " of block " << d.sequence << " out of order!");
            }
        }
    }

    guard.dismiss(); // !

    // add frame to block (if not empty)
    if (d.size > 0) {
        block->add_frame(d.frame_index, d.frame);
    }

    return true;
}

// try to decode a block, write audio data into resampler, push any
// stream messages into the priority queue and advances the stream time.
// This method also handles buffering.
bool source_desc::try_decode_block(const Sink& s, AooSample* buffer, stream_stats& stats){
    // first handle buffering.
    if (stream_state_ == stream_state::buffering) {
        // if stopped during buffering, just fake a buffer underrun.
        if (stopped_) {
            LOG_DEBUG("AooSink: stopped during buffering");
            return false;
        }
        // (We take either the process samples or stream samples, depending on
        // which has the smaller granularity)
        auto elapsed = std::min<int32_t>(process_samples_, stream_samples_ + 0.5);
    #if BUFFER_METHOD == BUFFER_BLOCKS
        if (jitterbuffer_.size() < latency_blocks_) {
    #elif BUFFER_METHOD == BUFFER_SAMPLES
        if (elapsed < latency_samples_) {
    #elif BUFFER_METHOD == BUFFER_BLOCKS_OR_SAMPLES
        if ((elapsed < latency_samples_) && (jitterbuffer_.size() < latency_blocks_)) {
    #elif BUFFER_METHOD == BUFFER_BLOCKS_AND_SAMPLES
        if ((elapsed < latency_samples_) || (jitter_buffer_.size() < latency_blocks_)) {
    #else
        #error "unknown buffer method"
    #endif
            // HACK: stop buffering after waiting too long; this is for the case where
            // where the source stops sending data while still buffering, but we don't
            // receive a /stop message (and thus never would become 'inactive').
            if (elapsed > latency_samples_ * 4) {
                LOG_INFO("AooSink: abort buffering after " << elapsed << " samples");
                return false;
            }

            LOG_DEBUG("AooSink: buffering (" << jitter_buffer_.size() << " / " << latency_blocks_
                      << " blocks, " << elapsed << " / " << latency_samples_ << " samples elapsed)");

            // use nominal sample rate
            if (!resampler_.fixed_sr()) {
                resampler_.update(format_->sampleRate, s.samplerate());
            }

            double resample = (double)s.samplerate() / (double)format_->sampleRate;
        #if BUFFER_PLC
            // use format blocksize
            auto framesize = format_->blockSize;
            auto bufsize = framesize * format_->numChannels;
            if (!resampler_.bypass()) {
                assert(buffer == nullptr);
                buffer = (AooSample *)alloca(bufsize * sizeof(AooSample));
            }
            // use packet loss concealment
            AooInt32 count = framesize;
            if (AooDecoder_decode(decoder_.get(), nullptr, 0, buffer, &count) != kAooOk) {
                LOG_WARNING("AooSink: couldn't decode block!");
                // fill with zeros
                std::fill(buffer, buffer + bufsize, 0);
            }
            assert(count == framesize);
            // advance stream time! use nominal sample rate.
            stream_samples_ += (double)framesize * resample;
        #else
            // use process blocksize for more fine-grained latency!
            int32_t framesize;
            int32_t bufsize;
            int32_t advance = s.blocksize();
            if (resampler_.bypass()) {
                assert(buffer != nullptr && format_->blockSize == s.blocksize());
                framesize = advance;
                bufsize = framesize * format_->numChannels;
            } else {
                assert(buffer == nullptr);
                framesize = (double)advance / resample + 0.5;
                bufsize = framesize * format_->numChannels;
                buffer = (AooSample *)alloca(bufsize * sizeof(AooSample));
            }
            std::fill(buffer, buffer + bufsize, 0);
            // advance stream time! (use nominal sample rate)
            stream_samples_ += advance;
        #endif

            if (resampler_.bypass()) {
                return true;
            } else {
                if (resampler_.write(buffer, framesize)) {
                    return true;
                } else {
                    LOG_ERROR("AooSink: bug: couldn't write to resampler");
                    // let the buffer run out
                    return false;
                }
            }
        } else {
            LOG_DEBUG("AooSink: buffering done (" << jitter_buffer_.size() << " / " << latency_blocks_
                      << " blocks, " << elapsed << " / " << latency_samples_ << " samples elapsed)");
            // buffering -> active
            stream_state_ = stream_state::active;
            stream_start_ = stream_samples_ + source_codec_delay_ + sink_codec_delay_ + sample_offset_;

            check_latency(s);
        }
    }

    if (jitter_buffer_.empty()) {
    #if 0
        LOG_DEBUG("AooSink: jitter buffer empty");
    #endif
        // buffer empty -> underrun
        return false;
    }

    const AooByte *data;
    int32_t size;
    int32_t msgsize;
    double sr;

    auto& b = jitter_buffer_.front();
    if (b.complete()){
        // block is ready
        if (b.flags & kAooBinMsgDataXRun) {
            stats.xrun++;
        }
        size = b.total_size;
        if (size > 0) {
            auto buffer = (AooByte*)alloca(size);
            b.copy_frames(buffer);
            data = buffer;
            msgsize = b.message_size;
        } else {
            // empty block
            data = nullptr;
            msgsize = 0;
        }
        sr = b.samplerate;
        channel_ = b.channel; // set current channel
    #if AOO_DEBUG_JITTER_BUFFER
        LOG_DEBUG("jitter buffer: write samples for block ("
                  << b.sequence << ")");
    #endif
    } else {
        // we need audio, so we have to drop a block
        data = nullptr;
        size = msgsize = 0;
        sr = format_->sampleRate; // nominal samplerate
        // keep current channel
        stats.dropped++;
        LOG_INFO("AooSink: dropped block " << b.sequence);
        LOG_DEBUG("AooSink: remaining blocks: " << jitter_buffer_.size() - 1);
    }

    // decode and push audio data to resampler
    auto framesize = format_->blockSize;
    auto bufsize = framesize * format_->numChannels;
    AooInt32 count = framesize;
    if (!resampler_.bypass()) {
        assert(buffer == nullptr);
        buffer = (AooSample *)alloca(bufsize * sizeof(AooSample));
    }

    if (AooDecoder_decode(decoder_.get(), data + msgsize, size - msgsize, buffer, &count) != kAooOk) {
        LOG_WARNING("AooSink: couldn't decode block!");
        // decoder failed - fill with zeros
        std::fill(buffer, buffer + bufsize, 0);
    }
    assert(count == framesize);

    if (!resampler_.bypass()) {
        if (resampler_.write(buffer, framesize)) {
            if (!resampler_.fixed_sr()) {
                // update resampler
                // real_samplerate() is our real samplerate, sr is the real source samplerate.
                // This will dynamically adjust the resampler so that it corrects the clock
                // difference between source and sink.
                // NB: we only do this if dynamic resampling is enabled here on this sink,
                // regardless whether the source sends its real samplerate or not!
                resampler_.update(sr, s.real_samplerate());
            }
        } else {
            LOG_ERROR("AooSink: bug: couldn't write to resampler");
            // let the buffer run out
            jitter_buffer_.pop(); // !
            return false;
        }
    }

    // schedule stream time message  (if present)
    if (b.tt > 0) {
        // NB: offset time by codec delay(s)!
        auto time = stream_samples_ + source_codec_delay_ + sink_codec_delay_;
        auto alloc_size = sizeof(stream_message_header) + sizeof(AooNtpTime);
        auto msg = (stream_time_message *)aoo::rt_allocate(alloc_size);
        msg->header.next = nullptr;
        msg->header.time = time;
        msg->header.channel = 0;
        msg->header.type = kAooDataStreamTime;
        msg->header.size = sizeof(AooNtpTime);
        msg->tt = b.tt;
    #if AOO_DEBUG_STREAM_MESSAGE
        LOG_DEBUG("AooSink: schedule stream time message (tt: "
                  << aoo::time_tag(b.tt) << ", stream samples: "
                  << (int64_t)time << ")");
    #endif
        sched_stream_message(&msg->header);
    }

    double resample = (double)s.samplerate() / sr;

    // schedule stream messages
    if (msgsize > 0) {
        int32_t num_messages = aoo::from_bytes<int32_t>(data);
        auto msgptr = data + 4;
        auto endptr = data + msgsize;
        for (int32_t i = 0; i < num_messages; ++i) {
            auto offset = aoo::read_bytes<uint16_t>(msgptr);
            auto channel = aoo::read_bytes<uint16_t>(msgptr);
            auto type = aoo::read_bytes<uint16_t>(msgptr);
            auto size = aoo::read_bytes<uint16_t>(msgptr);
            auto aligned_size = (size + 3) & ~3; // aligned to 4 bytes
            if ((endptr - msgptr) < aligned_size) {
                LOG_ERROR("AooSink: stream message with bad size argument");
                break;
            }
            auto time = stream_samples_ + offset * resample;
            auto alloc_size = sizeof(stream_message_header) + size;
            auto msg = (flat_stream_message *)aoo::rt_allocate(alloc_size);
            msg->header.next = nullptr;
            msg->header.time = time;
            msg->header.channel = channel;
            msg->header.type = type;
            msg->header.size = size;
        #if AOO_DEBUG_STREAM_MESSAGE
            LOG_DEBUG("AooSink: schedule stream message "
                      << "(type: " << aoo_dataTypeToString(type)
                      << ", channel: " << channel << ", size: " << size
                      << ", offset: " << offset << ", sample time: "
                      << (int64_t)time << ")");
        #endif
            memcpy(msg->data, msgptr, size);

            sched_stream_message(&msg->header);

            msgptr += aligned_size;
        }
    }

    stream_samples_ += (double)framesize * resample;

    jitter_buffer_.pop();

    return true;
}

// /aoo/src/<id>/data <sink> <stream_id> <seq0> <frame0> <seq1> <frame1> ...

// deal with "holes" in block queue
void source_desc::check_missing_blocks(const Sink& s){
    // only check if it has more than a single pending block!
    if (jitter_buffer_.size() <= 1 || !s.resend_enabled()){
        return;
    }
    int32_t resent = 0;
    int32_t limit = s.resend_limit();
    double interval = s.resend_interval();
    double elapsed = s.elapsed_time();

    // resend incomplete blocks except for the last block
    auto n = jitter_buffer_.size() - 1;
    for (auto b = jitter_buffer_.begin(); n--; ++b){
        if (!b->complete() && b->update(elapsed, interval)) {
            if (b->received_frames > 0) {
                // a) only some frames missing
                // we use a frame offset + bitset to indicate which frames are missing
                auto nframes = b->num_frames();
                for (int16_t offset = 0; offset < nframes; offset += 16) {
                    uint16_t bitset = 0;
                    // fill the bitset with missing frames
                    for (int i = 0; i < 16; ++i) {
                        auto frame = offset + i;
                        if (frame >= nframes) {
                            break;
                        }
                        if (!b->has_frame(frame)) {
                            bitset |= (uint16_t)1 << i;
                        #if AOO_DEBUG_RESEND
                            LOG_DEBUG("AooSink: request " << b->sequence
                                      << " (" << frame << " / " << nframes << ")");
                        #endif
                        }
                    }
                    if (bitset != 0) {
                        push_data_request({ b->sequence, offset, bitset });
                    }
                }
            } else {
                // b) all frames missing
                push_data_request({ b->sequence, -1, 0 }); // whole block
            #if AOO_DEBUG_RESEND
                LOG_DEBUG("AooSink: request " << b->sequence << " (all)");
            #endif
            }

            if (++resent >= limit) {
                LOG_DEBUG("AooSource: resend limit reached");
                break;
            }
        }
    }

    if (resent > 0) {
        LOG_DEBUG("AooSink: requested " << resent << " blocks");
    }
}

void source_desc::sched_stream_message(stream_message_header *msg) {
    // keep sorted!
    if (stream_messages_ && msg->time >= stream_messages_->time) {
        // a) insert
        auto it = stream_messages_;
        while (it->next && msg->time >= it->next->time) {
            it = it->next;
        }
        msg->next = it->next;
        it->next = msg;
    } else {
        // b) prepend
        msg->next = stream_messages_;
        stream_messages_ = msg;
    }
}

void source_desc::dispatch_stream_messages(const Sink &s, int nsamples,
                                           AooStreamMessageHandler fn, void *user) {
    // dispatch stream messages
    auto deadline = process_samples_ + nsamples;
    while (stream_messages_) {
        auto it = stream_messages_;
        if (it->time < deadline) {
            int32_t offset = it->time - process_samples_ + 0.5;
            if (offset >= nsamples) {
                break;
            }
            if (it->type == kAooDataStreamTime) {
                // a) stream time event
                if (offset >= 0) {
                    auto source_tt = reinterpret_cast<stream_time_message *>(it)->tt;
                    auto elapsed = it->time / (double)s.samplerate();
                    auto local_tt = local_tt_ + time_tag::from_seconds(elapsed);
                    auto e = make_event<stream_time_event>(ep, source_tt, local_tt, offset);
                    queue_event(std::move(e));
                #if AOO_DEBUG_STREAM_MESSAGE
                    LOG_DEBUG("AooSink: dispatch stream time message (source tt: "
                              << aoo::time_tag(source_tt) << ", local tt: "
                              << aoo::time_tag(local_tt) << ", stream samples: "
                              << (uint64_t)it->time << ", offset: " << offset << ")");
                #endif
                } else {
                    // this may happen with xruns
                    LOG_INFO("AooSink: skip stream time event (offset: " << offset << ")");
                }
            } else {
                // b) stream message
                if (offset >= 0) {
                    AooStreamMessage msg;
                    msg.sampleOffset = offset;
                    msg.channel = it->channel;
                    msg.type = it->type;
                    msg.size = it->size;
                    msg.data = (const AooByte *)reinterpret_cast<flat_stream_message *>(it)->data;

                    AooEndpoint ep;
                    ep.address = this->ep.address.address();
                    ep.addrlen = this->ep.address.length();
                    ep.id = this->ep.id;

                #if AOO_DEBUG_STREAM_MESSAGE
                    LOG_DEBUG("AooSink: dispatch stream message "
                              << "(type: " << aoo_dataTypeToString(msg.type)
                              << ", channel: " << msg.channel << ", size: " << msg.size
                              << ", offset: " << msg.sampleOffset << ")");
                #endif

                    // NB: the stream message handler is called with the mutex locked!
                    // See the documentation of AooStreamMessageHandler.
                    fn(user, &msg, &ep);
                } else {
                    // this may happen with xruns
                    LOG_INFO("AooSink: skip stream message (offset: " << offset << ")");
                }
            }

            auto next = it->next;
            auto alloc_size = sizeof(stream_message_header) + it->size;
            aoo::rt_deallocate(it, alloc_size);
            stream_messages_ = next;
        } else {
            break;
        }
    }
    process_samples_ = deadline;
}

void source_desc::flush_packet_queue() {
    LOG_DEBUG("AooSink: flush packet queue");
    packet_queue_.consume_all([&](const auto& packet) {
        if (packet.frame) {
            frame_allocator_.deallocate(packet.frame);
        }
    });
}

// /aoo/src/<id>/ping <id> <tt1>
void source_desc::send_ping(const Sink&s, const sendfn& fn) {
    if (state_.load(std::memory_order_relaxed) != source_state::run) {
        return;
    }
    auto elapsed = s.elapsed_time();
    auto pingtime = last_ping_time_.load();
    auto interval = s.ping_interval(); // 0: no ping
    if (interval > 0 && (elapsed - pingtime) >= interval){
        auto tt = aoo::time_tag::now();
        // send ping to source
        LOG_DEBUG("AooSink: send " kAooMsgPing " to " << ep);

        char buf[AOO_MAX_PACKET_SIZE];
        osc::OutboundPacketStream msg(buf, sizeof(buf));

        const int32_t max_addr_size = kAooMsgDomainLen
                + kAooMsgSourceLen + 16 + kAooMsgPingLen;
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s/%d%s",
                 kAooMsgDomain kAooMsgSource, (int)ep.id, kAooMsgPing);

        msg << osc::BeginMessage(address) << s.id() << osc::TimeTag(tt)
            << osc::EndMessage;

        ep.send(msg, fn);

        last_ping_time_.store(elapsed);
    }
}

// /aoo/src/<id>/pong <id> <tt1> <tt2> <packetloss>
// called without lock!
void source_desc::send_pong(const Sink &s, time_tag tt1, time_tag tt2, const sendfn &fn) {
    LOG_DEBUG("AooSink: send " kAooMsgPong " to " << ep);

    // cache samplerate and blocksize
    shared_lock lock(mutex_);
    if (!format_){
        LOG_DEBUG("AooSink: send_pong: no format");
        return; // shouldn't happen
    }
    auto sr = format_->sampleRate;
    auto blocksize = format_->blockSize;
    lock.unlock();

    // get lost blocks since last pong and calculate packet loss percentage.
    auto dropped_blocks = dropped_blocks_.exchange(0);
    auto last_ping_time = std::exchange(last_ping_reply_time_, tt2);
    // NOTE: the delta can be very large for the first ping in a stream,
    // but this is not an issue because there's no packet loss anyway.
    auto delta = time_tag::duration(last_ping_time, tt2);
    float packetloss = (float)dropped_blocks * (float)blocksize / ((float)sr * delta);
    if (packetloss > 1.0){
        LOG_DEBUG("AooSink: packet loss percentage larger than 1");
        packetloss = 1.0;
    }
    LOG_DEBUG("AooSink: ping delta: " << delta << ", packet loss: " << packetloss);

    time_tag tt3 = aoo::time_tag::now(); // local send time

    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSourceLen + 16 + kAooMsgPongLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSource, (int)ep.id, kAooMsgPong);

    msg << osc::BeginMessage(address) << s.id()
        << osc::TimeTag(tt1) << osc::TimeTag(tt2) << osc::TimeTag(tt3) << packetloss
        << osc::EndMessage;

    ep.send(msg, fn);
}

// /aoo/src/<id>/start <sink> <version>
// called without lock!
void source_desc::send_start_request(const Sink& s, const sendfn& fn) {
    LOG_INFO("AooSink: request " kAooMsgStart " for source " << ep);

    AooByte buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg((char *)buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen + kAooMsgSourceLen
                                  + 16 + kAooMsgStartLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSource, (int)ep.id, kAooMsgStart);

    msg << osc::BeginMessage(address)
        << s.id() << aoo_getVersionString()
        << osc::EndMessage;

    ep.send(msg, fn);
}

// /aoo/src/<id>/stop <sink> <stream>
// called without lock!
void source_desc::send_stop_request(const Sink& s, int32_t stream, const sendfn& fn) {
    LOG_INFO("AooSink: request " kAooMsgStop " for source " << ep);

    AooByte buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg((char *)buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen + kAooMsgSourceLen
                                  + 16 + kAooMsgStopLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSource, (int)ep.id, kAooMsgStop);

    msg << osc::BeginMessage(address)
        << s.id() << stream
        << osc::EndMessage;

    ep.send(msg, fn);
}

// /aoo/src/<id>/data <id> <stream_id> <seq1> <frame1> <seq2> <frame2> etc.
// or
// header, stream_id (int32), count (int32),
// seq1 (int32), offset1 (int16), bitset1 (uint16), ... // offset < 0 -> all

void source_desc::send_data_requests(const Sink& s, const sendfn& fn){
    if (data_requests_.empty()){
        return;
    }

    shared_lock lock(mutex_);
    int32_t stream_id = stream_id_; // cache!
    lock.unlock();

    AooByte buf[AOO_MAX_PACKET_SIZE];

    if (ep.binary) {
        // --- binary version ---
        const int32_t maxdatasize = s.packet_size() - kBinDataHeaderSize;
        const int32_t maxrequests = maxdatasize / 8; // 2 * int32

        // write header
        auto onset = aoo::binmsg_write_header(buf, sizeof(buf), kAooMsgTypeSource,
                                              kAooBinMsgCmdData, ep.id, s.id());
        // write arguments
        auto it = buf + onset;
        aoo::write_bytes<int32_t>(stream_id, it);
        // skip 'count' field
        it += sizeof(int32_t);

        auto head = it; // cache pointer

        int32_t numrequests = 0;
        data_request r;
        while (data_requests_.pop(r)) {
            aoo::write_bytes<int32_t>(r.sequence, it);
            aoo::write_bytes<int16_t>(r.offset, it);
            aoo::write_bytes<uint16_t>(r.bitset, it);
            if (++numrequests >= maxrequests){
                // write 'count' field
                aoo::to_bytes<int32_t>(numrequests, head - sizeof(int32_t));
            #if AOO_DEBUG_RESEND
                LOG_DEBUG("AooSink: send binary data request ("
                          << r.sequence << " " << r.offset << " " << r.bitset << ")");
            #endif
                // send it off
                ep.send(buf, it - buf, fn);
                // prepare next message (just rewind)
                it = head;
                numrequests = 0;
            }
        }

        if (numrequests > 0){
            // write 'count' field
            aoo::to_bytes(numrequests, head - sizeof(int32_t));
            // send it off
            ep.send(buf, it - buf, fn);
        }
    } else {
        // --- OSC version ---
        char buf[AOO_MAX_PACKET_SIZE];
        osc::OutboundPacketStream msg(buf, sizeof(buf));

        // make OSC address pattern
        char pattern[kDataMaxAddrSize];
        snprintf(pattern, sizeof(pattern), "%s/%d%s",
                 kAooMsgDomain kAooMsgSource, (int)ep.id, kAooMsgData);

        const int32_t maxdatasize = s.packet_size() - kDataHeaderSize;
        const int32_t maxrequests = maxdatasize / 10; // 2 * (int32_t + typetag + padding)
        int32_t numrequests = 0;

        msg << osc::BeginMessage(pattern) << s.id() << stream_id;

        data_request r;
        while (data_requests_.pop(r)) {
            if (r.offset < 0) {
                // request whole block
            #if AOO_DEBUG_RESEND
                LOG_DEBUG("AooSink: send data request (" << r.sequence << " -1)");
            #endif
                msg << r.sequence << (int32_t)-1;
                if (++numrequests >= maxrequests){
                    // send it off
                    msg << osc::EndMessage;

                    ep.send(msg, fn);

                    // prepare next message
                    msg.Clear();
                    msg << osc::BeginMessage(pattern) << s.id() << stream_id;
                    numrequests = 0;
                }
            } else {
                // get frames from offset and bitset
                uint16_t bitset = r.bitset;
                for (int i = 0; bitset != 0; ++i, bitset >>= 1) {
                    if (bitset & 1) {
                        auto frame = r.offset + i;
                    #if AOO_DEBUG_RESEND
                        LOG_DEBUG("AooSink: send data request ("
                                  << r.sequence << " " << frame << ")");
                    #endif
                        msg << r.sequence << (int32_t)frame;
                        if (++numrequests >= maxrequests){
                            // send it off
                            msg << osc::EndMessage;

                            ep.send(msg, fn);

                            // prepare next message
                            msg.Clear();
                            msg << osc::BeginMessage(pattern) << s.id() << stream_id;
                            numrequests = 0;
                        }
                    }
                }
            }
        }

        if (numrequests > 0){
            // send it off
            msg << osc::EndMessage;

            ep.send(msg, fn);
        }
    }
}

// /aoo/src/<id>/invite <sink> <stream_id> [<metadata_type> <metadata_content>]

// called without lock!
void send_invitation(const Sink& s, const endpoint& ep, AooId token,
                     const AooData *metadata, const sendfn& fn){
    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSourceLen + 16 + kAooMsgInviteLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSource, (int)ep.id, kAooMsgInvite);

    msg << osc::BeginMessage(address) << s.id() << token
        << metadata_view(metadata) << osc::EndMessage;

    LOG_DEBUG("AooSink: send " kAooMsgInvite " to source " << ep
              << " (" << token << ")");

    ep.send(msg, fn);
}

// /aoo/<id>/uninvite <sink>

void send_uninvitation(const Sink& s, const endpoint& ep,
                       AooId token, const sendfn &fn){
    LOG_DEBUG("AooSink: send " kAooMsgUninvite " to source " << ep);

    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSourceLen + 16 + kAooMsgUninviteLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s/%d%s",
             kAooMsgDomain kAooMsgSource, (int)ep.id, kAooMsgUninvite);

    msg << osc::BeginMessage(address) << s.id() << token
        << osc::EndMessage;

    ep.send(msg, fn);
}

// only send every 100 ms! LATER we might make this settable
#define INVITE_INTERVAL 0.1

void source_desc::send_invitations(const Sink &s, const sendfn &fn){
    auto state = state_.load(std::memory_order_acquire);
    if (state != source_state::invite){
        return;
    }

    auto timeout = s.invite_timeout();
    auto now = s.elapsed_time();
    auto delta = now - invite_start_time_.load(std::memory_order_acquire);
    if (timeout >= 0 && delta >= timeout){
        // transition into 'timeout' state, but only if the state
        // hasn't changed in between.
        if (state_.compare_exchange_strong(state, source_state::timeout)){
            LOG_DEBUG("AooSink: send_invitation: invite -> timeout");
        } else {
            LOG_DEBUG("AooSink: send_invitation: invite -> timeout failed");
        }
        // always send timeout event
        LOG_INFO("AooSink: " << ep << ": invitation timed out");
        auto e = make_event<source_event>(kAooEventInviteTimeout, ep);
        s.send_event(std::move(e), kAooThreadLevelNetwork);
    } else {
        delta = now - last_invite_time_.load(std::memory_order_relaxed);
        if (delta >= INVITE_INTERVAL){
            auto token = invite_token_.load();
            // NOTE: the metadata is only read/set in the send thread,
            // so we don't have to worry about race conditions!
            auto metadata = invite_metadata_.get();
            send_invitation(s, ep, token, metadata, fn);

            last_invite_time_.store(now);
        }
    }
}

void source_desc::reset_stream() {
    for (auto it = stream_messages_; it; ){
        auto next = it->next;
        auto alloc_size = sizeof(stream_message_header) + it->size;
        aoo::rt_deallocate(it, alloc_size);
        it = next;
    }
    stream_messages_ = nullptr;
    process_samples_ = 0;
    stream_samples_ = 0;
    stream_start_ = 0;
    local_tt_.clear();
    last_ping_time_.store(-1e007); // force ping
    last_stop_time_ = 0; // reset stop request timer
}

// NOTE: for kAooEventModeCallback we could theoretically
// pass the event directly to Sink::send_event(),
// but I'm not sure if it would make a real difference.
void source_desc::queue_event(event_ptr e) {
    event_buffer_.push_back(std::move(e));
}

void source_desc::flush_events(const Sink& s) {
    for (auto& e : event_buffer_) {
        s.send_event(std::move(e), kAooThreadLevelAudio);
    }
    event_buffer_.clear();
}

} // aoo
