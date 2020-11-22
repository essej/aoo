/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "sink.hpp"

#include <algorithm>
#include <cmath>

/*//////////////////// aoo_sink /////////////////////*/

aoo_sink * aoo_sink_new(aoo_id id, aoo_replyfn replyfn, void *user) {
    return new aoo::sink(id, replyfn, user);
}

aoo::sink::sink(aoo_id id, aoo_replyfn replyfn, void *user)
    : id_(id), replyfn_(replyfn), user_(user) {
    eventqueue_.resize(AOO_EVENTQUEUESIZE);
}

void aoo_sink_free(aoo_sink *sink) {
    // cast to correct type because base class
    // has no virtual destructor!
    delete static_cast<aoo::sink *>(sink);
}

int32_t aoo_sink_setup(aoo_sink *sink, int32_t samplerate,
                       int32_t blocksize, int32_t nchannels) {
    return sink->setup(samplerate, blocksize, nchannels);
}

int32_t aoo::sink::setup(int32_t samplerate,
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
        return 1;
    }
    return 0;
}

int32_t aoo_sink_invite_source(aoo_sink *sink, const void *address,
                               int32_t addrlen, aoo_id id)
{
    return sink->invite_source(address, addrlen, id);
}

// LATER put invitations on a queue
int32_t aoo::sink::invite_source(const void *address, int32_t addrlen, aoo_id id){
    ip_address addr((const sockaddr *)address, addrlen);

    // try to find existing source
    shared_lock lock(source_mutex_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id, 0);
    }
    src->request_invite();

    return 1;
}

int32_t aoo_sink_uninvite_source(aoo_sink *sink, const void *address,
                                 int32_t addrlen, aoo_id id)
{
    return sink->uninvite_source(address, addrlen, id);
}

