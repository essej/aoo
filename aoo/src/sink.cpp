/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "sink.hpp"

#include <algorithm>
#include <cmath>

/*//////////////////// memory_block /////////////////*/

namespace aoo {

memory_block * memory_block::allocate(size_t size){
    auto fullsize = sizeof(memory_block::header) + size;
    auto mem = (memory_block *)aoo::allocate(fullsize);
    mem->header.next = nullptr;
    mem->header.size = size;
#if DEBUG_MEMORY
    fprintf(stderr, "allocate memory block (%d bytes)\n", size);
    fflush(stderr);
#endif
    return mem;
}

void memory_block::free(memory_block *mem){
#if DEBUG_MEMORY
    fprintf(stderr, "deallocate memory block (%d bytes)\n", mem->size());
    fflush(stderr);
#endif
    aoo::deallocate(mem, mem->full_size());
}

} // aoo

/*//////////////////// aoo_sink /////////////////////*/

aoo_sink * aoo_sink_new(aoo_id id, uint32_t flags) {
    return aoo::construct<aoo::sink_imp>(id, flags);
}

aoo::sink_imp::sink_imp(aoo_id id, uint32_t flags)
    : id_(id) {
    eventqueue_.reserve(AOO_EVENTQUEUESIZE);
}

void aoo_sink_free(aoo_sink *sink) {
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::sink_imp *>(sink));
}

aoo::sink_imp::~sink_imp(){
    // free memory blocks
    auto mem = memlist_.load(std::memory_order_relaxed);
    while (mem){
        auto next = mem->header.next;
        memory_block::free(mem);
        mem = next;
    }
}

aoo_error aoo_sink_setup(aoo_sink *sink, int32_t samplerate,
                         int32_t blocksize, int32_t nchannels) {
    return sink->setup(samplerate, blocksize, nchannels);
}

