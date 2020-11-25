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
        state_ = AOO_SOURCE_STATE_STOP;
        underrun_ = false;
        xrun_ = 0;
        format_ = false;
        invite_ = NONE;
        pingtime1_ = 0;
        pingtime2_ = 0;
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

    bool update_state(aoo_source_state state){
        auto last = state_.exchange(state);
        return state != last;
    }
    aoo_source_state get_state(){
        return state_;
    }

    void set_ping(time_tag t1, time_tag t2){
        pingtime1_.store(t1);
        pingtime2_.store(t2);
    }

    bool need_ping(time_tag& t1, time_tag& t2){
        // check pingtime2 because it ensures that pingtime1 has been set
        auto pingtime2 = pingtime2_.exchange(0);
        if (pingtime2){
            t1 = pingtime1_.load();
            t2 = pingtime2;
            return true;
        } else {
            return false;
        }
    }

    void add_xrun(int32_t nblocks) { xrun_ += nblocks; }
    int32_t get_xrun() { return xrun_.exchange(0); }

    void set_underrun() { underrun_.store(true); }
    bool have_underrun() { return underrun_.exchange(false); }

    void request_format() { format_ = true; }
    bool need_format() { return format_.exchange(false); }

    enum invitation_state {
        NONE = 0,
        INVITE = 1,
        UNINVITE = 2,
    };

    void request_invitation(invitation_state state) { invite_ = state; }
    invitation_state get_invitation_state() { return invite_.exchange(NONE); }
private:
    std::atomic<int32_t> lost_since_ping_{0};
    std::atomic<int32_t> lost_{0};
    std::atomic<int32_t> reordered_{0};
    std::atomic<int32_t> resent_{0};
    std::atomic<int32_t> gap_{0};
    std::atomic<int32_t> xrun_{0};
    std::atomic<aoo_source_state> state_{AOO_SOURCE_STATE_STOP};
    std::atomic<invitation_state> invite_{NONE};
    std::atomic<bool> underrun_{false};
    std::atomic<bool> format_{false};
    std::atomic<uint64_t> pingtime1_;
    std::atomic<uint64_t> pingtime2_;
};

struct block_info {
    double sr;
    int32_t channel;
};

struct event
{
    event() = default;

    event(aoo_event_type type, const ip_address& addr, aoo_id id){
        memcpy(addr_, addr.address(), addr.length());
        source.type = type;
        source.address = addr_;
        source.addrlen = addr.length();
        source.id = id;
    }

    event(const event& other){
        memcpy(this, &other, sizeof(event)); // ugh
        source.address = addr_;
    }

    event& operator=(const event& other){
        memcpy(this, &other, sizeof(event)); // ugh
        source.address = addr_;
        return *this;
    }

    union {
        aoo_event_type type_;
        aoo_event event_;
        aoo_source_event source;
        aoo_ping_event ping;
        aoo_source_state_event source_state;
        aoo_block_lost_event block_loss;
        aoo_block_reordered_event block_reorder;
        aoo_block_resent_event block_resend;
        aoo_block_gap_event block_gap;
    };
private:
    char addr_[ip_address::max_length];
};

enum class request_type {
    invite,
    uninvite,
    uninvite_all
};

struct request {
    request() = default;

    request(request_type _type)
        : type(_type) {}

    request(request_type _type, const ip_address& _addr, aoo_id _id)
        : type(_type), address(_addr), id(_id) {}

    request_type type;
    ip_address address;
    aoo_id id = -1;
};

class sink;

class source_desc {
public:
    source_desc(const ip_address& addr, aoo_id id, int32_t salt);
    source_desc(const source_desc& other) = delete;
    source_desc& operator=(const source_desc& other) = delete;

    // getters
    aoo_id id() const { return id_; }

    const ip_address& address() const { return addr_; }

    bool match(const ip_address& addr, aoo_id id) const {
        return (addr_ == addr) && (id_ == id);
    }

    bool is_active(const sink& s) const;

    bool has_events() const {
        return !eventqueue_.empty();
    }

    int32_t get_format(aoo_format_storage& format);

    // methods
    void reset(const sink& s);

    int32_t handle_format(const sink& s, int32_t salt, const aoo_format& f,
                          const char *settings, int32_t size);

    int32_t handle_data(const sink& s, int32_t salt,
                                     const aoo::data_packet& d);

    int32_t handle_ping(const sink& s, time_tag tt);

    int32_t poll_events(aoo_eventhandler fn, void *user);

    bool send(const sink& s);

    bool decode(const sink& s);

    bool process(const sink& s, aoo_sample *buffer, int32_t nsamples, time_tag tt);

    void add_xrun(int32_t n){ streamstate_.add_xrun(n); }

    void request_format(){ streamstate_.request_format(); }

