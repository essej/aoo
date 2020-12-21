/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "source.hpp"

#include <cstring>
#include <algorithm>
#include <random>
#include <cmath>

/*//////////////////// AoO source /////////////////////*/

#define AOO_DATA_HEADERSIZE 80
// address pattern string: max 32 bytes
// typetag string: max. 12 bytes
// args (without blob data): 36 bytes

aoo_source * aoo_source_new(aoo_id id, uint32_t flags) {
    return aoo::construct<aoo::source>(id, flags);
}

aoo::source::source(aoo_id id, uint32_t flags)
    : id_(id)
{
    // event queue
    eventqueue_.reserve(AOO_EVENTQUEUESIZE);
    // request queues
    // formatrequestqueue_.resize(64);
    // datarequestqueue_.resize(1024);
}

void aoo_source_free(aoo_source *src){
    // cast to correct type because base class
    // has no virtual destructor!
    aoo::destroy(static_cast<aoo::source *>(src));
}

aoo::source::~source() {}

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

#define CHECKARG(type) assert(size == sizeof(type))

aoo_error aoo_source_set_option(aoo_source *src, int32_t opt, void *p, int32_t size)
{
    return src->set_option(opt, p, size);
}

aoo_error aoo::source::set_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // id
    case AOO_OPT_ID:
    {
        auto newid = as<int32_t>(ptr);
        if (id_.exchange(newid) != newid){
            // if playing, restart
            auto expected = stream_state::play;
            state_.compare_exchange_strong(expected, stream_state::start);
        }
        break;
    }
    // stop
    case AOO_OPT_STOP:
        state_.store(stream_state::stop);
        break;
    // resume
    case AOO_OPT_START:
        state_.store(stream_state::start);
        break;
    // format
    case AOO_OPT_FORMAT:
        CHECKARG(aoo_format);
        return set_format(as<aoo_format>(ptr));
    // buffersize
    case AOO_OPT_BUFFERSIZE:
    {
        CHECKARG(int32_t);
        auto bufsize = std::max<int32_t>(as<int32_t>(ptr), 0);
        if (buffersize_.exchange(bufsize) != bufsize){
            scoped_lock lock(update_mutex_); // writer lock!
            update_audioqueue();
        }
        break;
    }
    // packetsize
    case AOO_OPT_PACKETSIZE:
    {
        CHECKARG(int32_t);
        const int32_t minpacketsize = AOO_DATA_HEADERSIZE + 64;
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
    // timefilter bandwidth
    case AOO_OPT_TIMEFILTER_BANDWIDTH:
        CHECKARG(float);
        // time filter
        bandwidth_.store(as<float>(ptr));
        timer_.reset(); // will update
        break;
    // ping interval
    case AOO_OPT_PING_INTERVAL:
    {
        CHECKARG(int32_t);
        auto interval = std::max<int32_t>(0, as<int32_t>(ptr)) * 0.001;
        ping_interval_.store(interval);
        break;
    }
    // resend buffer size
    case AOO_OPT_RESEND_BUFFERSIZE:
    {
        CHECKARG(int32_t);
        // empty buffer is allowed! (no resending)
        auto bufsize = std::max<int32_t>(as<int32_t>(ptr), 0);
        if (resend_buffersize_.exchange(bufsize) != bufsize){
            scoped_lock lock(update_mutex_); // writer lock!
            update_historybuffer();
        }
        break;
    }
    // ping interval
    case AOO_OPT_REDUNDANCY:
    {
        CHECKARG(int32_t);
        // limit it somehow, 16 times is already very high
        auto redundancy = std::max<int32_t>(1, std::min<int32_t>(16, as<int32_t>(ptr)));
        redundancy_.store(redundancy);
        break;
    }
    // unknown
    default:
        LOG_WARNING("aoo_source: unsupported option " << opt);
        return AOO_ERROR_UNSPECIFIED;
    }
    return AOO_OK;
}

aoo_error aoo_source_get_option(aoo_source *src, int32_t opt,
                              void *p, int32_t size)
{
    return src->get_option(opt, p, size);
}

