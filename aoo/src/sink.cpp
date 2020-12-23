/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "sink.hpp"

#include <algorithm>
#include <cmath>

/*//////////////////// aoo_sink /////////////////////*/

aoo_sink * aoo_sink_new(aoo_id id, uint32_t flags) {
    return aoo::construct<aoo::sink>(id, flags);
}

aoo::sink::sink(aoo_id id, uint32_t flags)
    : id_(id) {
    eventqueue_.reserve(AOO_EVENTQUEUESIZE);
}

void aoo_sink_free(aoo_sink *sink) {
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::sink *>(sink));
}

aoo_error aoo_sink_setup(aoo_sink *sink, int32_t samplerate,
                         int32_t blocksize, int32_t nchannels) {
    return sink->setup(samplerate, blocksize, nchannels);
}

aoo_error aoo::sink::setup(int32_t samplerate,
                           int32_t blocksize, int32_t nchannels){
    if (samplerate > 0 && blocksize > 0 && nchannels > 0)
    {
        if (samplerate != samplerate_ || blocksize != blocksize_ ||
            nchannels != nchannels_)
        {
            nchannels_ = nchannels;
            samplerate_ = samplerate;
            blocksize_ = blocksize;

            buffer_.resize(blocksize_ * nchannels_);

            // reset timer + time DLL filter
            timer_.setup(samplerate_, blocksize_);

            reset_sources();
        }
        return AOO_OK;
    }
    return AOO_ERROR_UNSPECIFIED;
}

aoo_error aoo_sink_invite_source(aoo_sink *sink, const void *address,
                                 int32_t addrlen, aoo_id id)
{
    return sink->invite_source(address, addrlen, id);
}

// LATER put invitations on a queue
aoo_error aoo::sink::invite_source(const void *address, int32_t addrlen, aoo_id id){
    ip_address addr((const sockaddr *)address, addrlen);

    push_request(source_request { request_type::invite, addr, id });

    return AOO_OK;
}

aoo_error aoo_sink_uninvite_source(aoo_sink *sink, const void *address,
                                   int32_t addrlen, aoo_id id)
{
    return sink->uninvite_source(address, addrlen, id);
}

// LATER put uninvitations on a queue
aoo_error aoo::sink::uninvite_source(const void *address, int32_t addrlen, aoo_id id){
    ip_address addr((const sockaddr *)address, addrlen);

    push_request(source_request { request_type::uninvite, addr, id });

    return AOO_OK;
}

aoo_error aoo_sink_uninvite_all(aoo_sink *sink){
    return sink->uninvite_all();
}

aoo_error aoo::sink::uninvite_all(){
    push_request(source_request { request_type::uninvite_all });

    return AOO_OK;
}

namespace aoo {

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

} // aoo

#define CHECKARG(type) assert(size == sizeof(type))

aoo_error aoo_sink_set_option(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->set_option(opt, p, size);
}

aoo_error aoo::sink::set_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // id
    case AOO_OPT_ID:
    {
        CHECKARG(int32_t);
        auto newid = as<int32_t>(ptr);
        if (id_.exchange(newid) != newid){
            // LATER clear source list here
        }
        break;
    }
    // reset
    case AOO_OPT_RESET:
        reset_sources();
        // reset time DLL
        timer_.reset();
        break;
    // buffer size
    case AOO_OPT_BUFFERSIZE:
    {
        CHECKARG(int32_t);
        auto bufsize = std::max<int32_t>(0, as<int32_t>(ptr));
        if (bufsize != buffersize_){
            buffersize_.store(bufsize);
            reset_sources();
        }
        break;
    }
    // timefilter bandwidth
    case AOO_OPT_TIMEFILTER_BANDWIDTH:
    {
        CHECKARG(float);
        auto bw = std::max<double>(0, std::min<double>(1, as<float>(ptr)));
        bandwidth_.store(bw);
        timer_.reset(); // will update time DLL and reset timer
        break;
    }
    // packetsize
    case AOO_OPT_PACKETSIZE:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = 64;
        auto packetsize = as<int32_t>(ptr);
        if (packetsize < minpacketsize){
            LOG_WARNING("packet size too small! setting to " << minpacketsize);
            packetsize_.store(minpacketsize);
        } else if (packetsize > AOO_MAXPACKETSIZE){
            LOG_WARNING("packet size too large! setting to " << AOO_MAXPACKETSIZE);
            packetsize_.store(AOO_MAXPACKETSIZE);
        } else {
            packetsize_.store(packetsize);
        }
        break;
    }
    // resend limit
    case AOO_OPT_RESEND_ENABLE:
        CHECKARG(int32_t);
        resend_enabled_.store(as<int32_t>(ptr));
        break;
    // resend interval
    case AOO_OPT_RESEND_INTERVAL:
    {
        CHECKARG(int32_t);
        auto interval = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        resend_interval_.store(interval);
        break;
    }
    // resend maxnumframes
    case AOO_OPT_RESEND_MAXNUMFRAMES:
    {
        CHECKARG(int32_t);
        auto maxnumframes = std::max<int32_t>(1, as<int32_t>(ptr));
        resend_maxnumframes_.store(maxnumframes);
        break;
    }
    // source timeout
    case AOO_OPT_SOURCE_TIMEOUT:
    {
        CHECKARG(int32_t);
        auto timeout = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        source_timeout_.store(timeout);
        break;
    }
    // unknown
    default:
        LOG_WARNING("aoo_sink: unsupported option " << opt);
        return AOO_ERROR_UNSPECIFIED;
    }
    return AOO_OK;
}