    void request_invite(){ streamstate_.request_invitation(stream_state::INVITE); }

    void request_uninvite(){ streamstate_.request_invitation(stream_state::UNINVITE); }
private:
    struct data_request {
        int32_t sequence;
        int32_t frame;
    };
    void update(const sink& s);

    // handle messages
    void recover(const char *reason, int32_t n = 0);

    bool check_packet(const data_packet& d);

    bool add_packet(const data_packet& d);

    void process_blocks();

    void check_missing_blocks(const sink& s);

    // send messages
    bool send_format_request(const sink& s);

    int32_t send_data_request(const sink& s);

    bool send_notifications(const sink& s);

    // data
    ip_address addr_;
    aoo_id id_;
    int32_t salt_;
    // audio decoder
    std::unique_ptr<aoo::decoder> decoder_;
    // state
    int32_t channel_ = 0; // recent channel onset
    double samplerate_ = 0; // recent samplerate
    stream_state streamstate_;
    double dropped_ = 0;
    mutable time_tag lastprocesstime_;
    // queues and buffers
    jitter_buffer jitterbuffer_;
    lockfree::spsc_queue<aoo_sample> audioqueue_;
    lockfree::spsc_queue<block_info> infoqueue_;
    lockfree::unbounded_mpsc_queue<data_request> resendqueue_;
    lockfree::unbounded_mpsc_queue<event> eventqueue_;
    void push_event(const event& e){
        eventqueue_.push(e);
    }
    // resampler
    dynamic_resampler resampler_;
    // thread synchronization
    aoo::shared_mutex mutex_; // LATER replace with a spinlock?
};

class sink final : public isink {
public:
    sink(aoo_id id, aoo_replyfn replyfn, void *user);

    ~sink(){}

    int32_t setup(int32_t samplerate, int32_t blocksize, int32_t nchannels) override;

    int32_t invite_source(const void *address, int32_t addrlen, aoo_id id) override;

    int32_t uninvite_source(const void *address, int32_t addrlen, aoo_id id) override;

    int32_t uninvite_all() override;

    int32_t handle_message(const char *data, int32_t n,
                           const void *address, int32_t addrlen) override;

    int32_t send() override;

    int32_t decode() override;

    int32_t process(aoo_sample **data, int32_t nsamples, uint64_t t) override;

    int32_t events_available() override;

    int32_t poll_events(aoo_eventhandler fn, void *user) override;

    int32_t set_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t get_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t set_sourceoption(const void *address, int32_t addrlen, aoo_id id,
                             int32_t opt, void *ptr, int32_t size) override;

    int32_t get_sourceoption(const void *address, int32_t addrlen, aoo_id id,
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

    int32_t do_send(const char *data, int32_t size, const ip_address& addr) const {
        return replyfn_(user_, data, size, addr.address(), addr.length());
    }
private:
    // settings
    std::atomic<aoo_id> id_;
    aoo_replyfn replyfn_;
    void *user_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    // buffer for summing source audio output
    std::vector<aoo_sample> buffer_;
    // options
    std::atomic<int32_t> buffersize_{ AOO_SINK_BUFSIZE };
    std::atomic<int32_t> packetsize_{ AOO_PACKETSIZE };
    std::atomic<bool> resend_enabled_{AOO_RESEND_ENABLE};
    std::atomic<float> resend_interval_{ AOO_RESEND_INTERVAL * 0.001 };
    std::atomic<int32_t> resend_maxnumframes_{ AOO_RESEND_MAXNUMFRAMES };
    std::atomic<float> source_timeout_{ AOO_SOURCE_TIMEOUT * 0.001 };
    // the sources
    lockfree::simple_list<source_desc> sources_;
    shared_mutex source_mutex_;
    // timing
    std::atomic<float> bandwidth_{ AOO_TIMEFILTER_BANDWIDTH };
    time_dll dll_;
    timer timer_;
    // events
    lockfree::unbounded_mpsc_queue<event> eventqueue_;
    void push_event(const event& e){
        eventqueue_.push(e);
    }
    // requests
    lockfree::unbounded_mpsc_queue<request> requestqueue_;
    void push_request(const request& r){
        requestqueue_.push(r);
    }
    // helper methods
    source_desc *find_source(const ip_address& addr, aoo_id id);

    source_desc *add_source(const ip_address& addr, aoo_id id, int32_t salt);

    void reset_sources();

    int32_t handle_format_message(const osc::ReceivedMessage& msg,
                                  const ip_address& addr);

    int32_t handle_data_message(const osc::ReceivedMessage& msg,
                                const ip_address& addr);

    int32_t handle_ping_message(const osc::ReceivedMessage& msg,
                                const ip_address& addr);
};

} // aoo
