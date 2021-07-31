/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "sink.hpp"

#include <algorithm>
#include <cmath>

const size_t kAooEventQueueSize = 8;

/*//////////////////// AooSink /////////////////////*/

AOO_API AooSink * AOO_CALL AooSink_new(
        AooId id, AooFlag flags, AooError *err) {
    return aoo::construct<aoo::sink_imp>(id, flags, err);
}

aoo::sink_imp::sink_imp(AooId id, AooFlag flags, AooError *err)
    : id_(id) {
    eventqueue_.reserve(kAooEventQueueSize);
}

AOO_API void AOO_CALL AooSink_free(AooSink *sink) {
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::sink_imp *>(sink));
}

aoo::sink_imp::~sink_imp(){}

AOO_API AooError AOO_CALL AooSink_setup(
        AooSink *sink, AooSampleRate samplerate,
        AooInt32 blocksize, AooInt32 nchannels) {
    return sink->setup(samplerate, blocksize, nchannels);
}

AooError AOO_CALL aoo::sink_imp::setup(
        AooSampleRate samplerate, AooInt32 blocksize, AooInt32 nchannels){
    if (samplerate > 0 && blocksize > 0 && nchannels > 0)
    {
        if (samplerate != samplerate_ || blocksize != blocksize_ ||
            nchannels != nchannels_)
        {
            nchannels_ = nchannels;
            samplerate_ = samplerate;
            blocksize_ = blocksize;

            realsr_.store(samplerate);

            reset_sources();
        }

        // always reset timer + time DLL filter
        timer_.setup(samplerate_, blocksize_, timer_check_.load());

        return kAooOk;
    } else {
        return kAooErrorUnknown;
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
        return kAooErrorUnknown;        \
    }                                   \

AOO_API AooError AOO_CALL AooSink_control(
        AooSink *sink, AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    return sink->control(ctl, index, ptr, size);
}

AooError AOO_CALL aoo::sink_imp::control(
        AooCtl ctl, AooIntPtr index, void *ptr, AooSize size)
{
    switch (ctl){
    // invite source
    case kAooCtlInviteSource:
    {
        auto ep = (const AooEndpoint *)index;
        if (!ep){
            return kAooErrorUnknown;
        }
        ip_address addr((const sockaddr *)ep->address, ep->addrlen);

        push_request(source_request { request_type::invite, addr, ep->id });

        break;
    }
    // uninvite source(s)
    case kAooCtlUninviteSource:
    {
        auto ep = (const AooEndpoint *)index;
        if (ep){
            // single source
            ip_address addr((const sockaddr *)ep->address, ep->addrlen);

            push_request(source_request { request_type::uninvite, addr, ep->id });
        } else {
            // all sources
            push_request(source_request { request_type::uninvite_all });
        }
        break;
    }
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
            GETSOURCEARG;
            src->reset(*this);
        } else {
            // reset all sources
            reset_sources();
            // reset time DLL
            timer_.reset();
        }
        break;
    }
    // request format
    case kAooCtlRequestFormat:
    {
        CHECKARG(AooFormat);
        GETSOURCEARG;
        return src->request_format(*this, as<AooFormat>(ptr));
    }
    // get format
    case kAooCtlGetFormat:
    {
        assert(size >= sizeof(AooFormat));
        GETSOURCEARG;
        return src->get_format(as<AooFormat>(ptr), size);
    }
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
        CHECKARG(float);
        GETSOURCEARG;
        as<float>(ptr) = src->get_buffer_fill_ratio();
        break;
    }
    // timer check
    case kAooCtlSetTimerCheck:
        CHECKARG(AooBool);
        timer_check_.store(as<AooBool>(ptr));
        break;
    case kAooCtlGetTimerCheck:
        CHECKARG(AooBool);
        as<AooBool>(ptr) = timer_check_.load();
        break;
    // dynamic resampling
    case kAooCtlSetDynamicResampling:
        CHECKARG(AooBool);
        dynamic_resampling_.store(as<AooBool>(ptr));
        timer_.reset(); // !
        break;
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
        timer_.reset(); // will update time DLL and reset timer
        break;
    }
    case kAooCtlGetDllBandwidth:
        CHECKARG(float);
        as<float>(ptr) = dll_bandwidth_.load();
        break;
    // real samplerate
    case kAooCtlGetRealSampleRate:
        CHECKARG(double);
        as<double>(ptr) = realsr_.load(std::memory_order_relaxed);
        break;
    // packetsize
    case kAooCtlSetPacketSize:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = 64;
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
        CHECKARG(AooSeconds);
        as<int32_t>(ptr) = resend_limit_.load();
        break;
    // source timeout
    case kAooCtlSetSourceTimeout:
    {
        CHECKARG(AooSeconds);
        auto timeout = std::max<AooSeconds>(0, as<AooSeconds>(ptr));
        source_timeout_.store(timeout);
        break;
    }
    case kAooCtlGetSourceTimeout:
        CHECKARG(AooSeconds);
        as<AooSeconds>(ptr) = source_timeout_.load();
        break;
#if USE_AOO_NET
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

AOO_API AooError AOO_CALL AooSink_handleMessage(
        AooSink *sink, const AooByte *data, AooSize size,
        const void *address, AooAddrSize addrlen) {
    return sink->handleMessage(data, size, address, addrlen);
}

AooError AOO_CALL aoo::sink_imp::handleMessage(
        const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen)
{
    if (samplerate_ == 0){
        return kAooErrorUnknown; // not setup yet
    }

    AooMsgType type;
    AooId sinkid;
    AooInt32 onset;
    auto err = aoo_parsePattern(data, size, &type, &sinkid, &onset);
    if (err != kAooOk){
        LOG_WARNING("not an AoO message!");
        return kAooErrorUnknown;
    }

    if (type != kAooTypeSink){
        LOG_WARNING("not a sink message!");
        return kAooErrorUnknown;
    }
    if (sinkid != id()){
        LOG_WARNING("wrong sink ID!");
        return kAooErrorUnknown;
    }

    ip_address addr((const sockaddr *)address, addrlen);

    if (data[0] == 0){
        // binary message
        auto cmd = aoo::from_bytes<int16_t>(data + kAooBinMsgDomainSize + 2);
        switch (cmd){
        case kAooBinMsgCmdData:
            return handle_data_message(data + onset, size - onset, addr);
        default:
            return kAooErrorUnknown;
        }
    } else {
        // OSC message
        try {
            osc::ReceivedPacket packet((const char *)data, size);
            osc::ReceivedMessage msg(packet);

            auto pattern = msg.AddressPattern() + onset;
            if (!strcmp(pattern, kAooMsgStart)){
                return handle_start_message(msg, addr);
            } else if (!strcmp(pattern, kAooMsgStop)){
                return handle_stop_message(msg, addr);
            } else if (!strcmp(pattern, kAooMsgData)){
                return handle_data_message(msg, addr);
            } else if (!strcmp(pattern, kAooMsgPing)){
                return handle_ping_message(msg, addr);
            } else {
                LOG_WARNING("unknown message " << pattern);
            }
        } catch (const osc::Exception& e){
            LOG_ERROR("AooSink: exception in handle_message: " << e.what());
        }
        return kAooErrorUnknown;
    }
}

AOO_API AooError AOO_CALL AooSink_send(
        AooSink *sink, AooSendFunc fn, void *user){
    return sink->send(fn, user);
}