aoo_error aoo_sink_get_option(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->get_option(opt, p, size);
}

aoo_error aoo::sink::get_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // id
    case AOO_OPT_ID:
        as<aoo_id>(ptr) = id();
        break;
    // buffer size
    case AOO_OPT_BUFFERSIZE:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = buffersize_.load();
        break;
    // timefilter bandwidth
    case AOO_OPT_TIMEFILTER_BANDWIDTH:
        CHECKARG(float);
        as<float>(ptr) = bandwidth_.load();
        break;
    // resend packetsize
    case AOO_OPT_PACKETSIZE:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packetsize_.load();
        break;
    // resend limit
    case AOO_OPT_RESEND_ENABLE:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_enabled_.load();
        break;
    // resend interval
    case AOO_OPT_RESEND_INTERVAL:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_interval_.load() * 1000.0;
        break;
    // resend maxnumframes
    case AOO_OPT_RESEND_MAXNUMFRAMES:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_maxnumframes_.load();
        break;
    case AOO_OPT_SOURCE_TIMEOUT:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = source_timeout_.load() * 1000.0;
        break;
    // unknown
    default:
        LOG_WARNING("aoo_sink: unsupported option " << opt);
        return AOO_ERROR_UNSPECIFIED;
    }
    return AOO_OK;
}

aoo_error aoo_sink_set_source_option(aoo_sink *sink, const void *address, int32_t addrlen,
                                    aoo_id id, int32_t opt, void *p, int32_t size)
{
    return sink->set_source_option(address, addrlen, id, opt, p, size);
}

