#pragma once

#include "aoo/aoo.hpp"
#include "aoo_imp.hpp"
#include "lfqueue.hpp"
#include "time_dll.hpp"

#include <mutex>

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
    std::atomic<int32_t> lost{0};
    std::atomic<int32_t> reordered{0};
    std::atomic<int32_t> resent{0};
    std::atomic<int32_t> gap{0};
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

} // aoo

class aoo_sink final : public aoo::isink {
 public:
    aoo_sink(int32_t id)
        : id_(id) {}

    void setup(aoo_sink_settings& settings) override;

    int32_t handle_message(const char *data, int32_t n,
                           void *endpoint, aoo_replyfn fn) override;

    int32_t process(uint64_t t) override;
    bool events_available() override;

    int32_t handle_events() override;
 private:
    const int32_t id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    int32_t buffersize_ = 0;
    int32_t resend_limit_ = 0;
    float resend_interval_ = 0;
    int32_t resend_maxnumframes_ = 0;
    int32_t resend_packetsize_ = 0;
    float ping_interval_ = 0;
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
    std::mutex mutex_; // LATER replace with a spinlock?
    aoo::time_dll dll_;
    double bandwidth_ = AOO_DLL_BW;
    double starttime_ = 0;
    aoo::threadsafe_counter elapsedtime_;
    // helper methods
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