aoo_error aoo::source::get_option(int32_t opt, void *ptr, int32_t size)
{
    switch (opt){
    // id
    case AOO_OPT_ID:
        CHECKARG(int32_t);
        as<aoo_id>(ptr) = id();
        break;
    // format
    case AOO_OPT_FORMAT:
    {
        assert(size >= sizeof(aoo_format));
        auto fmt = as<aoo_format>(ptr);
        fmt.size = size; // !
        shared_lock lock(update_mutex_); // read lock!
        if (encoder_){
            return encoder_->get_format(fmt);
        } else {
            return AOO_ERROR_UNSPECIFIED;
        }
    }
    // buffer size
    case AOO_OPT_BUFFERSIZE:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = buffersize_.load();
        break;
    // time filter bandwidth
    case AOO_OPT_TIMEFILTER_BANDWIDTH:
        CHECKARG(float);
        as<float>(ptr) = bandwidth_.load();
        break;
    // resend buffer size
    case AOO_OPT_RESEND_BUFFERSIZE:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = resend_buffersize_.load();
        break;
    // packetsize
    case AOO_OPT_PACKETSIZE:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = packetsize_.load();
        break;
    // ping interval
    case AOO_OPT_PING_INTERVAL:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = ping_interval_.load() * 1000.0;
        break;
    // ping interval
    case AOO_OPT_REDUNDANCY:
        CHECKARG(int32_t);
        as<int32_t>(ptr) = redundancy_.load();
        break;
    // unknown
    default:
        LOG_WARNING("aoo_source: unsupported option " << opt);
        return AOO_ERROR_UNSPECIFIED;
    }
    return AOO_OK;
}

aoo_error aoo_source_set_sinkoption(aoo_source *src, const void *address, int32_t addrlen,
                                    aoo_id id, int32_t opt, void *p, int32_t size)
{
    return src->set_sinkoption(address, addrlen, id, opt, p, size);
}