aoo_error aoo::sink::set_source_option(const void *address, int32_t addrlen, aoo_id id,
                                      int32_t opt, void *ptr, int32_t size)
{
    ip_address addr((const sockaddr *)address, addrlen);

    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        switch (opt){
        // reset
        case AOO_OPT_RESET:
            src->reset(*this);
            break;
        // unsupported
        default:
            LOG_WARNING("aoo_sink: unsupported source option " << opt);
            return AOO_ERROR_UNSPECIFIED;
        }
        return AOO_OK;
    } else {
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error aoo_sink_get_source_option(aoo_sink *sink, const void *address, int32_t addrlen,
                                    aoo_id id, int32_t opt, void *p, int32_t size)
{
    return sink->get_source_option(address, addrlen, id, opt, p, size);
}

aoo_error aoo::sink::get_source_option(const void *address, int32_t addrlen, aoo_id id,
                                      int32_t opt, void *ptr, int32_t size)
{
    ip_address addr((const sockaddr *)address, addrlen);

    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        switch (opt){
        // format
        case AOO_OPT_FORMAT:
        {
            assert(size >= sizeof(aoo_format));
            auto fmt = as<aoo_format>(ptr);
            fmt.size = size; // !
            return src->get_format(fmt);
        }
        // unsupported
        default:
            LOG_WARNING("aoo_sink: unsupported source option " << opt);
            return AOO_ERROR_UNSPECIFIED;
        }
        return AOO_OK;
    } else {
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error aoo_sink_handle_message(aoo_sink *sink, const char *data, int32_t n,
                                  const void *address, int32_t addrlen) {
    return sink->handle_message(data, n, address, addrlen);
}

aoo_error aoo::sink::handle_message(const char *data, int32_t n,
                                    const void *address, int32_t addrlen) {
    if (!data){
        return decode();
    }

    try {
        ip_address addr((const sockaddr *)address, addrlen);

        osc::ReceivedPacket packet(data, n);
        osc::ReceivedMessage msg(packet);

        if (samplerate_ == 0){
            return AOO_ERROR_UNSPECIFIED; // not setup yet
        }

        aoo_type type;
        aoo_id sinkid;
        int32_t onset;
        auto err = aoo_parse_pattern(data, n, &type, &sinkid, &onset);
        if (err != AOO_OK){
            LOG_WARNING("not an AoO message!");
            return AOO_ERROR_UNSPECIFIED;
        }
        if (type != AOO_TYPE_SINK){
            LOG_WARNING("not a sink message!");
            return AOO_ERROR_UNSPECIFIED;
        }
        if (sinkid != id()){
            LOG_WARNING("wrong sink ID!");
            return AOO_ERROR_UNSPECIFIED;
        }

        auto pattern = msg.AddressPattern() + onset;
        if (!strcmp(pattern, AOO_MSG_FORMAT)){
            return handle_format_message(msg, addr);
        } else if (!strcmp(pattern, AOO_MSG_DATA)){
            return handle_data_message(msg, addr);
        } else if (!strcmp(pattern, AOO_MSG_PING)){
            return handle_ping_message(msg, addr);
        } else {
            LOG_WARNING("unknown message " << pattern);
        }
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_sink: exception in handle_message: " << e.what());
    }
    return AOO_ERROR_UNSPECIFIED;
}

aoo_error aoo_sink_send(aoo_sink *sink, aoo_sendfn fn, void *user){
    return sink->send(fn, user);
}

aoo_error aoo::sink::send(aoo_sendfn fn, void *user){
    sendfn func(fn, user);

    source_lock lock(sources_);
    for (auto& s: sources_){
        s.send(*this, func);
    }
    return AOO_OK;
}

aoo_error aoo::sink::decode() {

    source_lock lock(sources_);
    for (auto& s : sources_){
        s.decode(*this);
    }
    lock.unlock();

    // free unused source_descs
    if (!sources_.try_free()){
        LOG_DEBUG("aoo::sink: try_free() would block");
    }

    // handle requests
    // NOTE: we invite/uninvite sources in the same thread
    // where we add sources, so we can get away with holding
    // a reader lock without any ABA problem.
    source_request r;
    while (requestqueue_.try_pop(r)){
        switch (r.type) {
        case request_type::invite:
        {
            // try to find existing source
            // we might want to invite an existing source,
            // e.g. when it is currently uninviting
            source_lock lock(sources_);
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

    return AOO_OK;
}

aoo_error aoo_sink_process(aoo_sink *sink, aoo_sample **data,
                           int32_t nsamples, uint64_t t) {
    return sink->process(data, nsamples, t);
}

#define AOO_MAXNUMEVENTS 256

aoo_error aoo::sink::process(aoo_sample **data, int32_t nsamples, uint64_t t){
    std::fill(buffer_.begin(), buffer_.end(), 0);

    // update time DLL filter
    double error;
    auto state = timer_.update(t, error);
    if (state == timer::state::reset){
        LOG_DEBUG("setup time DLL filter for sink");
        dll_.setup(samplerate_, blocksize_, bandwidth_, 0);
    } else if (state == timer::state::error){
        // recover sources
        int32_t xrunsamples = error * samplerate_ + 0.5;

        // no lock needed - sources are only removed in this thread!
        for (auto& s : sources_){
            s.add_xrun(xrunsamples);
        }
        timer_.reset();
    } else {
        auto elapsed = timer_.get_elapsed();
        dll_.update(elapsed);
    #if AOO_DEBUG_DLL
        DO_LOG("time elapsed: " << elapsed << ", period: " << dll_.period()
               << ", samplerate: " << dll_.samplerate());
    #endif
    }

    bool didsomething = false;

    // no lock needed - sources are only removed in this thread!
    for (auto it = sources_.begin(); it != sources_.end();){
        if (it->process(*this, buffer_.data(), blocksize_, t)){
            didsomething = true;
        } else if (!it->is_active(*this)){
            // move source to garbage list (will be freed in decode())
            if (it->is_inviting()){
                LOG_VERBOSE("aoo::sink: invitation for " << it->address().name()
                            << " " << it->address().port() << " timed out");
                source_event e(AOO_INVITE_TIMEOUT_EVENT, *it);
                push_event(e);
            } else {
                LOG_VERBOSE("aoo::sink: removed inactive source " << it->address().name()
                            << " " << it->address().port());
                source_event e(AOO_SOURCE_REMOVE_EVENT, *it);
                push_event(e);
            }
            it =  sources_.erase(it);
            continue;
        }
        ++it;
    }

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
        // copy buffers
        for (int i = 0; i < nchannels_; ++i){
            auto buf = &buffer_[i * blocksize_];
            std::copy(buf, buf + blocksize_, data[i]);
        }
        return AOO_OK;
    } else {
        return AOO_ERROR_UNSPECIFIED;
    }
}
aoo_bool aoo_sink_events_available(aoo_sink *sink){
    return sink->events_available();
}

bool aoo::sink::events_available(){
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

aoo_error aoo_sink_poll_events(aoo_sink *sink,
                               aoo_eventhandler fn, void *user){
    return sink->poll_events(fn, user);
}

#define EVENT_THROTTLE 1000

aoo_error aoo::sink::poll_events(aoo_eventhandler fn, void *user){
    if (!fn){
        return AOO_ERROR_UNSPECIFIED;
    }
    int total = 0;
    source_event e;
    while (eventqueue_.try_pop(e)){
        aoo_source_event se;
        se.type = e.type;
        se.address = e.address.address();
        se.addrlen = e.address.length();
        se.id = e.id;
        fn(user, (const aoo_event *)&se);
        total++;
    }
    // we only need to protect against source removal
    source_lock lock(sources_);
    for (auto& src : sources_){
        total += src.poll_events(fn, user);
        if (total > EVENT_THROTTLE){
            break;
        }
    }
    return AOO_OK;
}

namespace aoo {

// must be called with source_mutex_ locked!
aoo::source_desc * sink::find_source(const ip_address& addr, aoo_id id){
    for (auto& src : sources_){
        if (src.match(addr, id)){
            return &src;
        }
    }
    return nullptr;
}

source_desc * sink::add_source(const ip_address& addr, aoo_id id){
    // add new source
    sources_.emplace_front(addr, id, elapsed_time());
    return &sources_.front();
}

void sink::reset_sources(){
    source_lock lock(sources_);
    for (auto& src : sources_){
        src.reset(*this);
    }
}

aoo_error sink::handle_format_message(const osc::ReceivedMessage& msg,
                                      const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    aoo_id id = (it++)->AsInt32();
    int32_t version = (it++)->AsInt32();

    // LATER handle this in the source_desc (e.g. ignoring further messages)
    if (!check_version(version)){
        LOG_ERROR("aoo_sink: source version not supported");
        return AOO_ERROR_UNSPECIFIED;
    }

    int32_t salt = (it++)->AsInt32();
    // get format from arguments
    aoo_format f;
    f.nchannels = (it++)->AsInt32();
    f.samplerate = (it++)->AsInt32();
    f.blocksize = (it++)->AsInt32();
    f.codec = (it++)->AsString();
    f.size = sizeof(aoo_format);
    const void *settings;
    osc::osc_bundle_element_size_t size;
    (it++)->AsBlob(settings, size);

    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_MSG_FORMAT << " message");
        return AOO_ERROR_UNSPECIFIED;
    }
    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id);
    }
    return src->handle_format(*this, salt, f, (const char *)settings, size);
}

aoo_error sink::handle_data_message(const osc::ReceivedMessage& msg,
                                    const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

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

    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_MSG_DATA << " message");
        return AOO_ERROR_UNSPECIFIED;
    }
    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id);
    }
    return src->handle_data(*this, salt, d);
}

