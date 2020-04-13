/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo.hpp"
#include "aoo/aoo_utils.hpp"

#include "time.hpp"
#include "sync.hpp"
#include "common.hpp"
#include "lockfree.hpp"
#include "time_dll.hpp"

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
        recover_ = false;
        format_ = false;
        invite_ = NONE;
        pingtime1_ = time_tag{};
        pingtime2_ = time_tag{};
    }

    void add_lost(int32_t n) { lost_ += n; }
    int32_t get_lost() { return lost_.exchange(0); }

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
        pingtime1_ = t1;
        pingtime2_ = t2;
    }

    bool need_ping(time_tag& t1, time_tag& t2){
        // check pingtime2 because it ensures that pingtime1 has been set
        auto pingtime2 = pingtime2_.exchange(time_tag{});
        if (!pingtime2.empty()){
            t1 = pingtime1_.load();
            t2 = pingtime2;
            return true;
        } else {
            return false;
        }
    }

    void request_recover() { recover_ = true; }
    bool need_recover() { return recover_.exchange(false); }

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
    std::atomic<int32_t> lost_{0};
    std::atomic<int32_t> reordered_{0};
    std::atomic<int32_t> resent_{0};
    std::atomic<int32_t> gap_{0};
    std::atomic<aoo_source_state> state_{AOO_SOURCE_STATE_STOP};
    std::atomic<invitation_state> invite_{NONE};
    std::atomic<bool> recover_{false};
    std::atomic<bool> format_{false};
    std::atomic<time_tag> pingtime1_;
    std::atomic<time_tag> pingtime2_;
};

struct block_info {
    double sr;
    int32_t channel;
};

class sink;

class source_desc {
public:
    typedef union event
    {
        aoo_event_type type;
        aoo_source_event source;
        aoo_ping_event ping;
        aoo_source_state_event source_state;
        aoo_block_lost_event block_loss;
        aoo_block_reordered_event block_reorder;
        aoo_block_resent_event block_resend;
        aoo_block_gap_event block_gap;
    } event;

    source_desc(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt);
    source_desc(const source_desc& other) = delete;
    source_desc& operator=(const source_desc& other) = delete;

    // getters
    int32_t id() const { return id_; }
    void *endpoint() const { return endpoint_; }
    bool has_events() const { return  eventqueue_.read_available() > 0; }
    int32_t get_format(aoo_format_storage& format);

    // methods
    void update(const sink& s);
    int32_t handle_format(const sink& s, int32_t salt, const aoo_format& f,
                               const char *setting, int32_t size);
    int32_t handle_data(const sink& s, int32_t salt, const data_packet& d);
    int32_t handle_ping(const sink& s, time_tag tt);
    int32_t handle_events(aoo_eventhandler fn, void *user);
    bool send(const sink& s);
    bool process(const sink& s, aoo_sample *buffer, int32_t size);
    void request_recover(){ streamstate_.request_recover(); }
    void request_format(){ streamstate_.request_format(); }
    void request_invite(){ streamstate_.request_invitation(stream_state::INVITE); }
    void request_uninvite(){ streamstate_.request_invitation(stream_state::UNINVITE); }
private:
    struct data_request {
        int32_t sequence;
        int32_t frame;
    };
    void do_update(const sink& s);
    // handle messages
    bool check_packet(const data_packet& d);
    bool add_packet(const data_packet& d);
    void process_blocks();
    void check_outdated_blocks();
    void check_missing_blocks(const sink& s);
    // send messages
    bool send_format_request(const sink& s);
    int32_t send_data_request(const sink& s);
    bool send_notifications(const sink& s);
    void dosend(const char *data, int32_t n){
        fn_(endpoint_, data, n);
    }
    // data
    void * const endpoint_;
    const aoo_replyfn fn_;
    const int32_t id_;
    int32_t salt_;
    // audio decoder
    std::unique_ptr<aoo::decoder> decoder_;
    // state
    int32_t newest_ = 0; // sequence number of most recent incoming block
    int32_t next_ = 0; // next outgoing block
    int32_t channel_ = 0; // recent channel onset
    double samplerate_ = 0; // recent samplerate
    stream_state streamstate_;
    // queues and buffers
    block_queue blockqueue_;
    block_ack_list ack_list_;
    lockfree::queue<aoo_sample> audioqueue_;
    lockfree::queue<block_info> infoqueue_;
    lockfree::queue<data_request> resendqueue_;
    lockfree::queue<event> eventqueue_;
    spinlock eventqueuelock_;
    void push_event(const event& e){
        scoped_lock<spinlock> l(eventqueuelock_);
        if (eventqueue_.write_available()){
            eventqueue_.write(e);
        }
    }
    dynamic_resampler resampler_;
    // thread synchronization
    aoo::shared_mutex mutex_; // LATER replace with a spinlock?
};

