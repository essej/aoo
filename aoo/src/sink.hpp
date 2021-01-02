/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo.hpp"

#include "common/lockfree.hpp"
#include "common/net_utils.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "buffer.hpp"
#include "codec.hpp"
#include "imp.hpp"
#include "resampler.hpp"
#include "timer.hpp"
#include "time_dll.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

namespace aoo {

struct stream_state {
    stream_state() = default;
    stream_state(stream_state&& other) = delete;
    stream_state& operator=(stream_state&& other) = delete;

    void reset(){
        lost_ = 0;
        reordered_ = 0;
        resent_ = 0;
        gap_ = 0;
        state_ = AOO_STREAM_STATE_STOP;
        underrun_ = false;
        xrun_ = 0;
    }

    void add_lost(int32_t n) { lost_ += n; lost_since_ping_ += n; }
    int32_t get_lost() { return lost_.exchange(0); }
    int32_t get_lost_since_ping() { return lost_since_ping_.exchange(0); }

    void add_reordered(int32_t n) { reordered_ += n; }
    int32_t get_reordered() { return reordered_.exchange(0); }

    void add_resent(int32_t n) { resent_ += n; }
    int32_t get_resent() { return resent_.exchange(0); }

    void add_gap(int32_t n) { gap_ += n; }
    int32_t get_gap() { return gap_.exchange(0); }

    bool update_state(aoo_stream_state state){
        auto last = state_.exchange(state);
        return state != last;
    }
    aoo_stream_state get_state(){
        return state_;
    }

    void add_xrun(int32_t nblocks) { xrun_ += nblocks; }
    int32_t get_xrun() { return xrun_.exchange(0); }

    void set_underrun() { underrun_.store(true); }
    bool have_underrun() { return underrun_.exchange(false); }
private:
    std::atomic<int32_t> lost_since_ping_{0};
    std::atomic<int32_t> lost_{0};
    std::atomic<int32_t> reordered_{0};
    std::atomic<int32_t> resent_{0};
    std::atomic<int32_t> gap_{0};
    std::atomic<int32_t> xrun_{0};
    std::atomic<aoo_stream_state> state_{AOO_STREAM_STATE_STOP};
    std::atomic<bool> underrun_{false};
};

struct block_info {
    double sr;
    int32_t channel;
};

class source_desc;

struct event
{
    event() = default;

    event(aoo_event_type type, const source_desc& desc);

    union {
        aoo_event_type type_;
        aoo_event event_;
        aoo_source_event source;
        aoo_format_change_event format;
        aoo_ping_event ping;
        aoo_stream_state_event source_state;
        aoo_block_lost_event block_loss;
        aoo_block_reordered_event block_reorder;
        aoo_block_resent_event block_resend;
        aoo_block_gap_event block_gap;
    };
};

struct source_event {
    source_event() = default;

    source_event(aoo_event_type _type, const source_desc& desc);

    aoo_event_type type;
    ip_address address;
    aoo_id id;
};

enum class request_type {
    unknown,
    invite,
    uninvite,
    uninvite_all
};

// used in 'sink'
struct source_request {
    source_request() = default;

    source_request(request_type _type)
        : type(_type) {}

    source_request(request_type _type, const ip_address& _addr, aoo_id _id)
        : type(_type), address(_addr), id(_id) {}

    request_type type;
    ip_address address;
    aoo_id id = -1;
};

class sink_imp;

enum class source_state {
    idle,
    stream,
    invite,
    uninvite
};

class source_desc {
public:
    source_desc(const ip_address& addr, aoo_id id, double time);
    source_desc(const source_desc& other) = delete;
    source_desc& operator=(const source_desc& other) = delete;

    ~source_desc();

    // getters
    aoo_id id() const { return id_; }

    const ip_address& address() const { return addr_; }

    bool match(const ip_address& addr, aoo_id id) const {
        return (addr_ == addr) && (id_ == id);
    }

    bool is_active(const sink_imp& s) const;

    bool is_inviting() const {
        return state_.load() == source_state::invite;
    }