aoo_error sink::handle_ping_message(const osc::ReceivedMessage& msg,
                                    const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    time_tag tt = (it++)->AsTimeTag();

    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_MSG_PING << " message");
        return AOO_ERROR_UNSPECIFIED;
    }
    // try to find existing source
    source_lock lock(sources_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_ping(*this, tt);
    } else {
        LOG_WARNING("couldn't find source for " << AOO_MSG_PING << " message");
        return AOO_ERROR_UNSPECIFIED;
    }
}

/*////////////////////////// event ///////////////////////////////////*/

// 'event' is always used inside 'source_desc', so we can safely
// store a pointer to the sockaddr. the ip_address itself
// never changes during lifetime of the 'source_desc'!
event::event(aoo_event_type type, const source_desc& desc){
    source.type = type;
    source.address = desc.address().address();
    source.addrlen = desc.address().length();
    source.id = desc.id();
}

// 'source_event' is used in 'sink' for source events that can outlive
// its corresponding 'source_desc'. therefore the ip_address is copied!
source_event::source_event(aoo_event_type _type, const source_desc &desc)
    : type(_type), address(desc.address()), id(desc.id()) {}

/*////////////////////////// source_desc /////////////////////////////*/

source_desc::source_desc(const ip_address& addr, aoo_id id, double time)
    : addr_(addr), id_(id), state_(source_state::idle),
      last_packet_time_(time)
{
    // reserve some memory, so we don't have to allocate memory
    // when pushing events in the audio thread.
    eventqueue_.reserve(AOO_EVENTQUEUESIZE);
    // resendqueue_.reserve(256);
}

source_desc::~source_desc(){
    // some events use dynamic memory
    event e;
    while (eventqueue_.try_pop(e)){
        if (e.type_ == AOO_FORMAT_CHANGE_EVENT){
            auto fmt = e.format.format;
            deallocate((void *)fmt, fmt->size);
        }
    }
}

bool source_desc::is_active(const sink& s) const {
    auto last = last_packet_time_.load(std::memory_order_relaxed);
    return (s.elapsed_time() - last) < s.source_timeout();
}

aoo_error source_desc::get_format(aoo_format &format){
    // synchronize with handle_format() and update()!
    shared_scoped_lock lock(mutex_);
    if (decoder_){
        return decoder_->get_format(format);
    } else {
        return AOO_ERROR_UNSPECIFIED;
    }
}

void source_desc::reset(const sink &s){
    // take writer lock!
    scoped_lock lock(mutex_);
    update(s);
}