aoo_error aoo::source::set_sinkoption(const void *address, int32_t addrlen, aoo_id id,
                                      int32_t opt, void *ptr, int32_t size)
{
    ip_address addr((const sockaddr *)address, addrlen);

    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        switch (opt){
        // channel onset
        case AOO_OPT_CHANNELONSET:
        {
            CHECKARG(int32_t);
            auto chn = as<int32_t>(ptr);
            sink->channel.store(chn);
            LOG_VERBOSE("aoo_source: send to sink " << sink->id
                        << " on channel " << chn);
            break;
        }
        // unknown
        default:
            LOG_WARNING("aoo_source: unknown sink option " << opt);
            return AOO_ERROR_UNSPECIFIED;
        }
        return AOO_OK;
    } else {
        LOG_ERROR("aoo_source: couldn't set option " << opt
                  << " - sink not found!");
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error aoo_source_get_sinkoption(aoo_source *src, const void *address, int32_t addrlen,
                                    aoo_id id, int32_t opt, void *p, int32_t size)
{
    return src->get_sinkoption(address, addrlen, id, opt, p, size);
}

aoo_error aoo::source::get_sinkoption(const void *address, int32_t addrlen, aoo_id id,
                                      int32_t opt, void *p, int32_t size)
{
    ip_address addr((const sockaddr *)address, addrlen);

    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        switch (opt){
        // channel onset
        case AOO_OPT_CHANNELONSET:
            CHECKARG(int32_t);
            as<int32_t>(p) = sink->channel.load();
            break;
        // unknown
        default:
            LOG_WARNING("aoo_source: unsupported sink option " << opt);
            return AOO_ERROR_UNSPECIFIED;
        }
        return AOO_OK;
    } else {
        LOG_ERROR("aoo_source: couldn't get option " << opt
                  << " - sink " << id << " not found!");
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error aoo_source_setup(aoo_source *src, int32_t samplerate,
                           int32_t blocksize, int32_t nchannels){
    return src->setup(samplerate, blocksize, nchannels);
}

aoo_error aoo::source::setup(int32_t samplerate,
                             int32_t blocksize, int32_t nchannels){
    scoped_lock lock(update_mutex_); // writer lock!
    if (samplerate > 0 && blocksize > 0 && nchannels > 0)
    {
        if (samplerate != samplerate_ || blocksize != blocksize_ ||
            nchannels != nchannels_)
        {
            nchannels_ = nchannels;
            samplerate_ = samplerate;
            blocksize_ = blocksize;

            if (encoder_){
                update_audioqueue();

                if (need_resampling()){
                    update_resampler();
                }

                update_historybuffer();
            }

            // this will also implicitly reset the time DLL filter (see process())
            timer_.setup(samplerate_, blocksize_);

            // always start new stream
            start_new_stream();
        }

        return AOO_OK;
    } else {
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error aoo_source_add_sink(aoo_source *src, const void *address,
                              int32_t addrlen, aoo_id id, uint32_t flags) {
    return src->add_sink(address, addrlen, id, flags);
}

aoo_error aoo::source::add_sink(const void *address, int32_t addrlen,
                                aoo_id id, uint32_t flags)
{
    ip_address addr((const sockaddr *)address, addrlen);

    sink_lock lock(sinks_);
    // check if sink exists!
    if (find_sink(addr, id)){
        LOG_WARNING("aoo_source: sink already added!");
        return AOO_ERROR_UNSPECIFIED;
    }
    // add sink descriptor
    sinks_.emplace_front(addr, id, flags);
    // push format request
    formatrequestqueue_.push(format_request { addr, id, flags });

    return AOO_OK;
}

aoo_error aoo_source_remove_sink(aoo_source *src, const void *address,
                                 int32_t addrlen, aoo_id id) {
    return src->remove_sink(address, addrlen, id);
}

aoo_error aoo::source::remove_sink(const void *address, int32_t addrlen, aoo_id id){
    ip_address addr((const sockaddr *)address, addrlen);

    sink_lock lock(sinks_);
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it){
        if (it->address == addr && it->id == id){
            sinks_.erase(it);
            return AOO_OK;
        }
    }
    LOG_WARNING("aoo_source: sink not found!");
    return AOO_ERROR_UNSPECIFIED;
}

void aoo_source_remove_all(aoo_source *src) {
    src->remove_all();
}

void aoo::source::remove_all(){
    sink_lock lock(sinks_);
    sinks_.clear();
}

aoo_error aoo_source_handle_message(aoo_source *src, const char *data, int32_t n,
                                    const void *address, int32_t addrlen) {
    return src->handle_message(data, n, address, addrlen);
}

// /aoo/src/<id>/format <sink>
aoo_error aoo::source::handle_message(const char *data, int32_t n,
                                      const void *address, int32_t addrlen){
    if (!data){
        return AOO_OK; // nothing to update
    }

    try {
        ip_address addr((const sockaddr *)address, addrlen);

        osc::ReceivedPacket packet(data, n);
        osc::ReceivedMessage msg(packet);

        aoo_type type;
        aoo_id src;
        int32_t onset;
        auto err = aoo_parse_pattern(data, n, &type, &src, &onset);
        if (err != AOO_OK){
            LOG_WARNING("aoo_source: not an AoO message!");
            return AOO_ERROR_UNSPECIFIED;
        }
        if (type != AOO_TYPE_SOURCE){
            LOG_WARNING("aoo_source: not a source message!");
            return AOO_ERROR_UNSPECIFIED;
        }
        if (src != id()){
            LOG_WARNING("aoo_source: wrong source ID!");
            return AOO_ERROR_UNSPECIFIED;
        }

        auto pattern = msg.AddressPattern() + onset;
        if (!strcmp(pattern, AOO_MSG_FORMAT)){
            handle_format_request(msg, addr);
        } else if (!strcmp(pattern, AOO_MSG_DATA)){
            handle_data_request(msg, addr);
        } else if (!strcmp(pattern, AOO_MSG_INVITE)){
            handle_invite(msg, addr);
        } else if (!strcmp(pattern, AOO_MSG_UNINVITE)){
            handle_uninvite(msg, addr);
        } else if (!strcmp(pattern, AOO_MSG_PING)){
            handle_ping(msg, addr);
        } else {
            LOG_WARNING("unknown message " << pattern);
            return AOO_ERROR_UNSPECIFIED;
        }
        return AOO_OK;
    } catch (const osc::Exception& e){
        LOG_ERROR("aoo_source: exception in handle_message: " << e.what());
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error aoo_source_send(aoo_source *src, aoo_sendfn fn, void *user) {
    return src->send(fn, user);
}

// This method reads audio samples from the ringbuffer,
// encodes them and sends them to all sinks.
// We have to aquire both the update lock and the sink list lock
// and release both before calling the sink's send method
// to avoid possible deadlocks in the client code.
// We have to make a local copy of the sink list, but this should be
// rather cheap in comparison to encoding and sending the audio data.
aoo_error aoo::source::send(aoo_sendfn fn, void *user){
    if (state_.load() != stream_state::play){
        return AOO_OK; // nothing to do
    }

    sendfn func(fn, user);

    send_format(func);

    send_data(func);

    resend_data(func);

    send_ping(func);

    if (!sinks_.try_free()){
        LOG_DEBUG("aoo::source: try_free() would block");
    }

    return AOO_OK;
}

aoo_error aoo_source_process(aoo_source *src, const aoo_sample **data, int32_t n, uint64_t t) {
    return src->process(data, n, t);
}

aoo_error aoo::source::process(const aoo_sample **data, int32_t n, uint64_t t){
    auto state = state_.load();
    if (state == stream_state::stop){
        return AOO_ERROR_UNSPECIFIED; // pausing
    } else if (state == stream_state::start){
        // start -> play
        // the mutex should be uncontended most of the time.
        // although it is repeatedly locked in send(), the latter
        // returns early if we're not already playing.
        unique_lock lock(update_mutex_, std::try_to_lock_t{}); // writer lock!
        if (!lock.owns_lock()){
            dropped_++;
            LOG_VERBOSE("aoo::source::process() would block");
            return AOO_ERROR_UNSPECIFIED; // ?
        }

        resampler_.reset();

        audioqueue_.reset();
        srqueue_.reset();

        start_new_stream();

        // check if we have been stopped in the meantime
        auto expected = stream_state::start;
        if (!state_.compare_exchange_strong(expected, stream_state::play)){
            return AOO_ERROR_UNSPECIFIED; // pausing
        }
    }

    // the mutex should be available most of the time.
    // it is only locked exclusively when setting certain options,
    // e.g. changing the buffer size.
    shared_lock lock(update_mutex_, std::try_to_lock_t{}); // reader lock!
    if (!lock.owns_lock()){
        dropped_++;
        LOG_VERBOSE("aoo::source::process() would block");
        return AOO_ERROR_UNSPECIFIED; // ?
    }

    if (!encoder_){
        return AOO_ERROR_UNSPECIFIED;
    }

    // update time DLL filter
    double error;
    auto timerstate = timer_.update(t, error);
    if (timerstate == timer::state::reset){
        LOG_DEBUG("setup time DLL filter for source");
        dll_.setup(samplerate_, blocksize_, bandwidth_, 0);
        // it is safe to set 'lastpingtime' after updating
        // the timer, because in the worst case the ping
        // is simply sent the next time.
        lastpingtime_.store(-1e007); // force first ping
    } else if (timerstate == timer::state::error){
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

    // non-interleaved -> interleaved
    // only as many channels as current format needs
    auto nfchannels = encoder_->nchannels();
    auto nsamples = blocksize_ * nfchannels;
    auto buf = (aoo_sample *)alloca(nsamples * sizeof(aoo_sample));
    auto maxnchannels = std::min(nfchannels, nchannels_);
    for (int i = 0; i < maxnchannels; ++i){
        for (int j = 0; j < n; ++j){
            buf[j * nfchannels + i] = data[i][j];
        }
    }
    // zero remaining channels
    for (int i = maxnchannels; i < nfchannels; ++i){
        for (int j = 0; j < n; ++j){
            buf[j * nfchannels + i] = 0;
        }
    }

    if (need_resampling()){
        // go through resampler
        if (!resampler_.write(buf, nsamples)){
            // LOG_DEBUG("couldn't process");
            return AOO_ERROR_UNSPECIFIED;
        }
        while (audioqueue_.write_available() && srqueue_.write_available()){
            // copy audio samples
            if (!resampler_.read(audioqueue_.write_data(), audioqueue_.blocksize())){
                break;
            }
            audioqueue_.write_commit();

            // push samplerate
            auto ratio = (double)encoder_->samplerate() / (double)samplerate_;
            srqueue_.write(dll_.samplerate() * ratio);
        }
    } else {
        // bypass resampler
        if (audioqueue_.write_available() && srqueue_.write_available()){
            // copy audio samples
            std::copy(buf, buf + audioqueue_.blocksize(), audioqueue_.write_data());
            audioqueue_.write_commit();

            // push samplerate
            srqueue_.write(dll_.samplerate());
        } else {
            // LOG_DEBUG("couldn't process");
        }
    }
    return AOO_OK;
}

aoo_bool aoo_source_events_available(aoo_source *src){
    return src->events_available();
}

bool aoo::source::events_available(){
    return !eventqueue_.empty();
}

aoo_error aoo_source_poll_events(aoo_source *src,
                                 aoo_eventhandler fn, void *user){
    return src->poll_events(fn, user);
}

aoo_error aoo::source::poll_events(aoo_eventhandler fn, void *user){
    // always thread-safe
    event e;
    while (eventqueue_.try_pop(e) > 0){
        fn(user, &e.event_);
    }
    return AOO_OK;
}

namespace aoo {

/*///////////////////////// source ////////////////////////////////*/

sink_desc * source::find_sink(const ip_address& addr, aoo_id id){
    for (auto& sink : sinks_){
        if (sink.address == addr && sink.id == id){
            return &sink;
        }
    }
    return nullptr;
}

aoo_error source::set_format(aoo_format &f){
    std::unique_ptr<encoder> new_encoder;
    {
        // create a new encoder if necessary
        shared_scoped_lock lock(update_mutex_); // reader lock!
        if (!encoder_ || strcmp(encoder_->name(), f.codec)){
            auto codec = aoo::find_codec(f.codec);
            if (codec){
                new_encoder = codec->create_encoder();
                if (!new_encoder){
                    LOG_ERROR("couldn't create encoder!");
                    return AOO_ERROR_UNSPECIFIED;
                }
            } else {
                LOG_ERROR("codec '" << f.codec << "' not supported!");
                return AOO_ERROR_UNSPECIFIED;
            }
        }
    }

    scoped_lock lock(update_mutex_); // writer lock!
    if (new_encoder){
        encoder_ = std::move(new_encoder);
    }

    auto err = encoder_->set_format(f);
    if (err == AOO_OK){
        update_audioqueue();

        if (need_resampling()){
            update_resampler();
        }

        update_historybuffer();

        start_new_stream();
    }
    return err;
}

int32_t source::make_salt(){
    thread_local std::random_device dev;
    thread_local std::mt19937 mt(dev());
    std::uniform_int_distribution<int32_t> dist;
    return dist(mt);
}

bool source::need_resampling() const {
    return blocksize_ != encoder_->blocksize() || samplerate_ != encoder_->samplerate();
}

void source::start_new_stream(){
    // implicitly reset time DLL to be on the safe side
    timer_.reset();

    // Start new sequence and resend format.
    // We naturally want to do this when setting the format,
    // but it's good to also do it in setup() to eliminate
    // any timing gaps.
    salt_ = make_salt();
    sequence_ = 0;
    dropped_ = 0;

    if (!history_.is_empty()){
        history_.clear(); // !
    }

    sink_lock lock(sinks_);
    for (auto& sink : sinks_){
        formatrequestqueue_.push(format_request { sink });
    }
}

void source::update_audioqueue(){
    if (encoder_){
        // recalculate buffersize from ms to samples
        int32_t bufsize = (double)buffersize_.load() * 0.001 * encoder_->samplerate();
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        // minimum buffer size increases when upsampling!
        int32_t minbuffers = std::ceil((double)encoder_->samplerate() / (double)samplerate_);
        nbuffers = std::max<int32_t>(nbuffers, minbuffers);
        LOG_DEBUG("aoo_source: buffersize (ms): " << buffersize_.load()
                  << ", samples: " << bufsize << ", nbuffers = " << nbuffers);

        // resize audio buffer
        auto nsamples = encoder_->blocksize() * encoder_->nchannels();
        audioqueue_.resize(nsamples, nbuffers);

        srqueue_.resize(nbuffers);
    }
}

void source::update_resampler(){
    if (encoder_){
        resampler_.setup(blocksize_, encoder_->blocksize(),
                         samplerate_, encoder_->samplerate(), encoder_->nchannels());
    }
}

void source::update_historybuffer(){
    if (encoder_){
        int32_t bufsize = (double)resend_buffersize_.load() * 0.001 * encoder_->samplerate();
        auto d = div(bufsize, encoder_->blocksize());
        int32_t nbuffers = d.quot + (d.rem != 0); // round up
        history_.resize(nbuffers);
        LOG_DEBUG("aoo_source: history buffersize (ms): " << resend_buffersize_.load()
                  << ", samples: " << bufsize << ", nbuffers = " << nbuffers);

    }
}

void source::send_format(sendfn& fn){
    if (formatrequestqueue_.empty()){
        return;
    }

    shared_lock updatelock(update_mutex_); // reader lock!

    if (!encoder_){
        return;
    }

    int32_t salt = salt_;

    aoo_format_storage f;
    f.header.size = sizeof(aoo_format_storage); // !
    if (encoder_->get_format(f.header) != AOO_OK){
        return;
    }

    char options[AOO_CODEC_MAXSETTINGSIZE];
    int32_t size = sizeof(options);
    if (encoder_->serialize(f.header, options, size) != AOO_OK){
        return;
    }

    updatelock.unlock();

    format_request r;
    while (formatrequestqueue_.try_pop(r)){
        // /aoo/sink/<id>/format <src> <version> <salt> <numchannels> <samplerate> <blocksize> <codec> <options...>

        LOG_DEBUG("send format to " << r.id << " (salt = " << salt << ")");

        char buf[AOO_MAXPACKETSIZE];
        osc::OutboundPacketStream msg(buf, sizeof(buf));

        const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
                + AOO_MSG_SINK_LEN + 16 + AOO_MSG_FORMAT_LEN;
        char address[max_addr_size];
        snprintf(address, sizeof(address), "%s%s/%d%s",
                 AOO_MSG_DOMAIN, AOO_MSG_SINK, r.id, AOO_MSG_FORMAT);

        msg << osc::BeginMessage(address) << id() << (int32_t)make_version()
            << salt << f.header.nchannels << f.header.samplerate << f.header.blocksize
            << f.header.codec << osc::Blob(options, size) << osc::EndMessage;

        fn(msg.Data(), msg.Size(), r.address, r.flags);
    }
}

void source::resend_data(sendfn& fn){
    shared_lock updatelock(update_mutex_); // reader lock!
    if (!history_.capacity()){
        return;
    }

    data_request r;
    while (datarequestqueue_.try_pop(r)){
        auto salt = salt_;
        if (salt != r.salt){
            // outdated request
            continue;
        }

        auto block = history_.find(r.sequence);
        if (block){
            aoo::data_packet d;
            d.sequence = block->sequence;
            d.samplerate = block->samplerate;
            d.channel = block->channel;
            d.totalsize = block->size();
            d.nframes = block->num_frames();
            // We use a buffer on the heap because blocks and even frames
            // can be quite large and we don't want them to sit on the stack.
            if (r.frame < 0){
                // Copy whole block and save frame pointers.
                sendbuffer_.resize(d.totalsize);
                char *buf = sendbuffer_.data();
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
                        LOG_ERROR("empty frame!");
                    }
                }
                // unlock before sending
                updatelock.unlock();

                // send frames to sink
                for (int i = 0; i < d.nframes; ++i){
                    d.framenum = i;
                    d.data = frameptr[i];
                    d.size = framesize[i];
                    send_data(fn, r, salt, d);
                }

                // lock again
                updatelock.lock();
            } else {
                // Copy a single frame
                if (r.frame >= 0 && r.frame < d.nframes){
                    int32_t size = block->frame_size(r.frame);
                    sendbuffer_.resize(size);
                    block->get_frame(r.frame, sendbuffer_.data(), size);
                    // unlock before sending
                    updatelock.unlock();

                    // send frame to sink
                    d.framenum = r.frame;
                    d.data = sendbuffer_.data();
                    d.size = size;
                    send_data(fn, r, salt, d);

                    // lock again
                    updatelock.lock();
                } else {
                    LOG_ERROR("frame number " << r.frame << " out of range!");
                }
            }
        } else {
            LOG_VERBOSE("couldn't find block " << r.sequence);
        }
    }
}

void source::send_data(sendfn& fn){
    shared_lock updatelock(update_mutex_); // reader lock!
    if (!encoder_){
        return;
    }

    data_packet d;
    int32_t salt = salt_;

    // *first* check for dropped blocks
    // NOTE: there's no ABA problem because the variable will only be decremented in this method.
    if (dropped_ > 0){
        // send empty block
        d.sequence = sequence_++;
        d.samplerate = encoder_->samplerate(); // use nominal samplerate
        d.channel = 0;
        d.totalsize = 0;
        d.nframes = 0;
        d.framenum = 0;
        d.data = nullptr;
        d.size = 0;
        // now we can unlock
        updatelock.unlock();

        // we only free sources in this thread, so we don't have to lock
    #if 0
        // this is not a real lock, so we don't have worry about dead locks
        sink_lock lock(sinks_);
    #endif
        // send block to sinks
        for (auto& sink : sinks_){
            d.channel = sink.channel; // !
            send_data(fn, sink, salt, d);
        }
        --dropped_;
    } else if (audioqueue_.read_available() && srqueue_.read_available()){
        // always read samplerate from ringbuffer!
        srqueue_.read(d.samplerate);

        if (!sinks_.empty()){
            // copy and convert audio samples to blob data
            auto nchannels = encoder_->nchannels();
            auto blocksize = encoder_->blocksize();
            sendbuffer_.resize(sizeof(double) * nchannels * blocksize); // overallocate

            d.totalsize = sendbuffer_.size();
            auto err = encoder_->encode(audioqueue_.read_data(), audioqueue_.blocksize(),
                sendbuffer_.data(), d.totalsize);

            audioqueue_.read_commit();

            if (err != AOO_OK){
                return;
            }

            if (d.totalsize > 0){
                d.sequence = sequence_++;

                // calculate number of frames
                auto maxpacketsize = packetsize_ - AOO_DATA_HEADERSIZE;
                auto dv = div(d.totalsize, maxpacketsize);
                d.nframes = dv.quot + (dv.rem != 0);

                // save block
                history_.push()->set(d.sequence, d.samplerate, d.channel, sendbuffer_.data(),
                                     d.totalsize, d.nframes, maxpacketsize);

                // unlock before sending!
                updatelock.unlock();

                // from here on we don't hold any lock!

                // send a single frame to all sinks
                // /AoO/<sink>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <numpackets> <packetnum> <data>
                auto dosend = [&](int32_t frame, const char* data, auto n){
                    d.framenum = frame;
                    d.data = data;
                    d.size = n;
                    // we only free sources in this thread, so we don't have to lock
                #if 0
                    // this is not a real lock, so we don't have worry about dead locks
                    sink_lock lock(sinks_);
                #endif
                    // send block to sinks
                    for (auto& sink : sinks_){
                        d.channel = sink.channel; // !
                        send_data(fn, sink, salt, d);
                    }
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
            } else {
                LOG_WARNING("aoo_source: couldn't encode audio data!");
                return;
            }
        } else {
            // drain buffer anyway
            audioqueue_.read_commit();
            return;
        }
    } else {
        // LOG_DEBUG("couldn't send");
        return;
    }

    // handle overflow (with 64 samples @ 44.1 kHz this happens every 36 days)
    // for now just force a reset by changing the salt, LATER think how to handle this better
    if (sequence_ == INT32_MAX){
        // we can safely lock the mutex because it is always unlocked
        // before the sequence numbers is incremented.
        unique_lock lock(update_mutex_); // writer lock
        salt_ = make_salt();
    }
}

// /aoo/sink/<id>/data <src> <salt> <seq> <sr> <channel_onset> <totalsize> <nframes> <frame> <data>

void source::send_data(sendfn& fn, const endpoint& ep, int32_t salt, const aoo::data_packet& d) const {
    char buf[AOO_MAXPACKETSIZE];
    osc::OutboundPacketStream msg(buf, sizeof(buf));

    const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
            + AOO_MSG_SINK_LEN + 16 + AOO_MSG_DATA_LEN;
    char address[max_addr_size];
    snprintf(address, sizeof(address), "%s%s/%d%s",
             AOO_MSG_DOMAIN, AOO_MSG_SINK, ep.id, AOO_MSG_DATA);

    msg << osc::BeginMessage(address) << id() << salt << d.sequence << d.samplerate
        << d.channel << d.totalsize << d.nframes << d.framenum << osc::Blob(d.data, d.size)
        << osc::EndMessage;

    LOG_DEBUG("send block: seq = " << d.sequence << ", sr = " << d.samplerate
              << ", chn = " << d.channel << ", totalsize = " << d.totalsize
              << ", nframes = " << d.nframes << ", frame = " << d.framenum << ", size " << d.size);

    fn(msg.Data(), msg.Size(), ep.address, ep.flags);
}

void source::send_ping(sendfn& fn){
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

            char buf[AOO_MAXPACKETSIZE];
            osc::OutboundPacketStream msg(buf, sizeof(buf));

            const int32_t max_addr_size = AOO_MSG_DOMAIN_LEN
                    + AOO_MSG_SINK_LEN + 16 + AOO_MSG_PING_LEN;
            char address[max_addr_size];
            snprintf(address, sizeof(address), "%s%s/%d%s",
                     AOO_MSG_DOMAIN, AOO_MSG_SINK, sink.id, AOO_MSG_PING);

            msg << osc::BeginMessage(address) << id() << osc::TimeTag(tt)
                << osc::EndMessage;

            fn(msg.Data(), msg.Size(), sink.address, sink.flags);
        }

        lastpingtime_.store(elapsed);
    }
}

void source::handle_format_request(const osc::ReceivedMessage& msg,
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
        formatrequestqueue_.push(format_request { addr, id, sink->flags });
    } else {
        LOG_VERBOSE("ignoring '" << AOO_MSG_FORMAT << "' message: sink not found");
    }
}

void source::handle_data_request(const osc::ReceivedMessage& msg,
                                 const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    auto id = (it++)->AsInt32();
    auto salt = (it++)->AsInt32();

    LOG_DEBUG("handle data request");

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    auto sink = find_sink(addr, id);
    if (sink){
        // get pairs of [seq, frame]
        int npairs = (msg.ArgumentCount() - 2) / 2;
        while (npairs--){
            auto seq = (it++)->AsInt32();
            auto frame = (it++)->AsInt32();
            datarequestqueue_.push(data_request{ addr, id, sink->flags,
                                                 salt, seq, frame });
        }
    } else {
        LOG_VERBOSE("ignoring '" << AOO_MSG_DATA << "' message: sink not found");
    }
}

void source::handle_invite(const osc::ReceivedMessage& msg,
                           const ip_address& addr)
{
    auto id = msg.ArgumentsBegin()->AsInt32();

    LOG_DEBUG("handle invite");

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    if (!find_sink(addr, id)){
        // push "invite" event
        event e(AOO_INVITE_EVENT, addr, id);
        eventqueue_.push(e);
    } else {
        LOG_VERBOSE("ignoring '" << AOO_MSG_INVITE << "' message: sink already added");
    }
}

void source::handle_uninvite(const osc::ReceivedMessage& msg,
                             const ip_address& addr)
{
    auto id = msg.ArgumentsBegin()->AsInt32();

    LOG_DEBUG("handle uninvite");

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    if (find_sink(addr, id)){
        // push "uninvite" event
        event e(AOO_UNINVITE_EVENT, addr, id);
        eventqueue_.push(e);
    } else {
        LOG_VERBOSE("ignoring '" << AOO_MSG_UNINVITE << "' message: sink not found");
    }
}

void source::handle_ping(const osc::ReceivedMessage& msg,
                         const ip_address& addr)
{
    auto it = msg.ArgumentsBegin();
    aoo_id id = (it++)->AsInt32();
    time_tag tt1 = (it++)->AsTimeTag();
    time_tag tt2 = (it++)->AsTimeTag();
    int32_t lost_blocks = (it++)->AsInt32();

    LOG_DEBUG("handle ping");

    // check if sink exists (not strictly necessary, but might help catch errors)
    sink_lock lock(sinks_);
    if (find_sink(addr, id)){
        // push "ping" event
        event e(AOO_PING_EVENT, addr, id);
        e.ping.tt1 = tt1;
        e.ping.tt2 = tt2;
        e.ping.lost_blocks = lost_blocks;
    #if 0
        e.ping.tt3 = timer_.get_absolute(); // use last stream time
    #else
        e.ping.tt3 = aoo::time_tag::now(); // use real system time
    #endif
        eventqueue_.push(e);
    } else {
        LOG_VERBOSE("ignoring '" << AOO_MSG_PING << "' message: sink not found");
    }
}

} // aoo
