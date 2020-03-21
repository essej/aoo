#pragma once

#include "aoo/aoo.hpp"
#include "aoo_imp.hpp"
#include "lfqueue.hpp"
#include "time_dll.hpp"

namespace aoo {

struct stream_state {
    stream_state() = default;

    stream_state(stream_state&& other)
        : lost(other.lost.load()),
          reordered(other.reordered.load()),
          resent(other.resent.load()),
          gap(other.gap.load()){}

    stream_state& operator=(stream_state&& other){
        lost = other.lost.load();
        reordered = other.reordered.load();
        resent = other.resent.load();
        gap = other.gap.load();
        return *this;
    }

    void reset(){
        lost = 0;
        reordered = 0;
        resent = 0;
        gap = 0;
    }

    std::atomic<int32_t> lost{0};
    std::atomic<int32_t> reordered{0};
    std::atomic<int32_t> resent{0};
    std::atomic<int32_t> gap{0};
    std::atomic<bool> recover{false};
};

struct source_desc {
    source_desc(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt);
    source_desc(source_desc&& other) = default;
    source_desc& operator=(source_desc&& other) = default;
    // data
    void *endpoint;
    aoo_replyfn fn;
    int32_t id;
    int32_t salt;
    std::unique_ptr<aoo::decoder> decoder;
    int32_t newest = 0; // sequence number of most recent incoming block
    int32_t next = 0; // next outgoing block
    int32_t channel = 0; // recent channel onset
    double samplerate = 0; // recent samplerate
    double lastpingtime = 0;
    aoo_source_state laststate;
    stream_state streamstate;
    block_queue blockqueue;
    block_ack_list ack_list;
    lfqueue<aoo_sample> audioqueue;
    struct info {
        double sr;
        int32_t channel;
    };
    lfqueue<info> infoqueue;
    lfqueue<aoo_event> eventqueue;
    dynamic_resampler resampler;
    // methods
    void send(const char *data, int32_t n);
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
 private:
    const int32_t id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    int32_t buffersize_ = AOO_SINK_BUFSIZE;
    int32_t packetsize_ = AOO_PACKETSIZE;
    int32_t resend_limit_ = AOO_RESEND_LIMIT;
    float resend_interval_ = AOO_RESEND_INTERVAL * 0.001;
    int32_t resend_maxnumframes_ = AOO_RESEND_MAXNUMFRAMES;
    float ping_interval_ = AOO_PING_INTERVAL * 0.001;
    std::vector<aoo_sample> buffer_;
    aoo_processfn processfn_ = nullptr;
    aoo_eventhandler eventhandler_ = nullptr;
    void *user_ = nullptr;
    std::vector<aoo::source_desc> sources_;
    struct data_request {
        int32_t sequence;
        int32_t frame;
    };
    std::vector<data_request> retransmit_list_;
    aoo::shared_mutex mutex_; // LATER replace with a spinlock?
    aoo::time_dll dll_;
    double bandwidth_ = AOO_TIMEFILTER_BANDWIDTH;
    aoo::timer timer_;
    // helper methods
    aoo::source_desc *find_source(void *endpoint, int32_t id);

    void update_sources();

    void update_source(aoo::source_desc& src);

    void request_format(void * endpoint, aoo_replyfn fn, int32_t id);

    void request_data(aoo::source_desc& src);

    void ping(aoo::source_desc& src);

    void handle_format_message(void *endpoint, aoo_replyfn fn,
                               int32_t id, int32_t salt, const aoo_format& f,
                               const char *setting, int32_t size);

    void handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                             int32_t salt, const aoo::data_packet& d);
};

} // aoo