void source_desc::update(const sink &s){
    // resize audio ring buffer
    if (decoder_ && decoder_->blocksize() > 0 && decoder_->samplerate() > 0){
        // recalculate buffersize from ms to samples
        int32_t bufsize = (double)s.buffersize() * 0.001 * decoder_->samplerate();
        auto d = div(bufsize, decoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        // minimum buffer size increases when downsampling!
        int32_t minbuffers = std::ceil((double)decoder_->samplerate() / (double)s.samplerate());
        nbuffers = std::max<int32_t>(nbuffers, minbuffers);
        LOG_DEBUG("source_desc: buffersize (ms): " << s.buffersize()
                  << ", samples: " << bufsize << ", nbuffers = " << nbuffers);

        // resize audio buffer and initially fill with zeros.
        auto nsamples = decoder_->nchannels() * decoder_->blocksize();
        audioqueue_.resize(nsamples, nbuffers);
        infoqueue_.resize(nbuffers);
        channel_ = 0;
        samplerate_ = decoder_->samplerate();
        int count = 0;
        while (audioqueue_.write_available() && infoqueue_.write_available()){
            audioqueue_.write_commit();
            // push nominal samplerate + default channel (0)
            block_info i;
            i.sr = samplerate_;
            i.channel = 0;
            infoqueue_.write(i);
            count++;
        };
        LOG_DEBUG("write " << count << " silent blocks");
    #if 0
        // don't touch the event queue once constructed
        eventqueue_.reset();
    #endif

        // setup resampler
        resampler_.setup(decoder_->blocksize(), s.blocksize(),
                         decoder_->samplerate(), s.samplerate(), decoder_->nchannels());

        // resize block queue
        jitterbuffer_.resize(nbuffers + 4); // extra capacity for network jitter (allows lower buffersizes)

        streamstate_.reset();

        dropped_ = 0;
    }
}

// called from the receive thread
void source_desc::invite(const sink& s){
    // only invite when idle or uninviting!
    // NOTE: state can only change in this thread, so we don't need a CAS
    auto state = state_.load(std::memory_order_relaxed);
    if (state != source_state::stream){
        // special case: (re)invite shortly after uninvite
        if (state == source_state::uninvite){
            // update last packet time to reset timeout!
            last_packet_time_.store(s.elapsed_time());
            // force new format, otherwise handle_format() would ignore
            // the format messages and we would spam the source with
            // redundant invitation messages until we time out.
            // NOTE: don't use a negative value, otherwise we would get
            // a redundant "add" event, see handle_format().
            scoped_lock lock(mutex_);
            salt_++;
        }
    #if 1
        state_time_.store(0.0);
    #else
        state_time_.store(s.elapsed_time());
    #endif
        state_.store(source_state::invite);
        LOG_DEBUG("source_desc: invite");
    } else {
        LOG_WARNING("aoo: couldn't invite source - already active");
    }
}

// called from the receive thread
void source_desc::uninvite(const sink& s){
    // NOTE: state can only change in this thread, so we don't need a CAS
    auto state = state_.load(std::memory_order_relaxed);
    if (state != source_state::idle){
        LOG_DEBUG("source_desc: uninvite");
        // update start time for uninvite phase, see handle_data().
        // NOTE: send_invitation() might concurrently set "state_time_",
        // but it also uses "s.elapsed_time()", so we don't care.
        state_time_.store(s.elapsed_time());
        state_.store(source_state::uninvite);
    } else {
        LOG_WARNING("aoo: couldn't uninvite source - not active");
    }
}

// /aoo/sink/<id>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <settings...>

aoo_error source_desc::handle_format(const sink& s, int32_t salt, const aoo_format& f,
                                     const char *settings, int32_t size){
    // ignore redundant format messages!
    // NOTE: salt_ can only change in this thread,
    // so we don't need a lock to safely *read* it!
    if (salt == salt_){
        return AOO_ERROR_UNSPECIFIED;
    }

    // Create a new decoder if necessary.
    // This is the only thread where the decoder can possibly
    // change, so we don't need a lock to safely *read* it!
    std::unique_ptr<decoder> new_decoder;

    if (!decoder_ || strcmp(decoder_->name(), f.codec)){
        auto c = aoo::find_codec(f.codec);
        if (c){
            new_decoder = c->create_decoder();
            if (!new_decoder){
                LOG_ERROR("couldn't create decoder!");
                return AOO_ERROR_UNSPECIFIED;
            }
        } else {
            LOG_ERROR("codec '" << f.codec << "' not supported!");
            return AOO_ERROR_UNSPECIFIED;
        }
    }

    unique_lock lock(mutex_); // writer lock!
    if (new_decoder){
        decoder_ = std::move(new_decoder);
    }

    auto oldsalt = salt_;
    salt_ = salt;

    // read format
    aoo_format_storage fmt;
    fmt.header.size = sizeof(aoo_format_storage); // !
    if (decoder_->deserialize(f, settings, size, fmt.header) != AOO_OK){
        return AOO_ERROR_UNSPECIFIED;
    }
    // set format
    if (decoder_->set_format(fmt.header) != AOO_OK){
        return AOO_ERROR_UNSPECIFIED;
    }

    update(s);

    lock.unlock();

    // NOTE: state can only change in this thread, so we don't need a CAS.
    auto state = state_.load(std::memory_order_relaxed);
    if (state == source_state::idle || state == source_state::invite){
        state_.store(source_state::stream);
        // only push "add" event, if this is the first format message!
        if (oldsalt < 0){
            event e(AOO_SOURCE_ADD_EVENT, *this);
            eventqueue_.push(e);
            LOG_DEBUG("add new source with id " << id());
        }
    }

    // send format event
    // NOTE: we could just allocate 'aoo_format_storage', but it would be wasteful.
    auto fs = aoo::allocate(fmt.header.size);
    memcpy(fs, &fmt, fmt.header.size);

    event e(AOO_FORMAT_CHANGE_EVENT, *this);
    e.format.format = (const aoo_format *)fs;

    push_event(e);

    return AOO_OK;
}

// /aoo/sink/<id>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

aoo_error source_desc::handle_data(const sink& s, int32_t salt, const aoo::data_packet& d){
    // always update packet time to signify that we're receiving packets
    last_packet_time_.store(s.elapsed_time(), std::memory_order_relaxed);

    // if we're in uninvite state, ignore data and push uninvite request.
    if (state_.load(std::memory_order_relaxed) == source_state::uninvite){
        // only try for a certain amount of time to avoid spamming the source.
        auto delta = s.elapsed_time() - state_time_.load(std::memory_order_relaxed);
        if (delta < s.source_timeout()){
            push_request(request { request_type::uninvite });
        }
        return AOO_OK;
    }

    // synchronize with update()!
    shared_scoped_lock lock(mutex_);

    // the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    if (salt != salt_){
        push_request(request { request_type::format });
        return AOO_OK;
    }

#if 1
    if (!decoder_){
        LOG_DEBUG("ignore data message");
        return AOO_ERROR_UNSPECIFIED;
    }
#else
    assert(decoder_ != nullptr);
#endif
    LOG_DEBUG("got block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << d.channel << ", totalsize = " << d.totalsize
              << ", nframes = " << d.nframes << ", frame = " << d.framenum << ", size " << d.size);

    // check data packet
    LOG_DEBUG("check packet");
    if (!check_packet(d)){
        return AOO_OK; // ?
    }

    // add data packet
    LOG_DEBUG("add packet");
    if (!add_packet(d)){
        return AOO_OK; // ?
    }

    // process blocks and send audio
    LOG_DEBUG("process blocks");
    process_blocks();

    // check and resend missing blocks
    LOG_DEBUG("check missing blocks");
    check_missing_blocks(s);

#if AOO_DEBUG_JITTER_BUFFER
    DO_LOG(jitterbuffer_);
    DO_LOG("oldest: " << jitterbuffer_.last_popped() << ", newest: "
           << jitterbuffer_.last_pushed());
#endif

    return AOO_OK;
}