aoo_error aoo::sink_imp::setup(int32_t samplerate,
                               int32_t blocksize, int32_t nchannels){
    if (samplerate > 0 && blocksize > 0 && nchannels > 0)
    {
        if (samplerate != samplerate_ || blocksize != blocksize_ ||
            nchannels != nchannels_)
        {
            nchannels_ = nchannels;
            samplerate_ = samplerate;
            blocksize_ = blocksize;

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

aoo_error aoo::sink_imp::invite_source(const void *address, int32_t addrlen, aoo_id id){
    ip_address addr((const sockaddr *)address, addrlen);

    push_request(source_request { request_type::invite, addr, id });

    return AOO_OK;
}

aoo_error aoo_sink_uninvite_source(aoo_sink *sink, const void *address,
                                   int32_t addrlen, aoo_id id)
{
    return sink->uninvite_source(address, addrlen, id);
}

aoo_error aoo::sink_imp::uninvite_source(const void *address, int32_t addrlen, aoo_id id){
    ip_address addr((const sockaddr *)address, addrlen);

    push_request(source_request { request_type::uninvite, addr, id });

    return AOO_OK;
}

aoo_error aoo_sink_uninvite_all(aoo_sink *sink){
    return sink->uninvite_all();
}

aoo_error aoo::sink_imp::uninvite_all(){
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

aoo_error aoo::sink_imp::set_option(int32_t opt, void *ptr, int32_t size)
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

aoo_error aoo::sink_imp::get_option(int32_t opt, void *ptr, int32_t size)
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

aoo_error aoo::sink_imp::set_source_option(const void *address, int32_t addrlen, aoo_id id,
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

aoo_error aoo::sink_imp::get_source_option(const void *address, int32_t addrlen, aoo_id id,
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

aoo_error aoo::sink_imp::handle_message(const char *data, int32_t n,
                                        const void *address, int32_t addrlen) {
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

aoo_error aoo::sink_imp::send(aoo_sendfn fn, void *user){
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
            sync::scoped_lock<sync::mutex> lock1(source_mutex_);
            source_lock lock2(sources_);
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
        // LOG_DEBUG("aoo::sink: try_free() would block");
    }

    return AOO_OK;
}

aoo_error aoo_sink_process(aoo_sink *sink, aoo_sample **data,
                           int32_t nsamples, uint64_t t) {
    return sink->process(data, nsamples, t);
}

#define AOO_MAXNUMEVENTS 256

aoo_error aoo::sink_imp::process(aoo_sample **data, int32_t nsamples, uint64_t t){
    if (sources_.empty() && requestqueue_.empty()){
        timer_.reset(); // !
        return AOO_ERROR_UNSPECIFIED;
    }

    // zero outputs
    for (int i = 0; i < nchannels_; ++i){
        std::fill(data[i], data[i] + nsamples, 0);
    }

    // update timer
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
        DO_LOG_DEBUG("time elapsed: " << elapsed << ", period: "
                  << dll_.period() << ", samplerate: " << dll_.samplerate());
    #endif
    }

    bool didsomething = false;

    // no lock needed - sources are only removed in this thread!
    for (auto it = sources_.begin(); it != sources_.end();){
        if (it->process(*this, data, nsamples, t)){
            didsomething = true;
        } else if (!it->is_active(*this)){
            // move source to garbage list (will be freed in send())
            if (it->is_inviting()){
                LOG_VERBOSE("aoo::sink: invitation for " << it->address().name()
                            << " " << it->address().port() << " timed out");
                source_event e(AOO_INVITE_TIMEOUT_EVENT, *it);
                send_event(e, AOO_THREAD_AUDIO);
            } else {
                LOG_VERBOSE("aoo::sink: removed inactive source " << it->address().name()
                            << " " << it->address().port());
                source_event e(AOO_SOURCE_REMOVE_EVENT, *it);
                send_event(e, AOO_THREAD_AUDIO);
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
    return AOO_OK;
}

aoo_error aoo_sink_set_eventhandler(aoo_sink *sink, aoo_eventhandler fn,
                                    void *user, int32_t mode)
{
    return sink->set_eventhandler(fn, user, mode);
}

aoo_error aoo::sink_imp::set_eventhandler(aoo_eventhandler fn, void *user, int32_t mode)
{
    eventhandler_ = fn;
    eventcontext_ = user;
    eventmode_ = (aoo_event_mode)mode;
    return AOO_OK;
}

aoo_bool aoo_sink_events_available(aoo_sink *sink){
    return sink->events_available();
}

aoo_bool aoo::sink_imp::events_available(){
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

aoo_error aoo_sink_poll_events(aoo_sink *sink){
    return sink->poll_events();
}

#define EVENT_THROTTLE 1000

aoo_error aoo::sink_imp::poll_events(){
    int total = 0;
    source_event e;
    while (eventqueue_.try_pop(e)){
        aoo_source_event se;
        se.type = e.type;
        se.address = e.address.address();
        se.addrlen = e.address.length();
        se.id = e.id;
        eventhandler_(eventcontext_, (const aoo_event *)&se,
                      AOO_THREAD_UNKNOWN);
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
    return AOO_OK;
}

namespace aoo {

void sink_imp::send_event(const source_event &e, aoo_thread_level level) {
    switch (eventmode_){
    case AOO_EVENT_POLL:
        eventqueue_.push(e);
        break;
    case AOO_EVENT_CALLBACK:
    {
        aoo_source_event se;
        se.type = e.type;
        se.address = e.address.address();
        se.addrlen = e.address.length();
        se.id = e.id;
        eventhandler_(eventcontext_, (const aoo_event *)&se, level);
        break;
    }
    default:
        break;
    }
}

// only called if mode is AOO_EVENT_CALLBACK
void sink_imp::call_event(const event &e, aoo_thread_level level) const {
    eventhandler_(eventcontext_, &e.event_, level);
    // some events use dynamic memory
    if (e.type_ == AOO_FORMAT_CHANGE_EVENT){
        mem_free(memory_block::from_bytes((void *)e.format.format));
    }
}

memory_block* sink_imp::mem_alloc(size_t size) const {
    for (;;){
        // try to pop existing block
        auto head = memlist_.load(std::memory_order_relaxed);
        if (head){
            auto next = head->header.next;
            if (memlist_.compare_exchange_weak(head, next, std::memory_order_acq_rel)){
                if (head->header.size >= size){
                #if DEBUG_MEMORY
                    fprintf(stderr, "reuse memory block (%d bytes)\n", head->header.size);
                    fflush(stderr);
                #endif
                    return head;
                } else {
                    // free block
                    memory_block::free(head);
                }
            } else {
                // try again
                continue;
            }
        }
        // allocate new block
        return memory_block::allocate(size);
    }
}
void sink_imp::mem_free(memory_block* b) const {
    b->header.next = memlist_.load(std::memory_order_relaxed);
    // check if the head has changed and update it atomically.
    // (if the CAS fails, 'next' is updated to the current head)
    while (!memlist_.compare_exchange_weak(b->header.next, b, std::memory_order_acq_rel)) ;
#if DEBUG_MEMORY
    fprintf(stderr, "return memory block (%d bytes)\n", b->header.size);
    fflush(stderr);
#endif
}

aoo::source_desc * sink_imp::find_source(const ip_address& addr, aoo_id id){
    for (auto& src : sources_){
        if (src.match(addr, id)){
            return &src;
        }
    }
    return nullptr;
}

source_desc * sink_imp::add_source(const ip_address& addr, aoo_id id){
    // add new source
    sources_.emplace_front(addr, id, elapsed_time());
    return &sources_.front();
}

void sink_imp::reset_sources(){
    source_lock lock(sources_);
    for (auto& src : sources_){
        src.reset(*this);
    }
}

// /format <id> <version> <salt> <channels> <sr> <blocksize> <codec> <options...>
aoo_error sink_imp::handle_format_message(const osc::ReceivedMessage& msg,
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
    // for backwards comptability (later remove check)
    uint32_t flags = (it != msg.ArgumentsEnd()) ?
                (uint32_t)(it++)->AsInt32() : 0;

    if (id < 0){
        LOG_WARNING("bad ID for " << AOO_MSG_FORMAT << " message");
        return AOO_ERROR_UNSPECIFIED;
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
    return src->handle_format(*this, salt, f, (const char *)settings, size, flags);
}

aoo_error sink_imp::handle_data_message(const osc::ReceivedMessage& msg,
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
    // NOTE: sources can also be added in the network send thread,
    // so we have to lock a mutex to avoid the ABA problem!
    sync::scoped_lock<sync::mutex> lock1(source_mutex_);
    source_lock lock2(sources_);
    auto src = find_source(addr, id);
    if (!src){
        src = add_source(addr, id);
    }
    return src->handle_data(*this, salt, d);
}

aoo_error sink_imp::handle_ping_message(const osc::ReceivedMessage& msg,
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
    LOG_DEBUG("source_desc");
}

source_desc::~source_desc(){
    // flush event queue
    event e;
    while (eventqueue_.try_pop(e)){
        if (e.type_ == AOO_FORMAT_CHANGE_EVENT){
            auto mem = memory_block::from_bytes((void *)e.format.format);
            memory_block::free(mem);
        }
    }
    // flush packet queue
    data_packet d;
    while (packetqueue_.try_pop(d)){
        auto mem = memory_block::from_bytes((void *)d.data);
        memory_block::free(mem);
    }
    LOG_DEBUG("~source_desc");
}

bool source_desc::is_active(const sink_imp& s) const {
    auto last = last_packet_time_.load(std::memory_order_relaxed);
    return (s.elapsed_time() - last) < s.source_timeout();
}

aoo_error source_desc::get_format(aoo_format &format){
    // synchronize with handle_format() and update()!
    scoped_shared_lock lock(mutex_);
    if (decoder_){
        return decoder_->get_format(format);
    } else {
        return AOO_ERROR_UNSPECIFIED;
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
        // recalculate buffersize from ms to samples
        int32_t bufsize = (double)s.buffersize() * 0.001 * decoder_->samplerate();
        // number of buffers (round up!)
        int32_t nbuffers = std::ceil((double)bufsize / (double)decoder_->blocksize());
        // minimum buffer size depends on resampling + reblocking!
        auto downsample = (double)decoder_->samplerate() / (double)s.samplerate();
        auto reblock = (double)s.blocksize() / (double)decoder_->blocksize();
        int32_t minbuffers = std::ceil(downsample * reblock);
        nbuffers = std::max<int32_t>(nbuffers, minbuffers);
        LOG_DEBUG("source_desc: buffersize (ms): " << s.buffersize()
                  << ", samples: " << bufsize << ", nbuffers = " << nbuffers);

    #if 0
        // don't touch the event queue once constructed
        eventqueue_.reset();
    #endif

        auto nsamples = decoder_->nchannels() * decoder_->blocksize();
        double sr = decoder_->samplerate(); // nominal samplerate

        // setup audio buffer
        auto nbytes = sizeof(block_data::header) + nsamples * sizeof(aoo_sample);
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
        auto minjitterbuf = std::ceil(hwsamples / (double)decoder_->blocksize());
        auto jitterbufsize = std::max<int32_t>(nbuffers, minjitterbuf);
        // LATER optimize max. block size
        jitterbuffer_.resize(jitterbufsize, nsamples * sizeof(double));
        LOG_DEBUG("jitter buffer: " << jitterbufsize << " blocks");

        streamstate_ = AOO_STREAM_STATE_STOP;
        lost_since_ping_.store(0);
        channel_ = 0;
        samplerate_ = decoder_->samplerate();
        dropped_ = 0;
        xrunsamples_ = 0;
        underrun_ = false;
    }
}

// called from the network thread
void source_desc::invite(const sink_imp& s){
    // only invite when idle or uninviting!
    // NOTE: state can only change in this thread (= send thread),
    // so we don't need a CAS loop.
    auto state = state_.load(std::memory_order_relaxed);
    while (state != source_state::stream){
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

// called from the network thread
void source_desc::uninvite(const sink_imp& s){
    // NOTE: state can only change in this thread (= send thread),
    // so we don't need a CAS loop.
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

// /aoo/sink/<id>/format <src> <salt> <numchannels> <samplerate> <blocksize> <codec> <settings...>

aoo_error source_desc::handle_format(const sink_imp& s, int32_t salt, const aoo_format& f,
                                     const char *settings, int32_t size, uint32_t flags){
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

    flags_ = flags;

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

    // NOTE: state can be changed in both network threads,
    // so we need a CAS loop.
    auto state = state_.load(std::memory_order_relaxed);
    while (state == source_state::idle || state == source_state::invite){
        if (state_.compare_exchange_weak(state, source_state::stream)){
            // only push "add" event, if this is the first format message!
            if (oldsalt < 0){
                event e(AOO_SOURCE_ADD_EVENT, *this);
                send_event(s, e, AOO_THREAD_AUDIO);
                LOG_DEBUG("add new source with id " << id());
            }
            break;
        }
    }

    // send format event
    // NOTE: we could just allocate 'aoo_format_storage', but it would be wasteful.
    auto mem = s.mem_alloc(fmt.header.size);
    memcpy(mem->data(), &fmt, fmt.header.size);

    event e(AOO_FORMAT_CHANGE_EVENT, *this);
    e.format.format = (const aoo_format *)mem->data();

    send_event(s, e, AOO_THREAD_NETWORK);

    return AOO_OK;
}

// /aoo/sink/<id>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>

aoo_error source_desc::handle_data(const sink_imp& s, int32_t salt, aoo::data_packet& d){
    // always update packet time to signify that we're receiving packets
    last_packet_time_.store(s.elapsed_time(), std::memory_order_relaxed);

    // if we're in uninvite state, ignore data and send uninvite request.
    if (state_.load(std::memory_order_acquire) == source_state::uninvite){
        // only try for a certain amount of time to avoid spamming the source.
        auto delta = s.elapsed_time() - state_time_.load(std::memory_order_relaxed);
        if (delta < s.source_timeout()){
            push_request(request(request_type::uninvite));
        }
        // ignore data message
        return AOO_OK;
    }

    // the source format might have changed and we haven't noticed,
    // e.g. because of dropped UDP packets.
    if (salt != salt_){
        push_request(request(request_type::format));
        return AOO_OK;
    }

    // synchronize with update()!
    scoped_shared_lock lock(mutex_);

#if 1
    if (!decoder_){
        LOG_DEBUG("ignore data message");
        return AOO_ERROR_UNSPECIFIED;
    }
#else
    assert(decoder_ != nullptr);
#endif

#if AOO_DEBUG_DATA
    LOG_DEBUG("got block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << d.channel << ", totalsize = " << d.totalsize
              << ", nframes = " << d.nframes << ", frame = " << d.framenum << ", size " << d.size);
#endif

    // copy blob data and push to queue
    auto data = (char *)s.mem_alloc(d.size)->data();
    memcpy(data, d.data, d.size);
    d.data = data;
    packetqueue_.push(d);

    return AOO_OK;
}

// /aoo/sink/<id>/ping <src> <time>

aoo_error source_desc::handle_ping(const sink_imp& s, time_tag tt){
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
    event e(AOO_PING_EVENT, *this);
    e.ping.tt1 = tt;
    e.ping.tt2 = tt2;
    e.ping.tt3 = 0;
    send_event(s, e, AOO_THREAD_NETWORK);

    return AOO_OK;
}

void source_desc::send(const sink_imp& s, const sendfn& fn){
    request r;
    while (requestqueue_.try_pop(r)){
        switch (r.type){
        case request_type::ping_reply:
            send_ping_reply(s, fn, r);
            break;
        case request_type::format:
            send_format_request(s, fn);
            break;
        case request_type::uninvite:
            send_uninvitation(s, fn);
            break;
        default:
            break;
        }
    }
    // send invitations
    if (state_.load(std::memory_order_acquire) == source_state::invite){
        send_invitation(s, fn);
    }

    send_data_requests(s, fn);
}

bool source_desc::process(const sink_imp& s, aoo_sample **buffer,
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

    stream_state ss;
    data_packet d;
    while (packetqueue_.try_pop(d)){
        // check data packet
        // LOG_DEBUG("check packet");
        if (check_packet(d, ss)){
            add_packet(d, ss);
        }
        // return memory
        s.mem_free(memory_block::from_bytes((void *)d.data));
    }

    // process blocks and transfer to audio thread
    process_blocks(s, ss);

    // check and resend missing blocks
    check_missing_blocks(s);

#if AOO_DEBUG_JITTER_BUFFER
    DO_LOG_DEBUG(jitterbuffer_);
    DO_LOG_DEBUG("oldest: " << jitterbuffer_.last_popped()
              << ", newest: " << jitterbuffer_.last_pushed());
#endif

    if (ss.lost > 0){
        // push packet loss event
        event e(AOO_BLOCK_LOST_EVENT, *this);
        e.block_loss.count = ss.lost;
        send_event(s, e, AOO_THREAD_AUDIO);
    }
    if (ss.reordered > 0){
        // push packet reorder event
        event e(AOO_BLOCK_REORDERED_EVENT, *this);
        e.block_reorder.count = ss.reordered;
        send_event(s, e, AOO_THREAD_AUDIO);
    }
    if (ss.resent > 0){
        // push packet resend event
        event e(AOO_BLOCK_RESENT_EVENT, *this);
        e.block_resend.count = ss.resent;
        send_event(s, e, AOO_THREAD_AUDIO);
    }
    if (ss.gap > 0){
        // push packet gap event
        event e(AOO_BLOCK_GAP_EVENT, *this);
        e.block_gap.count = ss.gap;
        send_event(s, e, AOO_THREAD_AUDIO);
    }

    // read audio queue
    auto size = decoder_->blocksize() * decoder_->nchannels();

    while (audioqueue_.read_available()){
        auto d = (block_data *)audioqueue_.read_data();

        if (dropped_ > 0.1){
            // skip audio and decrement block counter proportionally
            dropped_ -= s.real_samplerate() / samplerate_;
        } else {
            // write audio into resampler
            if (!resampler_.write(d->data, size)){
                break;
            }
        }

        // set current channel + samplerate
        samplerate_ = d->header.samplerate;
        // negative channel number: current channel
        if (d->header.channel >= 0){
            channel_ = d->header.channel;
        }

        audioqueue_.read_commit();
    }

    // update resampler
    resampler_.update(samplerate_, s.real_samplerate());
    // read samples from resampler
    auto nchannels = decoder_->nchannels();
    auto readsize = nsamples * nchannels;
    auto readbuf = (aoo_sample *)alloca(readsize * sizeof(aoo_sample));
    if (resampler_.read(readbuf, readsize)){
        // sum source into sink (interleaved -> non-interleaved),
        // starting at the desired sink channel offset.
        // out of bound source channels are silently ignored.
        auto realnchannels = s.nchannels();
        for (int i = 0; i < nchannels; ++i){
            auto chn = i + channel_;
            if (chn < realnchannels){
                auto out = buffer[chn];
                for (int j = 0; j < nsamples; ++j){
                    out[j] += readbuf[j * nchannels + i];
                }
            }
        }

        // LOG_DEBUG("read samples from source " << id_);

        if (streamstate_ != AOO_STREAM_STATE_PLAY){
            streamstate_ = AOO_STREAM_STATE_PLAY;

            // push "start" event
            event e(AOO_STREAM_STATE_EVENT, *this);
            e.source_state.state = AOO_STREAM_STATE_PLAY;
            send_event(s, e, AOO_THREAD_AUDIO);
        }

        return true;
    } else {
        // buffer ran out -> push "stop" event
        if (streamstate_ != AOO_STREAM_STATE_STOP){
            streamstate_ = AOO_STREAM_STATE_STOP;

            // push "stop" event
            event e(AOO_STREAM_STATE_EVENT, *this);
            e.source_state.state = AOO_STREAM_STATE_STOP;
            send_event(s, e, AOO_THREAD_AUDIO);
        }
        underrun_ = true;

        return false;
    }
}

int32_t source_desc::poll_events(sink_imp& s, aoo_eventhandler fn, void *user){
    // always lockfree!
    int count = 0;
    event e;
    while (eventqueue_.try_pop(e)){
        fn(user, &e.event_, AOO_THREAD_UNKNOWN);
        // some events use dynamic memory
        if (e.type_ == AOO_FORMAT_CHANGE_EVENT){
            auto mem = memory_block::from_bytes((void *)e.format.format);
            s.mem_free(mem);
        }
        count++;
    }
    return count;
}

int32_t source_desc::recover(const char *reason, int32_t n){
    int32_t limit;
    if (n > 0){
        limit = std::min<int32_t>(n, jitterbuffer_.size());
        // drop blocks
        for (int i = 0; i < limit; ++i){
            jitterbuffer_.pop_front();
        }
    } else {
        // clear buffer
        n = jitterbuffer_.size(); // for logging
        limit = jitterbuffer_.capacity();
        jitterbuffer_.clear();
    }

    // push empty blocks to keep the buffer full, but leave room for one block!
    int count = 0;
    double sr = decoder_->samplerate();
    auto nsamples = decoder_->blocksize() * decoder_->nchannels();
    for (int i = 0; i < limit && audioqueue_.write_available() > 1; ++i){
        auto b = (block_data *)audioqueue_.write_data();
        // push nominal samplerate, channel + silence
        b->header.samplerate = sr;
        b->header.channel = -1; // last channel
        int32_t size = nsamples;
        if (decoder_->decode(nullptr, 0, b->data, size) != AOO_OK){
            // fill with zeros
            std::fill(b->data, b->data + nsamples, 0);
        }
        audioqueue_.write_commit();

        count++;
    }

    if (n > 0 || count > 0){
        LOG_VERBOSE("dropped " << n << " blocks and wrote " << count
                    << " empty blocks for " << reason);
    }

    return n;
}

bool source_desc::check_packet(const data_packet &d, stream_state& state){
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
        auto lost = recover("transmission gap");
        state.lost += lost;
        add_lost(lost);
        // record gap (measured in blocks)
        state.gap += diff - 1;
    } else if (xrunsamples_ > 0) {
        // check for sink xruns
        int32_t xrunblocks = xrunsamples_ * resampler_.ratio()
                / (float)decoder_->blocksize() + 0.5;
        auto lost = recover("sink xrun", xrunblocks);
        state.lost += lost;
        add_lost(lost);
        xrunsamples_ = 0;
    }

    if (newest > 0 && diff > 1){
        LOG_VERBOSE("skipped " << (diff - 1) << " blocks");
    }

    return true;
}

bool source_desc::add_packet(const data_packet& d, stream_state& state){
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
                    auto lost = recover("jitter buffer overrun");
                    state.lost += lost;
                    add_lost(lost);
                }
                jitterbuffer_.push_back(i)->init(i, false);
            }
        }
        // add new block
        if (jitterbuffer_.full()){
            auto lost = recover("jitter buffer overrun");
            state.lost += lost;
            add_lost(lost);
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
                state.resent++;
            } else {
                LOG_VERBOSE("frame " << d.framenum << " of block " << d.sequence << " out of order!");
                state.reordered++;
            }
        }
    }

    // add frame to block
    block->add_frame(d.framenum, (const char *)d.data, d.size);

    return true;
}

void source_desc::process_blocks(const sink_imp& s, stream_state& state){
    if (jitterbuffer_.empty()){
        return;
    }

    int32_t limit = (double)s.blocksize() / (double)decoder_->blocksize()
            * resampler_.ratio() + 0.5;
    auto nsamples = decoder_->blocksize() * decoder_->nchannels();

    // Transfer all consecutive complete blocks
    while (!jitterbuffer_.empty() && audioqueue_.write_available()){
        // check for buffer underrun
        if (underrun_){
            auto lost = recover("audio buffer underrun");
            state.lost += lost;
            add_lost(lost);
            underrun_ = false;
            return;
        }

        const char *data;
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
            } else {
                // block is ready
                data = b.data();
                size = b.size();
            #if 1
                // dynamic resampling
                sr = b.samplerate; // real samplerate
            #else
                sr = decoder_->samplerate(); // nominal samplerate
            #endif
                channel = b.channel;
            #if AOO_DEBUG_JITTER_BUFFER
                DO_LOG_DEBUG("jitter buffer: write samples for block ("
                          << b.sequence << ")");
            #endif
            }
        } else if (jitterbuffer_.size() > 1 && audioqueue_.read_available() < limit){
            LOG_DEBUG("remaining: " << audioqueue_.read_available() << " / " << audioqueue_.capacity()
                      << ", limit: " << limit);
            // we need audio, so we have to drop a block - but only if it is not
            // the last one (which is expected to be incomplete)
            data = nullptr;
            size = 0;
            sr = decoder_->samplerate(); // nominal samplerate
            channel = -1; // current channel
            state.lost++;
            add_lost(1);
            LOG_VERBOSE("dropped block " << b.sequence);
        } else {
            // wait for block
        #if AOO_DEBUG_JITTER_BUFFER
            DO_LOG_DEBUG("jitter buffer: wait");
        #endif
            break;
        }

        // push samples and channel
        auto d = (block_data *)audioqueue_.write_data();
        d->header.samplerate = sr;
        d->header.channel = channel;
        // decode and push audio data
        auto n = nsamples;
        if (decoder_->decode(data, size, d->data, n) != AOO_OK){
            LOG_WARNING("aoo_sink: couldn't decode block!");
            // decoder failed - fill with zeros
            std::fill(d->data, d->data + nsamples, 0);
        }
        audioqueue_.write_commit();

        jitterbuffer_.pop_front();
    }
}

// /aoo/src/<id>/data <sink> <salt> <seq0> <frame0> <seq1> <frame1> ...

// deal with "holes" in block queue
void source_desc::check_missing_blocks(const sink_imp& s){
    // only check if it has more than a single pending block!
    if (jitterbuffer_.size() <= 1 || !s.resend_enabled()){
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
    auto lost_blocks = lost_since_ping_.exchange(0);

    char buffer[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
            + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_PING_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_PING);

    msg << osc::BeginMessage(address) << s.id()
        << osc::TimeTag(r.ping.tt1)
        << osc::TimeTag(r.ping.tt2)
        << lost_blocks
        << osc::EndMessage;

    fn(msg.Data(), msg.Size(), addr_, flags_);

    LOG_DEBUG("send /ping to source " << id_);
}

// /aoo/src/<id>/format <version> <sink>
// called without lock!
void source_desc::send_format_request(const sink_imp& s, const sendfn& fn) {
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

    fn(msg.Data(), msg.Size(), addr_, flags_);
}

void source_desc::send_data_requests(const sink_imp& s, const sendfn& fn){
    if (!datarequestqueue_.empty()){
        return;
    }

    shared_lock lock(mutex_);
    int32_t salt = salt_; // cache!
    lock.unlock();

    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    // make OSC address pattern
    const int32_t maxaddrsize = AOO_MSG_DOMAIN_LEN +
            AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_DATA_LEN;
    char pattern[maxaddrsize];
    snprintf(pattern, sizeof(pattern), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_DATA);

    const int32_t maxdatasize = s.packetsize() - maxaddrsize - 16; // id + salt + padding
    const int32_t maxrequests = maxdatasize / 10; // 2 * (int32_t + typetag)
    int32_t numrequests = 0;

    msg << osc::BeginMessage(pattern) << s.id() << salt;

    data_request r;
    while (datarequestqueue_.try_pop(r)){
        msg << r.sequence << r.frame;
        if (++numrequests >= maxrequests){
            // send it off
            msg << osc::EndMessage;

            fn(msg.Data(), msg.Size(), address(), flags_);

            // prepare next message
            msg.Clear();
            msg << osc::BeginMessage(pattern) << s.id() << salt;
            numrequests = 0;
        }
    }

    if (numrequests > 0){
        // send it off
        msg << osc::EndMessage;

        fn(msg.Data(), msg.Size(), address(), flags_);
    }
}

// AoO/<id>/invite <sink>

// only send every 50 ms! LATER we might make this settable
#define INVITE_INTERVAL 0.05

// called without lock!
void source_desc::send_invitation(const sink_imp& s, const sendfn& fn){
    auto now = s.elapsed_time();
    if ((now - state_time_.load()) < INVITE_INTERVAL){
        return;
    }
    state_time_.store(now);

    char buffer[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
            + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_INVITE_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_INVITE);

    msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

    fn(msg.Data(), msg.Size(), addr_, flags_);

    LOG_DEBUG("send /invite to source " << id_);
}

// called without lock!
void source_desc::send_uninvitation(const sink_imp& s, const sendfn &fn){
    // /aoo/<id>/uninvite <sink>
    char buffer[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buffer, sizeof(buffer));

    // make OSC address pattern
    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
            + AOO_MSG_SOURCE_LEN + 16 + AOO_MSG_UNINVITE_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SOURCE, id_, AOO_MSG_UNINVITE);

    msg << osc::BeginMessage(address) << s.id() << osc::EndMessage;

    fn(msg.Data(), msg.Size(), addr_, flags_);

    LOG_DEBUG("send /uninvite source " << id_);
}

void source_desc::send_event(const sink_imp& s, const event& e,
                             aoo_thread_level level){
    switch (s.event_mode()){
    case AOO_EVENT_POLL:
        eventqueue_.push(e);
        break;
    case AOO_EVENT_CALLBACK:
        s.call_event(e, level);
        break;
    default:
        break;
    }
}

} // aoo
