#pragma once

#include "aoo/aoo.hpp"
#include "aoo_imp.hpp"
#include "lockfree.hpp"
#include "time_dll.hpp"

// forward declaration
namespace osc {
    class ReceivedMessageArgumentIterator;
}

namespace aoo {

struct sink_desc {
    sink_desc(void *_endpoint, aoo_replyfn _fn, int32_t _id)
        : endpoint(_endpoint), fn(_fn), id(_id),
          channel(0), format_changed(true) {}
    sink_desc(const sink_desc& other)
        : endpoint(other.endpoint), fn(other.fn), id(other.id),
          channel(other.channel.load()),
          format_changed(other.format_changed) {}
    sink_desc& operator=(const sink_desc& other){
        endpoint = other.endpoint;
        fn = other.fn;
        id = other.id;
        channel = other.channel.load();
        format_changed = other.format_changed;
        return *this;
    }
    // data
    void * endpoint;
    aoo_replyfn fn;
    int32_t id;
    std::atomic<int32_t> channel;
    bool format_changed;

    // methods
    void send_data(int32_t src, int32_t salt, const data_packet& data) const;
    void send_data(int32_t sink, int32_t src, int32_t salt, const data_packet& data) const;

    void send_format(int32_t src, int32_t salt, const aoo_format& f,
                     const char *options, int32_t size) const;
    void send_format(int32_t sink, int32_t src, int32_t salt, const aoo_format& f,
                     const char *options, int32_t size) const;

    void send(const char *data, int32_t n) const {
        fn(endpoint, data, n);
    }
};

class source final : public isource {
 public:
    source(int32_t id);
    ~source();

    int32_t setup(int32_t samplerate, int32_t blocksize, int32_t nchannels) override;

    int32_t add_sink(void *sink, int32_t id, aoo_replyfn fn) override;

    int32_t remove_sink(void *sink, int32_t id) override;

    void remove_all() override;

    int32_t handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn) override;

    int32_t send() override;

    int32_t process(const aoo_sample **data, int32_t n, uint64_t t) override;

    int32_t events_available() override;

    int32_t handle_events(aoo_eventhandler fn, void *user) override;

    int32_t set_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t get_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t set_sinkoption(void *endpoint, int32_t id,
                           int32_t opt, void *ptr, int32_t size) override;

    int32_t get_sinkoption(void *endpoint, int32_t id,
                           int32_t opt, void *ptr, int32_t size) override;
 private:
    // settings
    const int32_t id_;
    int32_t salt_ = 0;
    int32_t nchannels_ = 0;
    int32_t blocksize_ = 0;
    int32_t samplerate_ = 0;
    // audio encoder
    std::unique_ptr<encoder> encoder_;
    // state
    int32_t sequence_ = 0;
    std::atomic<int32_t> dropped_{0};
    // timing
    time_dll dll_;
    std::atomic<double> bandwidth_{ AOO_TIMEFILTER_BANDWIDTH };
    timer timer_;
    // buffers and queues
    dynamic_resampler resampler_;
    lockfree::queue<aoo_sample> audioqueue_;
    lockfree::queue<double> srqueue_;
    lockfree::queue<aoo_event> eventqueue_;
    history_buffer history_;
    std::vector<char> blockbuffer_;
    std::vector<char> resendbuffer_;
    // sinks
    std::vector<sink_desc> sinks_;
    // thread synchronization
    aoo::shared_mutex update_mutex_;
    aoo::shared_mutex sinklist_mutex_;
    // options
    std::atomic<int32_t> buffersize_{ AOO_SOURCE_BUFSIZE };
    std::atomic<int32_t> packetsize_{ AOO_PACKETSIZE };
    std::atomic<int32_t> resend_buffersize_{ AOO_RESEND_BUFSIZE };

    // helper methods
    sink_desc * find_sink(void *endpoint, int32_t id);

    int32_t set_format(aoo_format& f);

    int32_t make_salt();

    void update();

    void update_historybuffer();

    void handle_request(void *endpoint, aoo_replyfn fn, int32_t id);

    void handle_resend(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt,
                        int32_t count, osc::ReceivedMessageArgumentIterator it);

    void handle_ping(void *endpoint, aoo_replyfn fn, int32_t id);

};

} // aoo