// /aoo/sink/<id>/ping <src> <time>

aoo_error source_desc::handle_ping(const sink &s, time_tag tt){
#if 0
    if (streamstate_.get_state() != AOO_STREAM_STATE_PLAY){
        return 0;
    }
#endif

#if 0
    time_tag tt2 = s.absolute_time(); // use last stream time
#else
    time_tag tt2 = aoo::time_tag::now(); // use real system time
#endif

    // push "ping" request
    request r(request_type::ping);
    r.ping.tt1 = tt;
    r.ping.tt2 = tt2;
    push_request(r);

    // push "ping" event
    event e(AOO_PING_EVENT, *this);
    e.ping.tt1 = tt;
    e.ping.tt2 = tt2;
    e.ping.tt3 = 0;
    push_event(e);

    return AOO_OK;
}

void source_desc::send(const sink& s, sendfn& fn){
    // handle requests
    request r;
    while (requestqueue_.try_pop(r)){
        switch (r.type){
        case request_type::format:
            send_format_request(s, fn);
            break;
        case request_type::ping:
            send_ping(s, fn, r.ping);
            break;
        case request_type::uninvite:
            send_uninvitation(s, fn);
            break;
        default:
            break;
        }
    }

    // data requests are handled specially
    send_data_requests(s, fn);

    send_invitation(s, fn);
}

void source_desc::decode(const sink& s){
    // synchronize with update()!
    shared_lock lock(mutex_);

    // process blocks and send audio
    process_blocks();

    // check and resend missing blocks
    check_missing_blocks(s);
}

bool source_desc::process(const sink& s, aoo_sample *buffer,
                          int32_t nsamples, time_tag tt)
{
#if 1
    if (state_.load(std::memory_order_acquire) != source_state::stream){
        return false;
    }
#endif
    // synchronize with update()!
    // the mutex should be uncontended most of the time.
    shared_lock lock(mutex_, std::try_to_lock_t{});
    if (!lock.owns_lock()){
        dropped_ += 1.0;
        LOG_VERBOSE("aoo::sink: source_desc::process() would block");
        return false;
    }

    if (!decoder_){
        return false;
    }

    // record stream state
    int32_t lost = streamstate_.get_lost();
    int32_t reordered = streamstate_.get_reordered();
    int32_t resent = streamstate_.get_resent();
    int32_t gap = streamstate_.get_gap();

    if (lost > 0){
        // push packet loss event
        event e(AOO_BLOCK_LOST_EVENT, *this);
        e.block_loss.count = lost;
        push_event(e);
    }
    if (reordered > 0){
        // push packet reorder event
        event e(AOO_BLOCK_REORDERED_EVENT, *this);
        e.block_reorder.count = reordered;
        push_event(e);
    }
    if (resent > 0){
        // push packet resend event
        event e(AOO_BLOCK_RESENT_EVENT, *this);
        e.block_resend.count = resent;
        push_event(e);
    }
    if (gap > 0){
        // push packet gap event
        event e(AOO_BLOCK_GAP_EVENT, *this);
        e.block_gap.count = gap;
        push_event(e);
    }

#if AOO_DEBUG_AUDIO_BUFFER
    DO_LOG("audioqueue: " << audioqueue_.read_available() << " / "
           << audioqueue_.capacity());
#endif

    // read audio queue
    while (audioqueue_.read_available() && infoqueue_.read_available()){
        if (dropped_ > 0.1){
            // skip audio and decrement block counter proportionally
            dropped_ -= s.real_samplerate() / samplerate_;
        } else {
            // write audio into resampler
            if (!resampler_.write(audioqueue_.read_data(), audioqueue_.blocksize())){
                break;
            }
        }

        audioqueue_.read_commit();

        // get block info and set current channel + samplerate
        block_info info;
        infoqueue_.read(info);
        samplerate_ = info.sr;
        // negative channel number: current channel
        if (info.channel >= 0){
            channel_ = info.channel;
        }
    }

    // update resampler
    resampler_.update(samplerate_, s.real_samplerate());
    // read samples from resampler
    auto nchannels = decoder_->nchannels();
    auto readsize = s.blocksize() * nchannels;
    auto readbuf = (aoo_sample *)alloca(readsize * sizeof(aoo_sample));
    if (resampler_.read(readbuf, readsize)){
        // sum source into sink (interleaved -> non-interleaved),
        // starting at the desired sink channel offset.
        // out of bound source channels are silently ignored.
        for (int i = 0; i < nchannels; ++i){
            auto chn = i + channel_;
            if (chn < s.nchannels()){
                auto out = buffer + nsamples * chn;
                for (int j = 0; j < nsamples; ++j){
                    out[j] += readbuf[j * nchannels + i];
                }
            }
        }

        // LOG_DEBUG("read samples from source " << id_);

        if (streamstate_.update_state(AOO_STREAM_STATE_PLAY)){
            // push "start" event
            event e(AOO_STREAM_STATE_EVENT, *this);
            e.source_state.state = AOO_STREAM_STATE_PLAY;
            push_event(e);
        }

        return true;
    } else {
        // buffer ran out -> push "stop" event
        if (streamstate_.update_state(AOO_STREAM_STATE_STOP)){
            event e(AOO_STREAM_STATE_EVENT, *this);
            e.source_state.state = AOO_STREAM_STATE_STOP;
            push_event(e);
        }
        streamstate_.set_underrun(); // notify network thread!

        return false;
    }
}