// LATER put uninvitations on a queue
int32_t aoo::sink::uninvite_source(const void *address, int32_t addrlen, aoo_id id){
     ip_address addr((const sockaddr *)address, addrlen);
    // try to find existing source
    shared_scoped_lock lock(source_mutex_);
    auto src = find_source(addr, id);
    if (src){
        src->request_uninvite();
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_sink_uninvite_all(aoo_sink *sink){
    return sink->uninvite_all();
}

int32_t aoo::sink::uninvite_all(){
    shared_scoped_lock lock(source_mutex_);
    for (auto& src : sources_){
        src.request_uninvite();
    }
    return 1;
}

namespace aoo {

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

} // aoo

#define CHECKARG(type) assert(size == sizeof(type))

int32_t aoo_sink_set_option(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->set_option(opt, p, size);
}

int32_t aoo::sink::set_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // id
    case aoo_opt_id:
    {
        CHECKARG(int32_t);
        auto newid = as<int32_t>(ptr);
        if (id_.exchange(newid) != newid){
            // LATER clear source list here
        }
        break;
    }
    // reset
    case aoo_opt_reset:
        reset_sources();
        // reset time DLL
        timer_.reset();
        break;
    // buffer size
    case aoo_opt_buffersize:
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
    case aoo_opt_timefilter_bandwidth:
    {
        CHECKARG(float);
        auto bw = std::max<double>(0, std::min<double>(1, as<float>(ptr)));
        bandwidth_.store(bw);
        timer_.reset(); // will update time DLL and reset timer
        break;
    }
    // packetsize
    case aoo_opt_packetsize:
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
    case aoo_opt_resend_enable:
        CHECKARG(int32_t);
        resend_enabled_.store(as<int32_t>(ptr));
        break;
    // resend interval
    case aoo_opt_resend_interval:
    {
        CHECKARG(int32_t);
        auto interval = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        resend_interval_.store(interval);
        break;
    }
    // resend maxnumframes
    case aoo_opt_resend_maxnumframes:
    {
        CHECKARG(int32_t);
        auto maxnumframes = std::max<int32_t>(1, as<int32_t>(ptr));
        resend_maxnumframes_.store(maxnumframes);
        break;
    }
    // source timeout
    case aoo_opt_source_timeout:
    {
        CHECKARG(int32_t);
        auto timeout = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        source_timeout_.store(timeout);
        break;
    }
    // unknown
    default:
        LOG_WARNING("aoo_sink: unsupported option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_sink_get_option(aoo_sink *sink, int32_t opt, void *p, int32_t size)
{
    return sink->get_option(opt, p, size);
}

int32_t aoo::sink::get_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // id
    case aoo_opt_id:
        as<aoo_id>(ptr) = id();
        break;
    // buffer size
    case aoo_opt_buffersize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = buffersize_.load();
        break;
    // timefilter bandwidth
    case aoo_opt_timefilter_bandwidth:
        CHECKARG(float);
        as<float>(ptr) = bandwidth_.load();
        break;
    // resend packetsize
    case aoo_opt_packetsize:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packetsize_.load();
        break;
    // resend limit
    case aoo_opt_resend_enable:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_enabled_.load();
        break;
    // resend interval
    case aoo_opt_resend_interval:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_interval_.load() * 1000.0;
        break;
    // resend maxnumframes
    case aoo_opt_resend_maxnumframes:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_maxnumframes_.load();
        break;
    case aoo_opt_source_timeout:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = source_timeout_.load() * 1000.0;
        break;
    // unknown
    default:
        LOG_WARNING("aoo_sink: unsupported option " << opt);
        return 0;
    }
    return 1;
}

int32_t aoo_sink_set_sourceoption(aoo_sink *sink, const void *address, int32_t addrlen,
                                  aoo_id id, int32_t opt, void *p, int32_t size)
{
    return sink->set_sourceoption(address, addrlen, id, opt, p, size);
}

int32_t aoo::sink::set_sourceoption(const void *address, int32_t addrlen, aoo_id id,
                                    int32_t opt, void *ptr, int32_t size)
{
    ip_address addr((const sockaddr *)address, addrlen);

    shared_scoped_lock lock(source_mutex_);
    auto src = find_source(addr, id);
    if (src){
        switch (opt){
        // reset
        case aoo_opt_reset:
            src->reset(*this);
            break;
        // unsupported
        default:
            LOG_WARNING("aoo_sink: unsupported source option " << opt);
            return 0;
        }
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_sink_get_sourceoption(aoo_sink *sink, const void *address, int32_t addrlen,
                                  aoo_id id, int32_t opt, void *p, int32_t size)
{
    return sink->get_sourceoption(address, addrlen, id, opt, p, size);
}

int32_t aoo::sink::get_sourceoption(const void *address, int32_t addrlen, aoo_id id,
                                    int32_t opt, void *p, int32_t size)
{
    ip_address addr((const sockaddr *)address, addrlen);

    shared_scoped_lock lock(source_mutex_);
    auto src = find_source(addr, id);
    if (src){
        switch (opt){
        // format
        case aoo_opt_format:
            CHECKARG(aoo_format_storage);
            return src->get_format(as<aoo_format_storage>(p));
        // unsupported
        default:
            LOG_WARNING("aoo_sink: unsupported source option " << opt);
            return 0;
        }
        return 1;
    } else {
        return 0;
    }
}

int32_t aoo_sink_handle_message(aoo_sink *sink, const char *data, int32_t n,
                                const void *address, int32_t addrlen) {
    return sink->handle_message(data, n, address, addrlen);
}

int32_t aoo::sink::handle_message(const char *data, int32_t n,
                                  const void *address, int32_t addrlen) {
    try {
        ip_address addr((const sockaddr *)address, addrlen);

        osc::ReceivedPacket packet(data, n);
        osc::ReceivedMessage msg(packet);

        if (samplerate_ == 0){
            return 0; // not setup yet
        }

        aoo_type type;
        aoo_id sinkid;
        auto onset = aoo_parse_pattern(data, n, &type, &sinkid);
        if (!onset){
            LOG_WARNING("not an AoO message!");
            return 0;
        }
        if (type != AOO_TYPE_SINK){
            LOG_WARNING("not a sink message!");
            return 0;
        }
        if (sinkid != id() && sinkid != AOO_ID_WILDCARD){
            LOG_WARNING("wrong sink ID!");
            return 0;
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
    return 0;
}

int32_t aoo_sink_send(aoo_sink *sink){
    return sink->send();
}

int32_t aoo::sink::send(){
    bool didsomething = false;

    shared_scoped_lock lock(source_mutex_);
    for (auto& s: sources_){
        if (s.send(*this)){
            didsomething = true;
        }
    }
    return didsomething;
}

int32_t aoo_sink_decode(aoo_sink *sink) {
    return sink->decode();
}

int32_t aoo::sink::decode() {
    bool result = false;

    shared_lock lock(source_mutex_);
    for (auto& s : sources_){
        if (s.decode(*this)){
            result = true;
        }
    }
    lock.unlock();

    // free unused source_descs
    sources_.free();

    return result;
}

int32_t aoo_sink_process(aoo_sink *sink, aoo_sample **data,
                         int32_t nsamples, uint64_t t) {
    return sink->process(data, nsamples, t);
}

#define AOO_MAXNUMEVENTS 256

int32_t aoo::sink::process(aoo_sample **data, int32_t nsamples, uint64_t t){
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
            // move source to garbage list (will be freed in decode()),
            // but only if we can grab the lock!
            unique_lock lock(source_mutex_, std::try_to_lock_t {}); // writer lock!
            if (lock.owns_lock()){
                LOG_VERBOSE("aoo::sink: removed inactive source " << it->address().name()
                            << " " << it->address().port());
                event e(AOO_SOURCE_REMOVE_EVENT, it->address(), it->id());
                if (eventqueue_.write_available() > 0){
                    eventqueue_.write(e);
                }
                it =  sources_.erase(it);
                continue;
            } else {
                LOG_WARNING("aoo::sink: removing inactive source would block");
            }
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
        return 1;
    } else {
        return 0;
    }
}
int32_t aoo_sink_events_available(aoo_sink *sink){
    return sink->events_available();
}

int32_t aoo::sink::events_available(){
    if (eventqueue_.read_available() > 0){
        return true;
    }

    shared_scoped_lock lock(source_mutex_);
    for (auto& src : sources_){
        if (src.has_events()){
            return true;
        }
    }

    return false;
}

int32_t aoo_sink_poll_events(aoo_sink *sink,
                             aoo_eventhandler fn, void *user){
    return sink->poll_events(fn, user);
}

#define EVENT_THROTTLE 1000

int32_t aoo::sink::poll_events(aoo_eventhandler fn, void *user){
    if (!fn){
        return 0;
    }
    int total = 0;
    while (eventqueue_.read_available() > 0){
        event e;
        eventqueue_.read(e);
        fn(user, &e.event_);
        total++;
    }
    // we only need to protect against source removal
    shared_scoped_lock lock(source_mutex_);
    for (auto& src : sources_){
        total += src.poll_events(fn, user);
        if (total > EVENT_THROTTLE){
            break;
        }
    }
    return total;
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

source_desc * sink::add_source(const ip_address& addr, aoo_id id, int32_t salt){
    // add new source
    sources_.emplace_front(addr, id, salt);
    return &sources_.front();
}

void sink::reset_sources(){
    shared_scoped_lock lock(source_mutex_);
    for (auto& src : sources_){
        src.reset(*this);
    }
}

bool check_version(uint32_t);

int32_t sink::handle_format_message(const osc::ReceivedMessage& msg,
                                    const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    aoo_id id = (it++)->AsInt32();
    int32_t version = (it++)->AsInt32();

    // LATER handle this in the source_desc (e.g. ignoring further messages)
    if (!check_version(version)){
        LOG_ERROR("aoo_sink: source version not supported");
        return 0;
    }

    int32_t salt = (it++)->AsInt32();
    // get format from arguments
    aoo_format f;
    f.nchannels = (it++)->AsInt32();
    f.samplerate = (it++)->AsInt32();
    f.blocksize = (it++)->AsInt32();
    f.codec = (it++)->AsString();
    const void *settings;
    osc::osc_bundle_element_size_t size;
    (it++)->AsBlob(settings, size);

    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_MSG_FORMAT << " message");
        return 0;
    }
    // try to find existing source
    shared_scoped_lock lock(source_mutex_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id, salt);
    }
    return src->handle_format(*this, salt, f, (const char *)settings, size);
}

int32_t sink::handle_data_message(const osc::ReceivedMessage& msg,
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
        return 0;
    }
    // try to find existing source
    shared_scoped_lock lock(source_mutex_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_data(*this, salt, d);
    } else {
        // discard data message, add source and request format!
        add_source(addr, id, salt)->request_format();
        return 0;
    }
}

int32_t sink::handle_ping_message(const osc::ReceivedMessage& msg,
                                  const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();

    auto id = (it++)->AsInt32();
    time_tag tt = (it++)->AsTimeTag();

    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_MSG_PING << " message");
        return 0;
    }
    // try to find existing source
    shared_scoped_lock lock(source_mutex_);
    auto src = find_source(addr, id);
    if (src){
        return src->handle_ping(*this, tt);
    } else {
        LOG_WARNING("couldn't find source for " << AOO_MSG_PING << " message");
        return 0;
    }
}

