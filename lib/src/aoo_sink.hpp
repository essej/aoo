#pragma once

#include "aoo/aoo.hpp"
#include "aoo_imp.hpp"
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
        recover_ = false;
    }

    void add_lost(int32_t n) { lost_ += n; }
    void add_reordered(int32_t n) { reordered_ += n; }
    void add_resent(int32_t n) { resent_ += n; }
    void add_gap(int32_t n) { gap_ += n; }
    void get(int32_t& lost, int32_t& reordered,
             int32_t& resent, int32_t& gap)
    {
        lost = lost_.exchange(0);
        reordered = reordered_.exchange(0);
        resent = resent_.exchange(0);
        gap = gap_.exchange(0);
    }

    void set_recover() { recover_ = true; }
    bool get_recover() { return recover_.exchange(false); }
private:
    std::atomic<int32_t> lost_{0};
    std::atomic<int32_t> reordered_{0};
    std::atomic<int32_t> resent_{0};
    std::atomic<int32_t> gap_{0};
    std::atomic<bool> recover_{false};
};

struct block_info {
    double sr;
    int32_t channel;
};

class sink;

class source_desc {
public:
    source_desc(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt);
    source_desc(source_desc&& other) = default;
    source_desc& operator=(source_desc&& other) = default;

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
    int32_t handle_events(aoo_eventhandler handler, void *user);
    bool process(const sink& s, aoo_sample *buffer, int32_t size);
    void recover(){ streamstate_.set_recover(); }
private:
    struct data_request {
        int32_t sequence;
        int32_t frame;
    };
    bool check_packet(const data_packet& d);
    bool add_packet(const data_packet& d);
    void send_data();
    void pop_outdated_blocks();
    void check_missing_blocks(const sink& s);
    void request_data(const sink& s);
    void ping(const sink& s);
    void send(const char *data, int32_t n){
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
    double lastpingtime_ = 0;
    aoo_source_state laststate_{AOO_SOURCE_STATE_STOP};
    stream_state streamstate_;
    // queues and buffers
    block_queue blockqueue_;
    block_ack_list ack_list_;
    lockfree::queue<aoo_sample> audioqueue_;
    lockfree::queue<block_info> infoqueue_;
    lockfree::queue<aoo_event> eventqueue_;
    dynamic_resampler resampler_;
    std::vector<data_request> retransmit_list_;
};

class sink final : public isink {
public:
    sink(int32_t id)
        : id_(id) {}

    int32_t setup(const aoo_sink_settings& settings) override;

    int32_t handle_message(const char *data, int32_t n,
                           void *endpoint, aoo_replyfn fn) override;

    int32_t process(uint64_t t) override;

    int32_t events_available() override;

    int32_t handle_events() override;

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
    float ping_interval() const { return ping_interval_; }
    double elapsed_time() const { return timer_.get_elapsed(); }

    void request_format(void *endpoint, aoo_replyfn fn, int32_t id) const;
private:
    // settings
    const int32_t id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    aoo_processfn processfn_ = nullptr;
    aoo_eventhandler eventhandler_ = nullptr;
    void *user_ = nullptr;
    // buffer for summing source audio output
    std::vector<aoo_sample> buffer_;
    // options
    int32_t buffersize_ = AOO_SINK_BUFSIZE;
    int32_t packetsize_ = AOO_PACKETSIZE;
    int32_t resend_limit_ = AOO_RESEND_LIMIT;
    float resend_interval_ = AOO_RESEND_INTERVAL * 0.001;
    int32_t resend_maxnumframes_ = AOO_RESEND_MAXNUMFRAMES;
    float ping_interval_ = AOO_PING_INTERVAL * 0.001;
    // the sources
    lockfree::list<source_desc> sources_;
    // thread synchronization
    aoo::shared_mutex mutex_; // LATER replace with a spinlock?
    // timing
    time_dll dll_;
    double bandwidth_ = AOO_TIMEFILTER_BANDWIDTH;
    timer timer_;

    // helper methods
    source_desc *find_source(void *endpoint, int32_t id);

    void update_sources();

    int32_t handle_format_message(void *endpoint, aoo_replyfn fn, int32_t id,
                               int32_t salt, const aoo_format& format,
                               const char *settings, int32_t size);

    int32_t handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                               int32_t salt, const data_packet& data);

};

} // aoo