int32_t source_desc::poll_events(aoo_eventhandler fn, void *user){
    // always lockfree!
    int count = 0;
    event e;
    while (eventqueue_.try_pop(e)){
        fn(user, &e.event_);
        // some events use dynamic memory
        if (e.type_ == AOO_FORMAT_CHANGE_EVENT){
            // freeing memory is not really RT safe,
            // but it is the easiest solution.
            // LATER think about better ways.
            auto fmt = e.format.format;
            deallocate((void *)fmt, fmt->size);
        }
        count++;
    }
    return count;
}

void source_desc::recover(const char *reason, int32_t n){
    if (n > 0){
        n = std::min<int32_t>(n, jitterbuffer_.size());
        // drop blocks
        for (int i = 0; i < n; ++i){
            jitterbuffer_.pop_front();
        }
    } else {
        // clear buffer
        n = jitterbuffer_.size();
        jitterbuffer_.clear();
    }

    // record dropped blocks
    streamstate_.add_lost(n);

    // push empty blocks to keep the buffer full, but leave room for one block!
    int count = 0;
    for (int i = 0; i < n && audioqueue_.write_available() > 1
           && infoqueue_.write_available() > 1; ++i)
    {
        auto size = audioqueue_.blocksize();
        decoder_->decode(nullptr, 0, audioqueue_.write_data(), size);
        audioqueue_.write_commit();

        // push nominal samplerate + current channel
        block_info bi;
        bi.sr = decoder_->samplerate();
        bi.channel = -1;
        infoqueue_.write(bi);

        count++;
    }

    if (count > 0){
        LOG_VERBOSE("dropped " << n << " blocks and wrote " << count
                    << " empty blocks for " << reason);
    }
}

bool source_desc::check_packet(const data_packet &d){
    if (d.sequence <= jitterbuffer_.last_popped()){
        // block too old, discard!
        LOG_VERBOSE("discard old block " << d.sequence);
        return false;
    }

    // check for large gap between incoming block and most recent block
    // (either network problem or stream has temporarily stopped.)
    auto newest = jitterbuffer_.last_pushed();
    auto diff = d.sequence - newest;
    if (newest > 0 && diff > jitterbuffer_.capacity()){
        recover("transmission gap");
        // record gap (measured in blocks)
        streamstate_.add_gap(diff - 1);
    } else {
        // check for sink xruns
        auto xrunsamples = streamstate_.get_xrun();
        if (xrunsamples){
            int32_t xrunblocks = xrunsamples * resampler_.ratio()
                    / (float)decoder_->blocksize() + 0.5;
            recover("sink xrun", xrunblocks);
        }
    }

    if (newest > 0 && diff > 1){
        LOG_VERBOSE("skipped " << (diff - 1) << " blocks");
    }

    return true;
}

bool source_desc::add_packet(const data_packet& d){
    auto block = jitterbuffer_.find(d.sequence);
    if (!block){
        auto newest = jitterbuffer_.last_pushed();
        if (d.sequence <= newest){
            LOG_VERBOSE("discard outdated block " << d.sequence);
            return false;
        }
        // fill gaps with empty blocks
        if (newest > 0){
            for (int32_t i = newest + 1; i < d.sequence; ++i){
                if (jitterbuffer_.full()){
                    recover("jitter buffer overrun");
                }
                jitterbuffer_.push_back(i)->init(i, false);
            }
        }
        // add new block
        if (jitterbuffer_.full()){
            recover("jitter buffer overrun");
        }

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
        } else if (block->has_frame(d.framenum)){
            // frame already received
            LOG_VERBOSE("frame " << d.framenum << " of block " << d.sequence << " already received");
            return false;
        }

        if (d.sequence != jitterbuffer_.last_pushed()){
            // out of order or resent
            if (block->resend_count() > 0){
                LOG_VERBOSE("resent frame " << d.framenum << " of block " << d.sequence);
                streamstate_.add_resent(1);
            } else {
                LOG_VERBOSE("frame " << d.framenum << " of block " << d.sequence << " out of order!");
                streamstate_.add_reordered(1);
            }
        }
    }

    // add frame to block
    block->add_frame(d.framenum, (const char *)d.data, d.size);

    return true;
}

#define MAXHARDWAREBLOCKSIZE 1024