/*////////////////////////// source_desc /////////////////////////////*/

source_desc::source_desc(const ip_address& addr, aoo_id id, int32_t salt)
    : addr_(addr), id_(id), salt_(salt)
{
    eventqueue_.resize(AOO_EVENTQUEUESIZE);
    // push "add" event
    event e(AOO_SOURCE_ADD_EVENT, addr, id);
    eventqueue_.write(e); // no need to lock
    LOG_DEBUG("add new source with id " << id);
    resendqueue_.resize(256);
}

bool source_desc::is_active(const sink& s) const {
    if (lastprocesstime_.is_empty()){
        // initialize
        lastprocesstime_ = s.absolute_time();
        return true;
    }
    auto delta = time_tag::duration(lastprocesstime_, s.absolute_time());
    return delta < s.source_timeout();
}

int32_t source_desc::get_format(aoo_format_storage &format){
    // synchronize with handle_format() and update()!
    shared_lock lock(mutex_);
    if (decoder_){
        return decoder_->get_format(format);
    } else {
        return 0;
    }
}

void source_desc::reset(const sink &s){
    // take writer lock!
    unique_lock lock(mutex_);
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

// /aoo/sink/<id>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <settings...>

int32_t source_desc::handle_format(const sink& s, int32_t salt, const aoo_format& f,
                                   const char *settings, int32_t size){
    std::unique_ptr<decoder> new_decoder;
    {
        // create a new decoder if necessary
        shared_scoped_lock rdlock(mutex_); // reader lock!
        // create/change decoder if needed
        if (!decoder_ || strcmp(decoder_->name(), f.codec)){
            auto c = aoo::find_codec(f.codec);
            if (c){
                new_decoder = c->create_decoder();
                if (!new_decoder){
                    LOG_ERROR("couldn't create decoder!");
                    return 0;
                }
            } else {
                LOG_ERROR("codec '" << f.codec << "' not supported!");
                return 0;
            }
        }
    }

    unique_lock lock(mutex_); // writer lock!
    if (new_decoder){
        decoder_ = std::move(new_decoder);
    }

    salt_ = salt;

    // read format
    decoder_->read_format(f, settings, size);

    update(s);

    lock.unlock();

    // push event
    event e(AOO_SOURCE_FORMAT_EVENT, addr_, id_);
    push_event(e);

    return 1;
}

// /aoo/sink/<id>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

int32_t source_desc::handle_data(const sink& s, int32_t salt, const aoo::data_packet& d){
    // synchronize with update()!
    shared_lock lock(mutex_);

    // the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    if (salt != salt_){
        streamstate_.request_format();
        return 0;
    }

#if 1
    if (!decoder_){
        LOG_DEBUG("ignore data message");
        return 0;
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
        return 0;
    }

    // add data packet
    LOG_DEBUG("add packet");
    if (!add_packet(d)){
        return 0;
    }

    // process blocks and send audio
    LOG_DEBUG("process blocks");
    process_blocks();

    // check and resend missing blocks
    LOG_DEBUG("check missing blocks");
    check_missing_blocks(s);

#if AOO_DEBUG_JITTER_BUFFER
    DO_LOG(jitterbuffer_);
    DO_LOG("oldest: " << jitterbuffer_.oldest() << ", newest: " << jitterbuffer_.newest());
#endif

    return 1;
}