class sink final : public isink {
public:
    sink(int32_t id)
        : id_(id) {}

    ~sink(){}

    int32_t setup(int32_t samplerate, int32_t blocksize, int32_t nchannels) override;

    int32_t invite_source(void *endpoint, int32_t id, aoo_replyfn fn) override;

    int32_t uninvite_source(void *endpoint, int32_t id, aoo_replyfn fn) override;

    int32_t uninvite_all() override;

    int32_t handle_message(const char *data, int32_t n,
                           void *endpoint, aoo_replyfn fn) override;

    int32_t send() override;

    int32_t process(aoo_sample **data, int32_t nsamples, uint64_t t) override;

    int32_t events_available() override;

    int32_t handle_events(aoo_eventhandler fn, void *user) override;

    int32_t set_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t get_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t set_sourceoption(void *endpoint, int32_t id,
                             int32_t opt, void *ptr, int32_t size) override;

    int32_t get_sourceoption(void *endpoint, int32_t id,
                             int32_t opt, void *ptr, int32_t size) override;
    // getters
    int32_t id() const { return id_; }
    int32_t nchannels() const { return nchannels_; }
    int32_t samplerate() const { return samplerate_; }
    double real_samplerate() const { return dll_.samplerate(); }
    int32_t blocksize() const { return blocksize_; }
    int32_t buffersize() const { return buffersize_; }
    int32_t packetsize() const { return packetsize_; }
    float resend_interval() const { return resend_interval_; }
    int32_t resend_limit() const { return resend_limit_; }
    int32_t resend_maxnumframes() const { return resend_maxnumframes_; }
    double elapsed_time() const { return timer_.get_elapsed(); }
    time_tag absolute_time() const { return timer_.get_absolute(); }
private:
    // settings
    const int32_t id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    // buffer for summing source audio output
    std::vector<aoo_sample> buffer_;
    // options
    std::atomic<int32_t> buffersize_{ AOO_SINK_BUFSIZE };
    std::atomic<int32_t> packetsize_{ AOO_PACKETSIZE };
    std::atomic<int32_t> resend_limit_{ AOO_RESEND_LIMIT };
    std::atomic<float> resend_interval_{ AOO_RESEND_INTERVAL * 0.001 };
    std::atomic<int32_t> resend_maxnumframes_{ AOO_RESEND_MAXNUMFRAMES };
    // the sources
    lockfree::list<source_desc> sources_;
    // timing
    std::atomic<float> bandwidth_{ AOO_TIMEFILTER_BANDWIDTH };
    time_dll dll_;
    timer timer_;

    // helper methods
    source_desc *find_source(void *endpoint, int32_t id);

    void update_sources();

    int32_t handle_format_message(void *endpoint, aoo_replyfn fn, int32_t id,
                               int32_t salt, const aoo_format& format,
                               const char *settings, int32_t size);

    int32_t handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                               int32_t salt, const data_packet& data);

    int32_t handle_ping_message(void *endpoint, aoo_replyfn fn, int32_t id,
                               time_tag tt);
};

} // aoo