    bool has_events() const {
        return !eventqueue_.empty();
    }

    int32_t poll_events(sink_imp& s, aoo_eventhandler fn, void *user);

    aoo_error get_format(aoo_format& format);

    // methods
    void reset(const sink_imp& s);

    aoo_error handle_format(const sink_imp& s, int32_t salt, const aoo_format& f,
                            const char *settings, int32_t size, const sendfn& reply);

    aoo_error handle_data(const sink_imp& s, int32_t salt, const aoo::data_packet& d,
                          const sendfn& reply);

    aoo_error handle_ping(const sink_imp& s, time_tag tt, const sendfn& reply);

    void update(const sink_imp& s, const sendfn& fn);

    bool process(const sink_imp& s, aoo_sample *buffer, int32_t nsamples, time_tag tt);

    void add_xrun(int32_t n){ streamstate_.add_xrun(n); }

    void invite(const sink_imp& s);

    void uninvite(const sink_imp& s);
private:
    void update(const sink_imp& s);

    // handle messages
    void recover(const char *reason, int32_t n = 0);

    bool check_packet(const data_packet& d);

    bool add_packet(const data_packet& d);

    void process_blocks();

    void check_missing_blocks(const sink_imp& s, const sendfn& reply,
                              aoo::shared_lock& lock);

    // send messages
    void send_format_request(const sink_imp& s, const sendfn& fn);

    void send_invitation(const sink_imp& s, const sendfn& fn);

    void send_uninvitation(const sink_imp& s, const sendfn& fn);

    // data
    const ip_address addr_;
    const aoo_id id_;
    std::atomic<uint32_t> flags_{0};
    uint32_t flags() const {
        return flags_.load(std::memory_order_acquire);
    }
    int32_t salt_ = -1; // start with invalid stream ID!
    std::atomic<source_state> state_;
    double state_time_ = 0.0;
    // audio decoder
    std::unique_ptr<aoo::decoder> decoder_;
    // state
    int32_t channel_ = 0; // recent channel onset
    double samplerate_ = 0; // recent samplerate
    stream_state streamstate_;
    double dropped_ = 0;
    std::atomic<double> last_packet_time_{0};
    // queues and buffers
    jitter_buffer jitterbuffer_;
    lockfree::spsc_queue<aoo_sample, aoo::allocator<aoo_sample>> audioqueue_;
    lockfree::spsc_queue<block_info, aoo::allocator<block_info>> infoqueue_;
    // events
    lockfree::unbounded_mpsc_queue<event, aoo::allocator<event>> eventqueue_;
    void send_event(const sink_imp& s, const event& e, aoo_thread_level level);
    // resampler
    dynamic_resampler resampler_;
    // thread synchronization
    aoo::shared_mutex mutex_; // LATER replace with a spinlock?
};

class sink_imp final : public sink {
public:
    sink_imp(aoo_id id, uint32_t flags);

    ~sink_imp(){}

    aoo_error setup(int32_t samplerate, int32_t blocksize, int32_t nchannels) override;

    aoo_error invite_source(const void *address, int32_t addrlen, aoo_id id) override;

    aoo_error uninvite_source(const void *address, int32_t addrlen, aoo_id id) override;

    aoo_error uninvite_all() override;

    aoo_error handle_message(const char *data, int32_t n,
                             const void *address, int32_t addrlen,
                             aoo_sendfn fn, void *user) override;

    aoo_error update(aoo_sendfn fn, void *user) override;

    aoo_error process(aoo_sample **data, int32_t nsamples, uint64_t t) override;

    aoo_error set_eventhandler(aoo_eventhandler fn, void *user, int32_t mode) override;

    aoo_bool events_available() override;

    aoo_error poll_events() override;

    aoo_error set_option(int32_t opt, void *ptr, int32_t size) override;

    aoo_error get_option(int32_t opt, void *ptr, int32_t size) override;

    aoo_error set_source_option(const void *address, int32_t addrlen, aoo_id id,
                             int32_t opt, void *ptr, int32_t size) override;