// /aoo/sink/<id>/ping <src> <time>

int32_t source_desc::handle_ping(const sink &s, time_tag tt){
#if 0
    if (streamstate_.get_state() != AOO_SOURCE_STATE_PLAY){
        return 0;
    }
#endif

#if 0
    time_tag tt2 = s.absolute_time(); // use last stream time
#else
    time_tag tt2 = aoo::time_tag::now(); // use real system time
#endif

    streamstate_.set_ping(tt, tt2);

    // push "ping" event
    event e(AOO_PING_EVENT, addr_, id_);
    e.ping.tt1 = tt;
    e.ping.tt2 = tt2;
    e.ping.tt3 = 0;
    push_event(e);

    return 1;
}

bool source_desc::send(const sink& s){
    bool didsomething = false;

    if (send_format_request(s)){
        didsomething = true;
    }
    if (send_data_request(s)){
        didsomething = true;
    }
    if (send_notifications(s)){
        didsomething = true;
    }
    return didsomething;
}

bool source_desc::decode(const sink& s){
    // synchronize with update()!
    shared_lock lock(mutex_);

    // process blocks and send audio
    process_blocks();

    // check and resend missing blocks
    check_missing_blocks(s);

    return true;
}

bool source_desc::process(const sink& s, aoo_sample *buffer,
                          int32_t nsamples, time_tag tt)
{
    // synchronize with update()!
    // the mutex should be uncontended most of the time.
    shared_lock lock(mutex_, std::try_to_lock_t{});
    if (!lock.owns_lock()){
        dropped_ += 1.0;
        LOG_WARNING("aoo::sink: source_desc::process() would block");
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
        event e(AOO_BLOCK_LOST_EVENT, addr_, id_);
        e.block_loss.count = lost;
        push_event(e);
    }
    if (reordered > 0){
        // push packet reorder event
        event e(AOO_BLOCK_REORDERED_EVENT, addr_, id_);
        e.block_reorder.count = reordered;
        push_event(e);
    }
    if (resent > 0){
        // push packet resend event
        event e(AOO_BLOCK_RESENT_EVENT, addr_, id_);
        e.block_resend.count = resent;
        push_event(e);
    }
    if (gap > 0){
        // push packet gap event
        event e(AOO_BLOCK_GAP_EVENT, addr_, id_);
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
        channel_ = info.channel;
        samplerate_ = info.sr;
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

        if (streamstate_.update_state(AOO_SOURCE_STATE_PLAY)){
            // push "start" event
            event e(AOO_SOURCE_STATE_EVENT, addr_, id_);
            e.source_state.state = AOO_SOURCE_STATE_PLAY;
            push_event(e);
        }

        lastprocesstime_ = tt;

        return true;
    } else {
        // buffer ran out -> push "stop" event
        if (streamstate_.update_state(AOO_SOURCE_STATE_STOP)){
            event e(AOO_SOURCE_STATE_EVENT, addr_, id_);
            e.source_state.state = AOO_SOURCE_STATE_STOP;
            push_event(e);
        }
        streamstate_.set_underrun(); // notify network thread!

        return false;
    }
}