AooError AOO_CALL aoo::sink_imp::send(AooSendFunc fn, void *user){
    sendfn reply(fn, user);

    // handle requests
    source_request r;
    while (requestqueue_.try_pop(r)){
        switch (r.type) {
        case request_type::invite:
        {
            // try to find existing source
            // we might want to invite an existing source,
            // e.g. when it is currently uninviting
            // NOTE that sources can also be added in the network
            // receive thread (see handle_data() or handle_format()),
            // so we have to lock a mutex to avoid the ABA problem.
            sync::scoped_lock<sync::mutex> lock(source_mutex_);
            source_lock srclock(sources_);
            auto src = find_source(r.address, r.id);
            if (!src){
                src = add_source(r.address, r.id);
            }
            src->invite(*this);
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
                LOG_WARNING("aoo: can't uninvite - source not found");
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

    source_lock lock(sources_);
    for (auto& s : sources_){
        s.send(*this, reply);
    }
    lock.unlock();

    // free unused source_descs
    if (!sources_.try_free()){
        // LOG_DEBUG("AooSink: try_free() would block");
    }

    return kAooOk;
}

AOO_API AooError AOO_CALL AooSink_process(
        AooSink *sink, AooSample **data, AooInt32 nsamples, AooNtpTime t) {
    return sink->process(data, nsamples, t);
}

AooError AOO_CALL aoo::sink_imp::process(
        AooSample **data, AooInt32 nsamples, AooNtpTime t){
    // clear outputs
    for (int i = 0; i < nchannels_; ++i){
        std::fill(data[i], data[i] + nsamples, 0);
    }

    // update timer
    // always do this, even if there are no sources!
    bool dynamic_resampling = dynamic_resampling_.load(std::memory_order_relaxed);
    double error;
    auto state = timer_.update(t, error);
    if (state == timer::state::reset){
        LOG_DEBUG("setup time DLL filter for sink");
        auto bw = dll_bandwidth_.load(std::memory_order_relaxed);
        dll_.setup(samplerate_, blocksize_, bw, 0);
        realsr_.store(samplerate_, std::memory_order_relaxed);
    } else if (state == timer::state::error){
        // recover sources
        int32_t xrunsamples = error * samplerate_ + 0.5;

        // no lock needed - sources are only removed in this thread!
        for (auto& s : sources_){
            s.add_xrun(xrunsamples);
        }

        sink_event e(kAooEventXRun);
        e.count = (float)xrunsamples / (float)blocksize_;
        send_event(e, kAooThreadLevelAudio);

        timer_.reset();
    } else if (dynamic_resampling) {
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

    bool didsomething = false;

    // no lock needed - sources are only removed in this thread!
    for (auto it = sources_.begin(); it != sources_.end();){
        if (it->process(*this, data, nsamples, t)){
            didsomething = true;
        } else if (!it->is_active(*this)){
            // move source to garbage list (will be freed in send())
            if (it->is_inviting()){
                LOG_VERBOSE("AooSink: invitation for " << it->ep << " timed out");
                sink_event e(kAooEventInviteTimeout, it->ep);
                send_event(e, kAooThreadLevelAudio);
            } else {
                LOG_VERBOSE("AooSink: removed inactive source " << it->ep);
                sink_event e(kAooEventSourceRemove, it->ep);
                send_event(e, kAooThreadLevelAudio);
            }
            it = sources_.erase(it);
            continue;
        }
        ++it;
    }

    if (didsomething){
    #if AOO_CLIP_OUTPUT
        for (int i = 0; i < nchannels_; ++i){
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

AooError AOO_CALL aoo::sink_imp::setEventHandler(
        AooEventHandler fn, void *user, AooEventMode mode)
{
    eventhandler_ = fn;
    eventcontext_ = user;
    eventmode_ = mode;
    return kAooOk;
}

AOO_API AooBool AOO_CALL AooSink_eventsAvailable(AooSink *sink){
    return sink->eventsAvailable();
}

AooBool AOO_CALL aoo::sink_imp::eventsAvailable(){
    if (!eventqueue_.empty()){
        return true;
    }

    source_lock lock(sources_);
    for (auto& src : sources_){
        if (src.has_events()){
            return true;
        }
    }

    return false;
}

AOO_API AooError AOO_CALL AooSink_pollEvents(AooSink *sink){
    return sink->pollEvents();
}

#define EVENT_THROTTLE 1000

AooError AOO_CALL aoo::sink_imp::pollEvents(){
    int total = 0;
    sink_event e;
    while (eventqueue_.try_pop(e)){
        if (e.type == kAooEventXRun){
            AooEventXRun xe;
            xe.type = e.type;
            xe.count = e.count;
            eventhandler_(eventcontext_, (const AooEvent *)&xe,
                          kAooThreadLevelUnknown);
        } else {
            AooEventEndpoint ee;
            ee.type = e.type;
            ee.endpoint.address = e.address.address();
            ee.endpoint.addrlen = e.address.length();
            ee.endpoint.id = e.id;
            eventhandler_(eventcontext_, (const AooEvent *)&ee,
                          kAooThreadLevelUnknown);
        }

        total++;
    }
    // we only need to protect against source removal
    source_lock lock(sources_);
    for (auto& src : sources_){
        total += src.poll_events(*this, eventhandler_, eventcontext_);
        if (total > EVENT_THROTTLE){
            break;
        }
    }
    return kAooOk;
}

namespace aoo {

void sink_imp::send_event(const sink_event &e, AooThreadLevel level) {
    switch (eventmode_){
    case kAooEventModePoll:
        eventqueue_.push(e);
        break;
    case kAooEventModeCallback:
    {
        AooEventEndpoint ee;
        ee.type = e.type;
        ee.endpoint.address = e.address.address();
        ee.endpoint.addrlen = e.address.length();
        ee.endpoint.id = e.id;
        eventhandler_(eventcontext_, (const AooEvent *)&ee, level);
        break;
    }
    default:
        break;
    }
}

// only called if mode is kAooEventModeCallback
void sink_imp::call_event(const event &e, AooThreadLevel level) const {
    eventhandler_(eventcontext_, &e.event_, level);
}

aoo::source_desc * sink_imp::find_source(const ip_address& addr, AooId id){
    for (auto& src : sources_){
        if (src.match(addr, id)){
            return &src;
        }
    }
    return nullptr;
}

aoo::source_desc * sink_imp::get_source_arg(intptr_t index){
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

source_desc * sink_imp::add_source(const ip_address& addr, AooId id){
    // add new source
    uint32_t flags = 0;
#if USE_AOO_NET
    // check if the peer needs to be relayed
    if (client_){
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        AooBool relay;
        if (client_->control(kAooCtlNeedRelay,
                             reinterpret_cast<intptr_t>(&ep),
                             &relay, sizeof(relay)) == kAooOk)
        {
            if (relay == kAooTrue){
                LOG_DEBUG("source " << addr << " needs to be relayed");
                flags |= kAooEndpointRelay;
            }
        }
    }
#endif
    sources_.emplace_front(addr, id, flags, elapsed_time());
    return &sources_.front();
}

void sink_imp::reset_sources(){
    source_lock lock(sources_);
    for (auto& src : sources_){
        src.reset(*this);
    }
}

// /aoo/sink/<id>/start <src> <version> <stream_id> <flags> <lastformat>
// <nchannels> <samplerate> <blocksize> <codec> <options>
// [<metadata_type> <metadata_content>]
AooError sink_imp::handle_start_message(const osc::ReceivedMessage& msg,
                                        const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    AooId id = (it++)->AsInt32();
    int32_t version = (it++)->AsInt32();

    // LATER handle this in the source_desc (e.g. ignoring further messages)
    if (!check_version(version)){
        LOG_ERROR("AooSink: source version not supported");
        return kAooErrorUnknown;
    }

    AooId stream = (it++)->AsInt32();
    AooFlag flags = (it++)->AsInt32();
    AooId lastformat = (it++)->AsInt32();
    // get stream format
    AooFormat f;
    f.numChannels = (it++)->AsInt32();
    f.sampleRate = (it++)->AsInt32();
    f.blockSize = (it++)->AsInt32();
    f.codec = (it++)->AsString();
    f.size = sizeof(AooFormat);
    const void *settings;
    osc::osc_bundle_element_size_t size;
    (it++)->AsBlob(settings, size);
    // get stream metadata
    AooCustomData md;
    if (msg.ArgumentCount() >= 12) {
        md.type = (it++)->AsString();
        const void *md_data;
        osc::osc_bundle_element_size_t md_size;
        (it++)->AsBlob(md_data, md_size);
        md.data = (const AooByte *)md_data;
        md.size = md_size;
    } else {
        md.type = kAooCustomDataInvalid;
        md.data = nullptr;
        md.size = 0;
    }

    if (id < 0){
        LOG_WARNING("bad ID for " << kAooMsgFormat << " message");
        return kAooErrorUnknown;
    }
    // try to find existing source
    // NOTE: sources can also be added in the network send thread,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock(source_mutex_);
    source_lock srclock(sources_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id);
    }
    return src->handle_start(*this, stream, flags, lastformat, f,
                             (const AooByte *)settings, size, md);
}

// /aoo/sink/<id>/stop <src> <stream>
AooError sink_imp::handle_stop_message(const osc::ReceivedMessage& msg,
                                       const ip_address& addr) {
    auto it = msg.ArgumentsBegin();

    AooId id = (it++)->AsInt32();
    AooId stream = (it++)->AsInt32();

    if (id < 0){
        LOG_WARNING("bad ID for " << kAooMsgStop << " message");
        return kAooErrorUnknown;
    }
    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_stop(*this, stream);
    } else {
        return kAooErrorUnknown;
    }
}

AooError sink_imp::handle_data_message(const osc::ReceivedMessage& msg,
                                        const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();

    aoo::net_packet d;
    d.stream_id = (it++)->AsInt32();
    d.sequence = (it++)->AsInt32();
    d.samplerate = (it++)->AsDouble();
    d.channel = (it++)->AsInt32();
    d.totalsize = (it++)->AsInt32();
    d.nframes = (it++)->AsInt32();
    d.frame = (it++)->AsInt32();
    const void *blobdata;
    osc::osc_bundle_element_size_t blobsize;
    (it++)->AsBlob(blobdata, blobsize);
    d.data = (const AooByte *)blobdata;
    d.size = blobsize;

    return handle_data_packet(d, false, addr, id);
}

// binary data message:
// id (int32), stream_id (int32), seq (int32), channel (int16), flags (int16),
// [total (int32), nframes (int16), frame (int16)],  [sr (float64)],
// size (int32), data...

AooError sink_imp::handle_data_message(const AooByte *msg, int32_t n,
                                       const ip_address& addr)
{
    // check size (excluding samplerate, frames and data)
    if (n < 20){
        LOG_ERROR("handle_data_message: header too small!");
        return kAooErrorUnknown;
    }

    auto it = msg;

    auto id = aoo::read_bytes<int32_t>(it);

    aoo::net_packet d;
    d.stream_id = aoo::read_bytes<int32_t>(it);
    d.sequence = aoo::read_bytes<int32_t>(it);
    d.channel = aoo::read_bytes<int16_t>(it);
    auto flags = aoo::read_bytes<int16_t>(it);
    if (flags & kAooBinMsgDataFrames){
        d.totalsize = aoo::read_bytes<int32_t>(it);
        d.nframes = aoo::read_bytes<int16_t>(it);
        d.frame = aoo::read_bytes<int16_t>(it);
    } else {
        d.totalsize = 0;
        d.nframes = 1;
        d.frame = 0;
    }
    if (flags & kAooBinMsgDataSampleRate){
        d.samplerate = aoo::read_bytes<double>(it);
    } else {
        d.samplerate = 0;
    }

    d.size = aoo::read_bytes<int32_t>(it);
    if (d.totalsize == 0){
        d.totalsize = d.size;
    }

    if (n < ((it - msg) + d.size)){
        LOG_ERROR("handle_data_bin_message: wrong data size!");
        return kAooErrorUnknown;
    }

    d.data = it;

    return handle_data_packet(d, true, addr, id);
}

AooError sink_imp::handle_data_packet(net_packet& d, bool binary,
                                       const ip_address& addr, AooId id)
{
    if (id < 0){
        LOG_WARNING("bad ID for " << kAooMsgData << " message");
        return kAooErrorUnknown;
    }
    // try to find existing source
    // NOTE: sources can also be added in the network send thread,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock(source_mutex_);
    source_lock srclock(sources_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id);
    }
    return src->handle_data(*this, d, binary);
}

AooError sink_imp::handle_ping_message(const osc::ReceivedMessage& msg,
                                        const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    time_tag tt = (it++)->AsTimeTag();

    if (id < 0){
        LOG_WARNING("bad ID for " << kAooMsgPing << " message");
        return kAooErrorUnknown;
    }
    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_ping(*this, tt);
    } else {
        LOG_WARNING("couldn't find source " << addr << "|" << id
                    << " for " << kAooMsgPing << " message");
        return kAooErrorUnknown;
    }
}

/*////////////////////////// event ///////////////////////////////////*/

// 'event' is always used inside 'source_desc', so we can safely
// store a pointer to the sockaddr. the ip_address itself
// never changes during lifetime of the 'source_desc'!
// NOTE: this assumes that the event queue is polled regularly,
// i.e. before a source_desc can be possibly autoremoved.
event::event(AooEventType type, const endpoint& ep){
    source.type = type;
    source.endpoint.address = ep.address.address();
    source.endpoint.addrlen = ep.address.length();
    source.endpoint.id = ep.id;
}

// 'sink_event' is used in 'sink' for source events that can outlive
// its corresponding 'source_desc', therefore the ip_address is copied!
sink_event::sink_event(AooEventType _type, const endpoint &ep)
    : type(_type), address(ep.address), id(ep.id) {}

/*////////////////////////// source_desc /////////////////////////////*/

source_desc::source_desc(const ip_address& addr, AooId id,
                         uint32_t flags, double time)
    : ep(addr, id, flags), last_packet_time_(time)
{
    // reserve some memory, so we don't have to allocate memory
    // when pushing events in the audio thread.
    eventqueue_.reserve(kAooEventQueueSize);
    // resendqueue_.reserve(256);
    LOG_DEBUG("source_desc");
}

source_desc::~source_desc(){
    // flush event queue
    event e;
    while (eventqueue_.try_pop(e)){
        free_event_data(e);
    }
    // flush packet queue
    net_packet d;
    while (packetqueue_.try_pop(d)){
        memory_.deallocate((void *)d.data);
    }
    // free metadata
    if (metadata_){
        memory_.deallocate((void *)metadata_);
    }
    LOG_DEBUG("~source_desc");
}

bool source_desc::is_active(const sink_imp& s) const {
    auto last = last_packet_time_.load(std::memory_order_relaxed);
    return (s.elapsed_time() - last) < s.source_timeout();
}

AooError source_desc::get_format(AooFormat &format, size_t size){
    // synchronize with handle_format() and update()!
    scoped_shared_lock lock(mutex_);
    if (decoder_){
        return decoder_->get_format(format, size);
    } else {
        return kAooErrorUnknown;
    }
}

void source_desc::reset(const sink_imp& s){
    // take writer lock!
    scoped_lock lock(mutex_);
    update(s);
}

#define MAXHWBUFSIZE 2048
#define MINSAMPLERATE 44100

void source_desc::update(const sink_imp& s){
    // resize audio ring buffer
    if (decoder_ && decoder_->blocksize() > 0 && decoder_->samplerate() > 0){
        // recalculate buffersize from seconds to samples
        int32_t bufsize = s.buffersize() * decoder_->samplerate();
        // number of buffers (round up!)
        int32_t nbuffers = std::ceil((double)bufsize / (double)decoder_->blocksize());
        // minimum buffer size depends on resampling and reblocking!
        auto downsample = (double)decoder_->samplerate() / (double)s.samplerate();
        auto reblock = (double)s.blocksize() / (double)decoder_->blocksize();
        minblocks_ = std::ceil(downsample * reblock);
        nbuffers = std::max<int32_t>(nbuffers, minblocks_);
        LOG_DEBUG("source_desc: buffersize (ms): " << (s.buffersize() * 1000)
                  << ", samples: " << bufsize << ", nbuffers: " << nbuffers
                  << ", minimum: " << minblocks_);

    #if 0
        // don't touch the event queue once constructed
        eventqueue_.reset();
    #endif

        auto nsamples = decoder_->nchannels() * decoder_->blocksize();
        double sr = decoder_->samplerate(); // nominal samplerate

        // setup audio buffer
        auto nbytes = sizeof(block_data::header) + nsamples * sizeof(AooSample);
        // align to 8 bytes
        nbytes = (nbytes + 7) & ~7;
        audioqueue_.resize(nbytes, nbuffers);
        // fill buffer
        for (int i = 0; i < nbuffers; ++i){
            auto b = (block_data *)audioqueue_.write_data();
            // push nominal samplerate, channel + silence
            b->header.samplerate = sr;
            b->header.channel = 0;
            std::fill(b->data, b->data + nsamples, 0);
            audioqueue_.write_commit();
        }

        // setup resampler
        resampler_.setup(decoder_->blocksize(), s.blocksize(),
                         decoder_->samplerate(), s.samplerate(),
                         decoder_->nchannels());

        // setup jitter buffer.
        // if we use a very small audio buffer size, we have to make sure that
        // we have enough space in the jitter buffer in case the source uses
        // a larger hardware buffer size and consequently sends packets in batches.
        // we don't know the actual source samplerate and hardware buffer size,
        // so we have to make a pessimistic guess.
        auto hwsamples = (double)decoder_->samplerate() / MINSAMPLERATE * MAXHWBUFSIZE;
        auto minbuffers = std::ceil(hwsamples / (double)decoder_->blocksize());
        auto jitterbufsize = std::max<int32_t>(nbuffers, minbuffers);
        // LATER optimize max. block size
        jitterbuffer_.resize(jitterbufsize, nsamples * sizeof(double));
        LOG_DEBUG("jitter buffer: " << jitterbufsize << " blocks");

        lost_since_ping_.store(0);
        channel_ = 0;
        skipblocks_ = 0;
        underrun_ = false;
        didupdate_ = true;

        // reset decoder to avoid garbage from previous stream
        decoder_->reset();
    }
}

void source_desc::invite(const sink_imp& s){
    // don't invite if already running!
    // state can change in different threads, so we need a CAS loop
    auto state = state_.load(std::memory_order_relaxed);
    while (state != source_state::run) {
        // special case: (re)invite shortly after uninvite
        if (state == source_state::uninvite){
            // update last packet time to reset timeout!
            last_packet_time_.store(s.elapsed_time());
            // force new stream, otherwise handle_start() would ignore
            // the /start messages and we would spam the source with
            // redundant invitation messages until we time out.
            // NOTE: don't use a negative value, otherwise we would get
            // a redundant "add" event, see handle_format().
            scoped_lock lock(mutex_);
            stream_id_++;
        }
    #if 1
        state_time_.store(0.0); // start immediately
    #else
        state_time_.store(s.elapsed_time()); // wait
    #endif
        if (state_.compare_exchange_weak(state, source_state::invite)){
            LOG_DEBUG("source_desc: invite");
            return;
        }
    }
    LOG_WARNING("aoo: couldn't invite source - already active");
}

void source_desc::uninvite(const sink_imp& s){
    // don't uninvite when already idle!
    // state can change in different threads, so we need a CAS loop
    auto state = state_.load(std::memory_order_relaxed);
    while (state != source_state::idle){
        // update start time for uninvite phase, see handle_data()
        state_time_.store(s.elapsed_time());
        if (state_.compare_exchange_weak(state, source_state::uninvite)){
            LOG_DEBUG("source_desc: uninvite");
            return;
        }
    }
    LOG_WARNING("aoo: couldn't uninvite source - not active");
}

AooError source_desc::request_format(const sink_imp& s, const AooFormat &f){
    if (state_.load(std::memory_order_relaxed) == source_state::uninvite){
        // requesting a format during uninvite doesn't make sense.
        // also, we couldn't use 'state_time', because it has a different
        // meaning during the uninvite phase.
        return kAooErrorUnknown;
    }

    if (!aoo::find_codec(f.codec)){
        LOG_WARNING("request_format: codec '" << f.codec << "' not supported");
        return kAooErrorUnknown;
    }

    // copy format
    auto fmt = (AooFormat *)aoo::allocate(f.size);
    memcpy(fmt, &f, f.size);

    LOG_DEBUG("source_desc: request format");

    scoped_lock lock(mutex_); // writer lock!

    format_request_.reset(fmt);

    format_time_ = s.elapsed_time();
#if 1
    state_time_.store(0.0); // start immediately
#else
    state_time_.store(s.elapsed_time()); // wait
#endif

    return kAooOk;
}

float source_desc::get_buffer_fill_ratio(){
    scoped_shared_lock lock(mutex_);
    if (decoder_){
        // consider samples in resampler!
        auto nsamples = decoder_->nchannels() * decoder_->blocksize();
        auto available = (double)audioqueue_.read_available() +
                (double)resampler_.size() / (double)nsamples;
        auto ratio = available / (double)audioqueue_.capacity();
        LOG_DEBUG("fill ratio: " << ratio << ", audioqueue: " << audioqueue_.read_available()
                  << ", resampler: " << (double)resampler_.size() / (double)nsamples);
        // FIXME sometimes the result is bigger than 1.0
        return std::min<float>(1.0, ratio);
    } else {
        return 0.0;
    }
}

// /aoo/sink/<id>/start <src> <version> <stream_id> <flags>
// <lastformat> <nchannels> <samplerate> <blocksize> <codec> <options>

AooError source_desc::handle_start(const sink_imp& s, int32_t stream, uint32_t flags,
                                   int32_t lastformat, const AooFormat& f,
                                   const AooByte *settings, int32_t size,
                                   const AooCustomData& md) {
    LOG_DEBUG("handle start");
    // if we're in 'uninvite' state, ignore /start message, see also handle_data().
    if (state_.load(std::memory_order_acquire) == source_state::uninvite){
    #if 1
        push_request(request(request_type::uninvite));
    #endif
        return kAooOk;
    }

    // ignore redundant /start messages!
    // NOTE: stream_id_ can only change in this thread,
    // so we don't need a lock to safely *read* it!
    if (stream == stream_id_){
        return kAooErrorNone;
    }

    AooFormatStorage fmt;
    std::unique_ptr<decoder> new_decoder;

    // ignore redundant /format messages!
    // NOTE: format_id_ can only change in this thread,
    // so we don't need a lock to safely *read* it!
    bool new_format = lastformat != format_id_;

    if (new_format){
        // look up codec
        auto c = aoo::find_codec(f.codec);
        if (!c){
            LOG_ERROR("codec '" << f.codec << "' not supported!");
            return kAooErrorUnknown;
        }

        // try to deserialize format

        if (c->deserialize(f, settings, size,
                           fmt.header, sizeof(fmt)) != kAooOk){
            return kAooErrorUnknown;
        }

        // Create a new decoder if necessary.
        // This is the only thread where the decoder can possibly
        // change, so we don't need a lock to safely *read* it!
        if (!decoder_ || strcmp(decoder_->name(), f.codec)){
            new_decoder = c->create_decoder(nullptr);
            if (!new_decoder){
                LOG_ERROR("couldn't create decoder!");
                return kAooErrorUnknown;
            }
        }
    }

    // copy metadata
    AooCustomData *metadata = nullptr;
    if (md.data){
        assert(md.size > 0);
        LOG_DEBUG("stream metadata: "
                  << md.type << ", " << md.size << " bytes");
        // allocate flat metadata
        auto mdsize = flat_metadata_size(md);
        metadata = (AooCustomData *)memory_.allocate(mdsize);
        flat_metadata_copy(md, *metadata);
    }

    unique_lock lock(mutex_); // writer lock!

    bool first_stream = stream_id_ == kAooIdInvalid;
    stream_id_ = stream;

    // TODO handle 'flags' (future)

    if (new_format){
        format_id_ = lastformat;
        format_request_ = nullptr;

        if (new_decoder){
            decoder_ = std::move(new_decoder);
        }

        // set format
        if (decoder_->set_format(fmt.header) != kAooOk){
            return kAooErrorUnknown;
        }
    }

    // free old metadata
    if (metadata_){
        memory_.deallocate((void *)metadata_);
    }
    // set new metadata (can be NULL!)
    metadata_ = metadata;

    // always update!
    update(s);

    lock.unlock();

    // send "add" event *before* setting the state to avoid
    // possible wrong ordering with subsequent "start" event
    if (first_stream){
        // first /start message -> source added.
        event e(kAooEventSourceAdd, ep);
        send_event(s, e, kAooThreadLevelNetwork);
        LOG_DEBUG("add new source " << ep);
    }

    // set state to "start" and check again for uninvite.
    // NOTE: state can be changed in both network threads,
    // so we need a CAS loop.
    auto state = state_.load(std::memory_order_relaxed);
    while (state != source_state::uninvite){
        if (state_.compare_exchange_weak(state, source_state::start)){
            break;
        }
    }

    if (new_format){
        // send "format" event
        event e(kAooEventFormatChange, ep);

        auto mode = s.event_mode();
        if (mode == kAooEventModeCallback){
            // use stack
            e.format.format = &fmt.header;
        } else if (kAooEventModePoll){
            // use heap
            auto f = (AooFormat *)memory_.allocate(fmt.header.size);
            memcpy(f, &fmt, fmt.header.size);
            e.format.format = f;
        }

        send_event(s, e, kAooThreadLevelNetwork);
    }

    return kAooOk;
}

// /aoo/sink/<id>/stop <src> <stream_id>

AooError source_desc::handle_stop(const sink_imp& s, int32_t stream) {
    LOG_DEBUG("handle stop");
    // ignore redundant /stop messages!
    // NOTE: stream_id_ can only change in this thread,
    // so we don't need a lock to safely *read* it!
    if (stream == stream_id_){
        state_.store(source_state::stop, std::memory_order_release);
    }

    return kAooOk;
}

// /aoo/sink/<id>/data <src> <stream_id> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

AooError source_desc::handle_data(const sink_imp& s, net_packet& d, bool binary)
{
    binary_.store(binary, std::memory_order_relaxed);

    // always update packet time to signify that we're receiving packets
    last_packet_time_.store(s.elapsed_time(), std::memory_order_relaxed);

    // if we're in uninvite state, ignore data and send uninvite request.
    if (state_.load(std::memory_order_acquire) == source_state::uninvite){
        LOG_DEBUG("handle data: uninvite");
        // only try for a certain amount of time to avoid spamming the source.
        auto delta = s.elapsed_time() - state_time_.load(std::memory_order_relaxed);
        if (delta < s.source_timeout()){
            push_request(request(request_type::uninvite));
        }
        // ignore data message
        return kAooOk;
    }

    // the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    // NOTE: stream_id_ can only change in this thread!
    if (d.stream_id != stream_id_){
        push_request(request(request_type::start));
        return kAooOk;
    }

    // synchronize with update()!
    scoped_shared_lock lock(mutex_);

#if 1
    if (!decoder_){
        LOG_DEBUG("ignore data message");
        return kAooErrorUnknown;
    }
#else
    assert(decoder_ != nullptr);
#endif
    // check and fix up samplerate
    if (d.samplerate == 0){
        // no dynamic resampling, just use nominal samplerate
        d.samplerate = decoder_->samplerate();
    }

    // copy blob data and push to queue
    auto data = (AooByte *)memory_.allocate(d.size);
    memcpy(data, d.data, d.size);
    d.data = data;

    packetqueue_.push(d);

#if AOO_DEBUG_DATA
    LOG_DEBUG("got block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << d.channel << ", totalsize = " << d.totalsize
              << ", nframes = " << d.nframes << ", frame = " << d.frame << ", size " << d.size);
#endif

    return kAooOk;
}

// /aoo/sink/<id>/ping <src> <time>

AooError source_desc::handle_ping(const sink_imp& s, time_tag tt){
    LOG_DEBUG("handle ping");

#if 0
    time_tag tt2 = s.absolute_time(); // use last stream time
#else
    time_tag tt2 = aoo::time_tag::now(); // use real system time
#endif

    // push ping reply request
    request r(request_type::ping_reply);
    r.ping.tt1 = tt;
    r.ping.tt2 = tt2;
    push_request(r);

    // push "ping" event
    event e(kAooEventPing, ep);
    e.ping.tt1 = tt;
    e.ping.tt2 = tt2;
    e.ping.tt3 = 0;
    send_event(s, e, kAooThreadLevelAudio);

    return kAooOk;
}

// only send every 50 ms! LATER we might make this settable
#define INVITE_INTERVAL 0.05

void source_desc::send(const sink_imp& s, const sendfn& fn){
    request r;
    while (requestqueue_.try_pop(r)){
        switch (r.type){
        case request_type::ping_reply:
            send_ping_reply(s, fn, r);
            break;
    #if 0
        case request_type::format:
            send_format_request(s, fn);
            break;
    #endif
        case request_type::start:
            send_start_request(s, fn);
            break;
        case request_type::uninvite:
            send_uninvitation(s, fn);
            break;
        default:
            break;
        }
    }

    auto now = s.elapsed_time();
    if ((now - state_time_.load(std::memory_order_relaxed)) >= INVITE_INTERVAL){
        // send invitations
        if (state_.load(std::memory_order_acquire) == source_state::invite){
            send_invitation(s, fn);
        }

        // the check is not really threadsafe, but it's safe because we only
        // dereference the pointer later after grabbing the mutex
        if (format_request_){
            send_format_request(s, fn);
        }

        state_time_.store(now);
    }

    send_data_requests(s, fn);
}

#define XRUN_THRESHOLD 0.1

bool source_desc::process(const sink_imp& s, AooSample **buffer,
                          int32_t nsamples, time_tag tt)
{
    // synchronize with update()!
    // the mutex should be uncontended most of the time.
    shared_lock lock(mutex_, std::try_to_lock_t{});
    if (!lock.owns_lock()) {
        if (streamstate_ == kAooStreamStateActive) {
            xrun_ += 1.0;
            LOG_VERBOSE("AooSink: source_desc::process() would block");
        } else {
            // I'm not sure if this can happen...
        }
        return false;
    }

    if (!decoder_){
        return false;
    }

    auto state = state_.load(std::memory_order_acquire);
    // handle state transitions in a CAS loop
    while (state != source_state::run) {
        if (state == source_state::start){
            // start -> run
            if (state_.compare_exchange_weak(state, source_state::run)) {
                if (streamstate_ == kAooStreamStateActive){
                #if 0
                    streamstate_ = kAooStreamStateInactive;
                #endif
                    // send missing /stop message
                    event e(kAooEventStreamStop, ep);
                    send_event(s, e, kAooThreadLevelAudio);
                }

                event e(kAooEventStreamStart, ep);
                // move metadata into event
                e.stream_start.metadata = metadata_;
                metadata_ = nullptr;

                send_event(s, e, kAooThreadLevelAudio);

                // deallocate metadata if we don't need it anymore, see also poll_events().
                if (s.event_mode() != kAooEventModePoll && e.stream_start.metadata){
                    memory_.deallocate((void *)e.stream_start.metadata);
                }

                // stream state is handled at the end of the function

                break; // continue processing
            }
        } else if (state == source_state::stop){
            // stop -> idle
            if (state_.compare_exchange_weak(state, source_state::idle)) {
                lock.unlock(); // !

                if (streamstate_ != kAooStreamStateInactive){
                    streamstate_ = kAooStreamStateInactive;

                    event e(kAooEventStreamState, ep);
                    e.stream_state.state = kAooStreamStateInactive;
                    send_event(s, e, kAooThreadLevelAudio);
                }

                event e(kAooEventStreamStop, ep);
                send_event(s, e, kAooThreadLevelAudio);

                return false;
            }
        } else if (state == source_state::uninvite){
            // don't transition into "stop" state!
            if (streamstate_ != kAooStreamStateInactive){
                lock.unlock(); // !

                streamstate_ = kAooStreamStateInactive;

                event e(kAooEventStreamState, ep);
                e.stream_state.state = kAooStreamStateInactive;
                send_event(s, e, kAooThreadLevelAudio);
            }

            return false;
        } else {
            // invite, uninvite or idle
            return false;
        }
    }

    // check for sink xruns
    if (didupdate_){
        xrunsamples_ = 0;
        xrun_ = 0;
        assert(underrun_ == false);
        assert(skipblocks_ == 0);
        didupdate_ = false;
    } else if (xrunsamples_ > 0) {
        auto xrunblocks = (float)xrunsamples_ / (float)decoder_->blocksize();
        xrun_ += xrunblocks;
        xrunsamples_ = 0;
    }

    stream_stats stats;

    if (!packetqueue_.empty()){
        // check for buffer underrun (only if packets arrive!)
        if (underrun_){
            handle_underrun(s);
        }

        net_packet d;
        while (packetqueue_.try_pop(d)){
            // check data packet
            add_packet(s, d, stats);
            // return memory
            memory_.deallocate((void *)d.data);
        }
    }

    if (skipblocks_ > 0){
        skip_blocks(s);
    }

    process_blocks(s, stats);

    check_missing_blocks(s);

#if AOO_DEBUG_JITTER_BUFFER
    DO_LOG_DEBUG(jitterbuffer_);
    DO_LOG_DEBUG("oldest: " << jitterbuffer_.last_popped()
              << ", newest: " << jitterbuffer_.last_pushed());
#endif

    if (stats.lost > 0){
        // push packet loss event
        event e(kAooEventBlockLost, ep);
        e.block_lost.count = stats.lost;
        send_event(s, e, kAooThreadLevelAudio);
    }
    if (stats.reordered > 0){
        // push packet reorder event
        event e(kAooEventBlockReordered, ep);
        e.block_reordered.count = stats.reordered;
        send_event(s, e, kAooThreadLevelAudio);
    }
    if (stats.resent > 0){
        // push packet resend event
        event e(kAooEventBlockResent, ep);
        e.block_resent.count = stats.resent;
        send_event(s, e, kAooThreadLevelAudio);
    }
    if (stats.dropped > 0){
        // push packet resend event
        event e(kAooEventBlockDropped, ep);
        e.block_dropped.count = stats.dropped;
        send_event(s, e, kAooThreadLevelAudio);
    }

    auto nchannels = decoder_->nchannels();
    auto insize = decoder_->blocksize() * nchannels;
    auto outsize = nsamples * nchannels;
    // if dynamic resampling is disabled, this will simply
    // return the nominal samplerate
    double sr = s.real_samplerate();

#if AOO_DEBUG_AUDIO_BUFFER
    // will print audio buffer and resampler balance
    get_buffer_fill_ratio();
#endif

    // try to read samples from resampler
    auto buf = (AooSample *)alloca(outsize * sizeof(AooSample));

    while (!resampler_.read(buf, outsize)){
        // try to write samples from buffer into resampler
        if (audioqueue_.read_available()){
            auto d = (block_data *)audioqueue_.read_data();

            if (xrun_ > XRUN_THRESHOLD){
                // skip audio and decrement xrun counter proportionally
                xrun_ -= sr / decoder_->samplerate();
            } else {
                // try to write audio into resampler
                if (resampler_.write(d->data, insize)){
                    // update resampler
                    resampler_.update(d->header.samplerate, sr);
                    // set channel; negative = current
                    if (d->header.channel >= 0){
                        channel_ = d->header.channel;
                    }
                } else {
                    LOG_ERROR("bug: couldn't write to resampler");
                    // let the buffer run out
                }
            }

            audioqueue_.read_commit();
        } else {
            // buffer ran out -> "inactive"
            if (streamstate_ != kAooStreamStateInactive){
                streamstate_ = kAooStreamStateInactive;

                event e(kAooEventStreamState, ep);
                e.stream_state.state = kAooStreamStateInactive;
                send_event(s, e, kAooThreadLevelAudio);
            }
            underrun_ = true;

            return false;
        }
    }

    // sum source into sink (interleaved -> non-interleaved),
    // starting at the desired sink channel offset.
    // out of bound source channels are silently ignored.
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

    // LOG_DEBUG("read samples from source " << id_);

    if (streamstate_ != kAooStreamStateActive){
        streamstate_ = kAooStreamStateActive;

        event e(kAooEventStreamState, ep);
        e.stream_state.state = kAooStreamStateActive;
        send_event(s, e, kAooThreadLevelAudio);
    }

    return true;
}

int32_t source_desc::poll_events(sink_imp& s, AooEventHandler fn, void *user){
    // always lockfree!
    int count = 0;
    event e;
    while (eventqueue_.try_pop(e)){
        fn(user, &e.event_, kAooThreadLevelUnknown);
        // some events use dynamic memory
        free_event_data(e);
        count++;
    }
    return count;
}

void source_desc::add_lost(stream_stats& stats, int32_t n) {
    stats.lost += n;
    lost_since_ping_.fetch_add(n, std::memory_order_relaxed);
}

#define SILENT_REFILL 0
#define SKIP_BLOCKS 0

void source_desc::handle_underrun(const sink_imp& s){
    LOG_VERBOSE("audio buffer underrun");

    int32_t n = audioqueue_.write_available();
    auto nsamples = decoder_->blocksize() * decoder_->nchannels();
    // reduce by blocks in resampler!
    n -= static_cast<int32_t>((double)resampler_.size() / (double)nsamples + 0.5);

    LOG_DEBUG("audioqueue: " << audioqueue_.read_available()
              << ", resampler: " << (double)resampler_.size() / (double)nsamples);

    if (n > 0){
        double sr = decoder_->samplerate();
        for (int i = 0; i < n; ++i){
            auto b = (block_data *)audioqueue_.write_data();
            // push nominal samplerate, channel + silence
            b->header.samplerate = sr;
            b->header.channel = -1; // last channel
        #if SILENT_REFILL
            // push silence
            std::fill(b->data, b->data + nsamples, 0);
        #else
            // use packet loss concealment
            AooInt32 size = nsamples;
            if (decoder_->decode(nullptr, 0, b->data, size) != kAooOk){
                LOG_WARNING("AooSink: couldn't decode block!");
                // fill with zeros
                std::fill(b->data, b->data + nsamples, 0);
            }
        #endif
            audioqueue_.write_commit();
        }

        LOG_DEBUG("write " << n << " empty blocks to audio buffer");

    #if SKIP_BLOCKS
        skipblocks_ += n;

        LOG_DEBUG("skip next " << n << " blocks");
    #endif
    }

    event e(kAooEventBufferUnderrun, ep);
    send_event(s, e, kAooThreadLevelAudio);

    underrun_ = false;
}

bool source_desc::add_packet(const sink_imp& s, const net_packet& d,
                             stream_stats& stats){
    // we have to check the stream_id (again) because the stream
    // might have changed in between!
    if (d.stream_id != stream_id_){
        LOG_DEBUG("ignore data packet from previous stream");
        return false;
    }

    if (d.sequence <= jitterbuffer_.last_popped()){
        // block too old, discard!
        LOG_VERBOSE("discard old block " << d.sequence);
        LOG_DEBUG("oldest: " << jitterbuffer_.last_popped());
        return false;
    }

    // check for large gap between incoming block and most recent block
    // (either network problem or stream has temporarily stopped.)
    auto newest = jitterbuffer_.last_pushed();
    auto diff = d.sequence - newest;
    if (newest >= 0 && diff > jitterbuffer_.capacity()){
        // jitter buffer should be empty.
        if (!jitterbuffer_.empty()){
            LOG_VERBOSE("source_desc: transmission gap, but jitter buffer is not empty");
            jitterbuffer_.clear();
        }
        // we don't need to skip blocks!
        skipblocks_ = 0;
        // No need to refill, because audio buffer should have ran out.
        if (audioqueue_.write_available()){
            LOG_VERBOSE("source_desc: transmission gap, but audio buffer is not empty");
        }
        // report gap to source
        lost_since_ping_.fetch_add(diff - 1);
        // send event
        event e(kAooEventBlockLost, ep);
        e.block_lost.count = diff - 1;
        send_event(s, e, kAooThreadLevelAudio);
    }

    auto block = jitterbuffer_.find(d.sequence);
    if (!block){
    #if 1
        // can this ever happen!?
        if (d.sequence <= newest){
            LOG_VERBOSE("discard outdated block " << d.sequence);
            LOG_DEBUG("newest: " << newest);
            return false;
        }
    #endif

        if (newest >= 0){
            // notify for gap
            if (diff > 1){
                LOG_VERBOSE("skipped " << (diff - 1) << " blocks");
            }

            // check for jitter buffer overrun
            // can happen if the sink blocks for a longer time
            // or with extreme network jitter (packets have piled up)
        try_again:
            auto space = jitterbuffer_.capacity() - jitterbuffer_.size();
            if (diff > space){
                if (skipblocks_ > 0){
                    LOG_DEBUG("jitter buffer would overrun!");
                    skip_blocks(s);
                    goto try_again;
                } else {
                    // for now, just clear the jitter buffer and let the
                    // audio buffer underrun.
                    LOG_VERBOSE("jitter buffer overrun!");
                    jitterbuffer_.clear();

                    newest = d.sequence; // !
                }
            }

            // fill gaps with empty blocks
            for (int32_t i = newest + 1; i < d.sequence; ++i){
                jitterbuffer_.push_back(i)->init(i, false);
            }
        }

        // add new block
        block = jitterbuffer_.push_back(d.sequence);

        if (d.totalsize == 0){
            // dropped block
            block->init(d.sequence, true);
            return true;
        } else {
            block->init(d.sequence, d.samplerate,
                        d.channel, d.totalsize, d.nframes);
        }
    } else {
        if (d.totalsize == 0){
            if (!block->dropped()){
                // dropped block arrived out of order
                LOG_VERBOSE("empty block " << d.sequence << " out of order");
                block->init(d.sequence, true); // don't call before dropped()!
                return true;
            } else {
                LOG_VERBOSE("empty block " << d.sequence << " already received");
                return false;
            }
        }

        if (block->num_frames() == 0){
            // placeholder block
            block->init(d.sequence, d.samplerate,
                        d.channel, d.totalsize, d.nframes);
        } else if (block->has_frame(d.frame)){
            // frame already received
            LOG_VERBOSE("frame " << d.frame << " of block " << d.sequence << " already received");
            return false;
        }

        if (d.sequence != newest){
            // out of order or resent
            if (block->resend_count() > 0){
                LOG_VERBOSE("resent frame " << d.frame << " of block " << d.sequence);
                stats.resent++;
            } else {
                LOG_VERBOSE("frame " << d.frame << " of block " << d.sequence << " out of order!");
                stats.reordered++;
            }
        }
    }

    // add frame to block
    block->add_frame(d.frame, d.data, d.size);

    return true;
}

void source_desc::process_blocks(const sink_imp& s, stream_stats& stats){
    if (jitterbuffer_.empty()){
        return;
    }

    auto nsamples = decoder_->blocksize() * decoder_->nchannels();

    // Transfer all consecutive complete blocks
    while (!jitterbuffer_.empty() && audioqueue_.write_available()){
        const AooByte *data;
        int32_t size;
        double sr;
        int32_t channel;

        auto& b = jitterbuffer_.front();
        if (b.complete()){
            if (b.dropped()){
                data = nullptr;
                size = 0;
                sr = decoder_->samplerate(); // nominal samplerate
                channel = -1; // current channel
            #if AOO_DEBUG_JITTER_BUFFER
                DO_LOG_DEBUG("jitter buffer: write empty block ("
                          << b.sequence << ") for source xrun");
            #endif
                // record dropped block
                stats.dropped++;
            } else {
                // block is ready
                data = b.data();
                size = b.size();
                sr = b.samplerate; // real samplerate
                channel = b.channel;
            #if AOO_DEBUG_JITTER_BUFFER
                DO_LOG_DEBUG("jitter buffer: write samples for block ("
                          << b.sequence << ")");
            #endif
            }
        } else {
            // we also have to consider the content of the resampler!
            auto remaining = audioqueue_.read_available() + resampler_.size() / nsamples;
            if (remaining < minblocks_){
                // we need audio, so we have to drop a block
                LOG_DEBUG("remaining: " << remaining << " / " << audioqueue_.capacity()
                          << ", limit: " << minblocks_);
                data = nullptr;
                size = 0;
                sr = decoder_->samplerate(); // nominal samplerate
                channel = -1; // current channel
                add_lost(stats, 1);
                LOG_VERBOSE("dropped block " << b.sequence);
            } else {
                // wait for block
            #if AOO_DEBUG_JITTER_BUFFER
                DO_LOG_DEBUG("jitter buffer: wait");
            #endif
                break;
            }
        }

        // push samples and channel
        auto d = (block_data *)audioqueue_.write_data();
        d->header.samplerate = sr;
        d->header.channel = channel;
        // decode and push audio data
        AooInt32 n = nsamples;
        if (decoder_->decode(data, size, d->data, n) != kAooOk){
            LOG_WARNING("AooSink: couldn't decode block!");
            // decoder failed - fill with zeros
            std::fill(d->data, d->data + nsamples, 0);
        }
        audioqueue_.write_commit();

    #if 0
        Log log;
        for (int i = 0; i < nsamples; ++i){
            log << d->data[i] << " ";
        }
    #endif

        jitterbuffer_.pop_front();
    }
}

void source_desc::skip_blocks(const sink_imp& s){
    auto n = std::min<int>(skipblocks_, jitterbuffer_.size());
    LOG_VERBOSE("skip " << n << " blocks");
    while (n--){
        jitterbuffer_.pop_front();
    }
}

// /aoo/src/<id>/data <sink> <stream_id> <seq0> <frame0> <seq1> <frame1> ...

// deal with "holes" in block queue
void source_desc::check_missing_blocks(const sink_imp& s){
    // only check if it has more than a single pending block!
    if (jitterbuffer_.size() <= 1 || !s.resend_enabled()){
        return;
    }
    int32_t resent = 0;
    int32_t maxnumframes = s.resend_limit();
    double interval = s.resend_interval();
    double elapsed = s.elapsed_time();

    // resend incomplete blocks except for the last block
    auto n = jitterbuffer_.size() - 1;
    for (auto b = jitterbuffer_.begin(); n--; ++b){
        if (!b->complete() && b->update(elapsed, interval)){
            auto nframes = b->num_frames();

            if (b->count_frames() > 0){
                // only some frames missing
                for (int i = 0; i < nframes; ++i){
                    if (!b->has_frame(i)){
                        if (resent < maxnumframes){
                            push_data_request({ b->sequence, i });
                        #if 0
                            DO_LOG_DEBUG("request " << b->sequence << " (" << i << ")");
                        #endif
                            resent++;
                        } else {
                            goto resend_done;
                        }
                    }
                }
            } else {
                // all frames missing
                if (resent + nframes <= maxnumframes){
                    push_data_request({ b->sequence, -1 }); // whole block
                #if 0
                    DO_LOG_DEBUG("request " << b->sequence << " (all)");
                #endif
                    resent += nframes;
                } else {
                    goto resend_done;
                }
            }
        }
    }
resend_done:

    assert(resent <= maxnumframes);
    if (resent > 0){
        LOG_DEBUG("requested " << resent << " frames");
    }
}

// /aoo/<id>/ping <sink>
// called without lock!
void source_desc::send_ping_reply(const sink_imp &s, const sendfn &fn,
                                  const request& r){
    LOG_DEBUG("send " kAooMsgPing " to " << ep);

    auto lost_blocks = lost_since_ping_.exchange(0);

    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSourceLen + 16 + kAooMsgPingLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             kAooMsgDomain, kAooMsgSource, ep.id, kAooMsgPing);

    msg << osc::BeginMessage(address) << s.id()
        << osc::TimeTag(r.ping.tt1)
        << osc::TimeTag(r.ping.tt2)
        << lost_blocks
        << osc::EndMessage;

    fn((const AooByte *)msg.Data(), msg.Size(), ep);
}

// /aoo/src/<id>/start <sink>
// called without lock!
void source_desc::send_start_request(const sink_imp& s, const sendfn& fn) {
    LOG_VERBOSE("request " kAooMsgStart " for source " << ep);

    AooByte buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg((char *)buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen +
            kAooMsgSourceLen + 16 + kAooMsgStartLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             kAooMsgDomain, kAooMsgSource, ep.id, kAooMsgStart);

    msg << osc::BeginMessage(address) << s.id()
        << (int32_t)make_version() << osc::EndMessage;

    fn((const AooByte *)msg.Data(), msg.Size(), ep);
}

// /aoo/src/<id>/format <sink> <version> <stream>
// <numchannels> <samplerate> <blocksize> <codec> <options>
// called without lock!
void source_desc::send_format_request(const sink_imp& s, const sendfn& fn) {
    LOG_VERBOSE("request " kAooMsgFormat " for source " << ep);

    AooByte buf[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg((char *)buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen +
            kAooMsgSourceLen + 16 + kAooMsgFormatLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             kAooMsgDomain, kAooMsgSource, ep.id, kAooMsgFormat);

    shared_lock lock(mutex_); // !
    // check again!
    if (!format_request_){
        return;
    }

    auto delta = s.elapsed_time() - format_time_;
    // for now just reuse source timeout
    if (delta < s.source_timeout()){
        auto stream = stream_id_;

        auto& f = *format_request_;

        auto c = aoo::find_codec(f.codec);
        assert(c != nullptr);

        AooByte buf[kAooCodecMaxSettingSize];
        AooInt32 size;
        if (c->serialize(f, buf, size) == kAooOk){
            LOG_DEBUG("codec = " << f.codec << ", nchannels = "
                      << f.numChannels << ", sr = " << f.sampleRate
                      << ", blocksize = " << f.blockSize
                      << ", option size = " << size);

            // send without lock!
            lock.unlock();

            msg << osc::BeginMessage(address) << s.id()
                << (int32_t)make_version() << stream
                << f.numChannels << f.sampleRate << f.blockSize
                << f.codec << osc::Blob(buf, size)
                << osc::EndMessage;

            fn((const AooByte *)msg.Data(), msg.Size(), ep);
        }
    } else {
        LOG_DEBUG("format request timeout");

        event e(kAooEventFormatTimeout, ep);

        send_event(s, e, kAooThreadLevelAudio);

        // clear request
        // this is safe even with a reader lock,
        // because elsewhere it is always read/written
        // with a writer lock, see request_format()
        // and handle_format().
        format_request_ = nullptr;
    }
}

// /aoo/src/<id>/data <id> <stream_id> <seq1> <frame1> <seq2> <frame2> etc.
// or
// (header), id (int32), stream_id (int32), count (int32),
// seq1 (int32), frame1(int32), seq2(int32), frame2(seq), etc.

void source_desc::send_data_requests(const sink_imp& s, const sendfn& fn){
    if (datarequestqueue_.empty()){
        return;
    }

    shared_lock lock(mutex_);
    int32_t stream_id = stream_id_; // cache!
    lock.unlock();

    AooByte buf[AOO_MAX_PACKET_SIZE];

    if (binary_.load(std::memory_order_relaxed)){
        const int32_t maxdatasize = s.packetsize()
                - (kAooBinMsgHeaderSize + 8); // id + stream_id
        const int32_t maxrequests = maxdatasize / 8; // 2 * int32
        int32_t numrequests = 0;

        auto it = buf;
        // write header
        memcpy(it, kAooBinMsgDomain, kAooBinMsgDomainSize);
        it += kAooBinMsgDomainSize;
        aoo::write_bytes<int16_t>(kAooTypeSource, it);
        aoo::write_bytes<int16_t>(kAooBinMsgCmdData, it);
        aoo::write_bytes<int32_t>(ep.id, it);
        // write first 2 args (constant)
        aoo::write_bytes<int32_t>(s.id(), it);
        aoo::write_bytes<int32_t>(stream_id, it);
        // skip 'count' field
        it += sizeof(int32_t);

        auto head = it;

        data_request r;
        while (datarequestqueue_.try_pop(r)){
            LOG_DEBUG("send binary data request ("
                      << r.sequence << " " << r.frame << ")");

            aoo::write_bytes<int32_t>(r.sequence, it);
            aoo::write_bytes<int32_t>(r.frame, it);
            if (++numrequests >= maxrequests){
                // write 'count' field
                aoo::to_bytes(numrequests, head - sizeof(int32_t));
                // send it off
                fn(buf, it - buf, ep);
                // prepare next message (just rewind)
                it = head;
                numrequests = 0;
            }
        }

        if (numrequests > 0){
            // write 'count' field
            aoo::to_bytes(numrequests, head - sizeof(int32_t));
            // send it off
            fn(buf, it - buf, ep);
        }
    } else {
        char buf[AOO_MAX_PACKET_SIZE];
        osc::OutboundPacketStream msg(buf, sizeof(buf));

        // make OSC address pattern
        const int32_t maxaddrsize = kAooMsgDomainLen +
                kAooMsgSourceLen + 16 + kAooMsgDataLen;
        char pattern[maxaddrsize];
        snprintf(pattern, sizeof(pattern), "%s%s/%d%s",
                 kAooMsgDomain, kAooMsgSource, ep.id, kAooMsgData);

        const int32_t maxdatasize = s.packetsize() - maxaddrsize - 16; // id + stream_id + padding
        const int32_t maxrequests = maxdatasize / 10; // 2 * (int32_t + typetag)
        int32_t numrequests = 0;

        msg << osc::BeginMessage(pattern) << s.id() << stream_id;

        data_request r;
        while (datarequestqueue_.try_pop(r)){
            LOG_DEBUG("send data request (" << r.sequence
                      << " " << r.frame << ")");

            msg << r.sequence << r.frame;
            if (++numrequests >= maxrequests){
                // send it off
                msg << osc::EndMessage;

                fn((const AooByte *)msg.Data(), msg.Size(), ep);

                // prepare next message
                msg.Clear();
                msg << osc::BeginMessage(pattern) << s.id() << stream_id;
                numrequests = 0;
            }
        }

        if (numrequests > 0){
            // send it off
            msg << osc::EndMessage;

            fn((const AooByte *)msg.Data(), msg.Size(), ep);
        }
    }
}

// /aoo/src/<id>/invite <sink>

// called without lock!
void source_desc::send_invitation(const sink_imp& s, const sendfn& fn){
    LOG_DEBUG("send " kAooMsgInvite " to source " << ep);

    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSourceLen + 16 + kAooMsgInviteLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             kAooMsgDomain, kAooMsgSource, ep.id, kAooMsgInvite);

    msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

    fn((const AooByte *)msg.Data(), msg.Size(), ep);
}

// /aoo/<id>/uninvite <sink>

// called without lock!
void source_desc::send_uninvitation(const sink_imp& s, const sendfn &fn){
    LOG_DEBUG("send " kAooMsgUninvite " to source " << ep);

    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = kAooMsgDomainLen
            + kAooMsgSourceLen + 16 + kAooMsgUninviteLen;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             kAooMsgDomain, kAooMsgSource, ep.id, kAooMsgUninvite);

    msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

    fn((const AooByte *)msg.Data(), msg.Size(), ep);
}

void source_desc::send_event(const sink_imp& s, const event& e,
                             AooThreadLevel level){
    switch (s.event_mode()){
    case kAooEventModePoll:
        eventqueue_.push(e);
        break;
    case kAooEventModeCallback:
        s.call_event(e, level);
        break;
    default:
        break;
    }
}

void source_desc::free_event_data(const event &e){
    if (e.type_ == kAooEventFormatChange){
        memory_.deallocate((void *)e.format.format);
    } else if (e.type_ == kAooEventStreamStart){
        if (e.stream_start.metadata){
            memory_.deallocate((void *)e.stream_start.metadata);
        }
    }
}

} // aoo