    aoo_error get_source_option(const void *address, int32_t addrlen, aoo_id id,
                             int32_t opt, void *ptr, int32_t size) override;
    // getters
    aoo_id id() const { return id_.load(std::memory_order_relaxed); }

    int32_t nchannels() const { return nchannels_; }

    int32_t samplerate() const { return samplerate_; }

    double real_samplerate() const { return dll_.samplerate(); }

    int32_t blocksize() const { return blocksize_; }

    int32_t buffersize() const { return buffersize_.load(std::memory_order_relaxed); }

    int32_t packetsize() const { return packetsize_.load(std::memory_order_relaxed); }

    bool resend_enabled() const { return resend_enabled_.load(std::memory_order_relaxed); }

    float resend_interval() const { return resend_interval_.load(std::memory_order_relaxed); }

    int32_t resend_maxnumframes() const { return resend_maxnumframes_.load(std::memory_order_relaxed); }

    float source_timeout() const { return source_timeout_.load(std::memory_order_relaxed); }

    double elapsed_time() const { return timer_.get_elapsed(); }

    time_tag absolute_time() const { return timer_.get_absolute(); }

    aoo_event_mode event_mode() const { return eventmode_; }
private:
    // settings
    std::atomic<aoo_id> id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    // buffer for summing source audio output
    std::vector<aoo_sample, aoo::allocator<aoo_sample>> buffer_;
    // the sources
    using source_list = lockfree::simple_list<source_desc, aoo::allocator<source_desc>>;
    using source_lock = std::unique_lock<source_list>;
    source_list sources_;
    // timing
    std::atomic<float> bandwidth_{ AOO_TIMEFILTER_BANDWIDTH };
    time_dll dll_;
    timer timer_;
    // options
    std::atomic<int32_t> buffersize_{ AOO_SINK_BUFSIZE };
    std::atomic<int32_t> packetsize_{ AOO_PACKETSIZE };
    std::atomic<float> resend_interval_{ AOO_RESEND_INTERVAL * 0.001 };
    std::atomic<int32_t> resend_maxnumframes_{ AOO_RESEND_MAXNUMFRAMES };
    std::atomic<float> source_timeout_{ AOO_SOURCE_TIMEOUT * 0.001 };
    std::atomic<bool> resend_enabled_{AOO_RESEND_ENABLE};
    // events
    lockfree::unbounded_mpsc_queue<source_event, aoo::allocator<source_event>> eventqueue_;
    void send_event(const source_event& e, aoo_thread_level level);
public:
    void call_event(const event& e, aoo_thread_level level) const;
private:
    aoo_eventhandler eventhandler_ = nullptr;
    void *eventcontext_ = nullptr;
    aoo_event_mode eventmode_ = AOO_EVENT_NONE;
    // requests
    lockfree::unbounded_mpsc_queue<source_request, aoo::allocator<source_request>> requestqueue_;
    void push_request(const source_request& r){
        requestqueue_.push(r);
    }
    // queue memory deallocation
    struct sized_deleter {
        sized_deleter(size_t size = 0)
            : size_(size){}
        void operator()(void *ptr){
            aoo::deallocate(ptr, size_);
        }
    private:
        size_t size_;
    };
    using mem_ptr = std::unique_ptr<void, sized_deleter>;
    lockfree::unbounded_mpsc_queue<mem_ptr, aoo::allocator<mem_ptr>> memqueue_;
public:
    void sched_free(void *ptr, size_t size){
        memqueue_.push(mem_ptr(ptr, sized_deleter(size)));
    }
private:
    // helper methods
    source_desc *find_source(const ip_address& addr, aoo_id id);

    source_desc *add_source(const ip_address& addr, aoo_id id);

    void reset_sources();

    aoo_error handle_format_message(const osc::ReceivedMessage& msg,
                                    const ip_address& addr, const sendfn& reply);

    aoo_error handle_data_message(const osc::ReceivedMessage& msg,
                                  const ip_address& addr, const sendfn& reply);

    aoo_error handle_ping_message(const osc::ReceivedMessage& msg,
                                  const ip_address& addr, const sendfn& reply);
};

} // aoo