int32_t source_desc::poll_events(aoo_eventhandler fn, void *user){
    // copy events - always lockfree! (the eventqueue is never resized)
    int count = 0;
    while (eventqueue_.read_available() > 0){
        event e;
        eventqueue_.read(e);
        fn(user, &e.event_);
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
        decoder_->decode(nullptr, 0, audioqueue_.write_data(),
                         audioqueue_.blocksize());
        audioqueue_.write_commit();

        // push nominal samplerate + current channel
        block_info bi;
        bi.sr = decoder_->samplerate();
        bi.channel = channel_;
        infoqueue_.write(bi);

        count++;
    }

    if (count > 0){
        LOG_VERBOSE("dropped " << n << " blocks and wrote " << count
                    << " empty blocks for " << reason);
    }
}

bool source_desc::check_packet(const data_packet &d){
    auto oldest = jitterbuffer_.oldest();
    auto newest = jitterbuffer_.newest();

    if (d.sequence < oldest){
        // block too old, discard!
        LOG_VERBOSE("discard old block " << d.sequence);
        return false;
    }

    // check for large gap between incoming block and most recent block
    // (either network problem or stream has temporarily stopped.)
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
        auto newest = jitterbuffer_.newest();
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
                block->init(d.sequence, true);
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
        if (d.sequence != jitterbuffer_.newest()){
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
                i.channel = channel_;
                LOG_VERBOSE("wrote empty block (" << b.sequence << ") for source xrun");
            } else {
                // block is ready
                data = b.data();
                size = b.size();
                i.sr = b.samplerate;
                i.channel = b.channel;
                LOG_DEBUG("write samples (" << b.sequence << ")");
            }
        } else if (jitterbuffer_.size() > 1 && remaining <= limit){
            LOG_VERBOSE("remaining: " << remaining << " / " << audioqueue_.capacity()
                        << ", limit: " << limit);
            // we need audio, drop block - but only if it is not
            // the last one (which is expected to be incomplete)
            data = nullptr;
            size = 0;
            i.sr = decoder_->samplerate();
            i.channel = channel_;
            streamstate_.add_lost(1);
            LOG_VERBOSE("dropped block " << b.sequence);
        } else {
            // wait for block
            break;
        }

        // decode data and push samples
        auto ptr = audioqueue_.write_data();
        auto nsamples = audioqueue_.blocksize();
        // decode audio data
        if (decoder_->decode(data, size, ptr, nsamples) < 0){
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
                        if (resent < maxnumframes
                                && resendqueue_.write_available()){
                            resendqueue_.write(data_request { b->sequence, i });
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
                if (resent + nframes <= maxnumframes
                        && resendqueue_.write_available()){
                    resendqueue_.write(data_request { b->sequence, -1 }); // whole block
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

uint32_t make_version();

bool source_desc::send_format_request(const sink& s) {
    if (streamstate_.need_format()){
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

        s.do_send(msg.Data(), msg.Size(), addr_);

        return true;
    } else {
        return false;
    }
}


// /aoo/src/<id>/data <sink> <salt> <seq0> <frame0> <seq1> <frame1> ...

int32_t source_desc::send_data_request(const sink &s){
    // called without lock!
    shared_lock lock(mutex_);
    int32_t salt = salt_;
    lock.unlock();

    int32_t numrequests;
    while ((numrequests = resendqueue_.read_available()) > 0){
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
        auto d = div(numrequests, maxrequests);

        auto dorequest = [&](int32_t n){
            msg << osc::BeginMessage(address) << s.id() << salt;
            while (n--){
                data_request request;
                resendqueue_.read(request);
                msg << request.sequence << request.frame;
            #if 0
                DO_LOG("resend block " << request.sequence
                        << ", frame " << request.frame);
            #endif
            }
            msg << osc::EndMessage;

            s.do_send(msg.Data(), msg.Size(), addr_);
        };

        for (int i = 0; i < d.quot; ++i){
            dorequest(maxrequests);
        }
        if (d.rem > 0){
            dorequest(d.rem);
        }
    }
    return numrequests;
}

// AoO/<id>/ping <sink>

bool source_desc::send_notifications(const sink& s){
    // called without lock!
    bool didsomething = false;

    time_tag pingtime1;
    time_tag pingtime2;
    if (streamstate_.need_ping(pingtime1, pingtime2)){
    #if 1
        // only send ping if source is active
        if (streamstate_.get_state() == AOO_SOURCE_STATE_PLAY){
    #else
        {
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
                << osc::TimeTag(pingtime1)
                << osc::TimeTag(pingtime2)
                << lost_blocks
                << osc::EndMessage;

            s.do_send(msg.Data(), msg.Size(), addr_);

            LOG_DEBUG("send /ping to source " << id_);
            didsomething = true;
        }
    }

    auto invitation = streamstate_.get_invitation_state();
    if (invitation == stream_state::INVITE){
        char buffer[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream msg(buffer, sizeof(buffer));

        // make OSC address pattern
        const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
                + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_INVITE_LEN;
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s%s/%d%s",
                 AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_INVITE);

        msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

        s.do_send(msg.Data(), msg.Size(), addr_);

        LOG_DEBUG("send /invite to source " << id_);

        didsomething = true;
    } else if (invitation == stream_state::UNINVITE){
        char buffer[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream msg(buffer, sizeof(buffer));

        // make OSC address pattern
        const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
                + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_UNINVITE_LEN;
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s%s/%d%s",
                 AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_UNINVITE);

        msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

        s.do_send(msg.Data(), msg.Size(), addr_);

        LOG_DEBUG("send /uninvite source " << id_);

        didsomething = true;
    }

    return didsomething;
}

} // aoo