void source_desc::process_blocks(){
    if (jitterbuffer_.empty()){
        return;
    }

    // Transfer all consecutive complete blocks
    int32_t limit = MAXHARDWAREBLOCKSIZE * resampler_.ratio()
            / (float)audioqueue_.blocksize() + 0.5;
    if (audioqueue_.capacity() < limit){
        limit = -1; // don't use limit!
    }

    while (!jitterbuffer_.empty() && audioqueue_.write_available()
           && infoqueue_.write_available()){
        // check for buffer underrun
        if (streamstate_.have_underrun()){
            recover("audio buffer underrun");
            return;
        }

        const char *data;
        int32_t size;
        block_info i;
        auto remaining = audioqueue_.read_available();

        auto& b = jitterbuffer_.front();
        if (b.complete()){
            if (b.dropped()){
                data = nullptr;
                size = 0;
                i.sr = decoder_->samplerate();
                i.channel = -1; // current channel
            #if AOO_DEBUG_JITTER_BUFFER
                DO_LOG("jitter buffer: write empty block (" << b.sequence << ") for source xrun");
            #endif
            } else {
                // block is ready
                data = b.data();
                size = b.size();
                i.sr = b.samplerate;
                i.channel = b.channel;
            #if AOO_DEBUG_JITTER_BUFFER
                DO_LOG("jitter buffer: write samples for block (" << b.sequence << ")");
            #endif
            }
        } else if (jitterbuffer_.size() > 1 && remaining <= limit){
            LOG_DEBUG("remaining: " << remaining << " / " << audioqueue_.capacity()
                      << ", limit: " << limit);
            // we need audio, drop block - but only if it is not
            // the last one (which is expected to be incomplete)
            data = nullptr;
            size = 0;
            i.sr = decoder_->samplerate();
            i.channel = -1; // current channel
            streamstate_.add_lost(1);
            LOG_VERBOSE("dropped block " << b.sequence);
        } else {
            // wait for block
        #if AOO_DEBUG_JITTER_BUFFER
            DO_LOG("jitter buffer: wait");
        #endif
            break;
        }

        // decode data and push samples
        auto ptr = audioqueue_.write_data();
        auto nsamples = audioqueue_.blocksize();
        // decode audio data
        if (decoder_->decode(data, size, ptr, nsamples) != AOO_OK){
            LOG_WARNING("aoo_sink: couldn't decode block!");
            // decoder failed - fill with zeros
            std::fill(ptr, ptr + nsamples, 0);
        }
        audioqueue_.write_commit();

        // push info
        infoqueue_.write(i);

        jitterbuffer_.pop_front();
    }
}

// deal with "holes" in block queue
void source_desc::check_missing_blocks(const sink& s){
    if (jitterbuffer_.empty() || !s.resend_enabled()){
        return;
    }
    int32_t resent = 0;
    int32_t maxnumframes = s.resend_maxnumframes();
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
                            resendqueue_.push(data_request { b->sequence, i });
                        #if 0
                            DO_LOG("request " << b->sequence << " (" << i << ")");
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
                    resendqueue_.push(data_request { b->sequence, -1 }); // whole block
                #if 0
                    DO_LOG("request " << b->sequence << " (all)");
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

// /aoo/src/<id>/format <version> <sink>

void source_desc::send_format_request(const sink& s, sendfn& fn) {
    LOG_VERBOSE("request format for source " << id_);
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN +
            AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_FORMAT_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_FORMAT);

    msg << osc::BeginMessage(address) << s.id() << (int32_t)make_version()
        << osc::EndMessage;

    fn(msg.Data(), msg.Size(), addr_, flags());
}

// AoO/<id>/ping <sink>

void source_desc::send_ping(const sink &s, sendfn& fn, const ping_request &ping){
#if 0
    // only send ping reply if source is active
    if (streamstate_.get_state() != AOO_STREAM_STATE_PLAY){
        return;
    }
#endif
    auto lost_blocks = streamstate_.get_lost_since_ping();

    char buffer[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
            + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_PING_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_PING);

    msg << osc::BeginMessage(address) << s.id()
        << osc::TimeTag(ping.tt1)
        << osc::TimeTag(ping.tt2)
        << lost_blocks
        << osc::EndMessage;

    fn(msg.Data(), msg.Size(), addr_, flags());

    LOG_DEBUG("send /ping to source " << id_);
}

// AoO/<id>/uninvite <sink>

void source_desc::send_uninvitation(const sink &s, sendfn& fn){
    char buffer[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
            + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_UNINVITE_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_UNINVITE);

    msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

    fn(msg.Data(), msg.Size(), addr_, flags());

    LOG_DEBUG("send /uninvite source " << id_);
}


// /aoo/src/<id>/data <sink> <salt> <seq0> <frame0> <seq1> <frame1> ...

void source_desc::send_data_requests(const sink &s, sendfn& fn){
    if (resendqueue_.empty()){
        return;
    }
    // called without lock!
    shared_lock lock(mutex_);
    int32_t salt = salt_;
    lock.unlock();

    // send request messages
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t maxaddrsize = AOO_MSG_DOMAIN_LEN +
            AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_DATA_LEN;
    char address[maxaddrsize];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_DATA);

    const int32_t maxdatasize = s.packetsize() - maxaddrsize - 16; // id + salt + padding
    const int32_t maxrequests = maxdatasize / 10; // 2 * (int32_t + typetag)
    int32_t numrequests = 0;

    while (!resendqueue_.empty() && numrequests < maxrequests){
        msg << osc::BeginMessage(address) << s.id() << salt;

        for (int i = 0; i < maxrequests; ++i, ++numrequests){
            data_request request;
            if (resendqueue_.try_pop(request)){
                msg << request.sequence << request.frame;
            } else {
                break;
            }
        }

        msg << osc::EndMessage;

        fn(msg.Data(), msg.Size(), addr_, flags());
    }
}

// AoO/<id>/invite <sink>

// only send every 50 ms! LATER we might make this settable
#define INVITE_INTERVAL 0.05

void source_desc::send_invitation(const sink& s, sendfn& fn){
    // called without lock!
    if (state_.load(std::memory_order_acquire) != source_state::invite){
        return;
    }

    auto now = s.elapsed_time();
    if ((now - state_time_.load(std::memory_order_relaxed)) < INVITE_INTERVAL){
        return;
    } else {
        state_time_.store(now);
    }

    char buffer[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
            + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_INVITE_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_INVITE);

    msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

    fn(msg.Data(), msg.Size(), addr_, flags());

    LOG_DEBUG("send /invite to source " << id_);
}

} // aoo
