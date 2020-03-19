#pragma once

#include "aoo/aoo.hpp"
#include "aoo_imp.hpp"
#include "lfqueue.hpp"
#include "time_dll.hpp"

// forward declaration
namespace osc {
    class ReceivedMessageArgumentIterator;
}

namespace aoo {

class source final : public isource {
 public:
    source(int32_t id);
    ~source();

    int32_t setup(const aoo_source_settings& settings) override;

    int32_t add_sink(void *sink, int32_t id, aoo_replyfn fn) override;

    int32_t remove_sink(void *sink, int32_t id) override;

    void remove_all() override;

    int32_t handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn) override;

    int32_t send() override;

    int32_t process(const aoo_sample **data, int32_t n, uint64_t t) override;

    int32_t events_available() override;

    int32_t handle_events() override;

    int32_t set_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t get_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t set_sinkoption(void *endpoint, int32_t id,
                           int32_t opt, void *ptr, int32_t size) override;

    int32_t get_sinkoption(void *endpoint, int32_t id,
                           int32_t opt, void *ptr, int32_t size) override;
 private:
    const int32_t id_;
    int32_t salt_ = 0;
    std::unique_ptr<aoo::encoder> encoder_;
    int32_t nchannels_ = 0;
    int32_t blocksize_ = 0;
    int32_t samplerate_ = 0;
    int32_t buffersize_ = AOO_SOURCE_BUFSIZE;
    int32_t packetsize_ = AOO_PACKETSIZE;
    int32_t resend_buffersize_ = AOO_RESEND_BUFSIZE;
    int32_t sequence_ = 0;
    aoo::dynamic_resampler resampler_;
    aoo::lfqueue<aoo_sample> audioqueue_;
    aoo::lfqueue<double> srqueue_;
    aoo::lfqueue<aoo_event> eventqueue_;
    aoo_eventhandler eventhandler_;
    void *user_;
    aoo::time_dll dll_;
    double bandwidth_ = AOO_TIMEFILTER_BANDWIDTH;
    double starttime_ = 0;
    aoo::history_buffer history_;
    // sinks
    struct sink_desc {
        // data
        void *endpoint;
        aoo_replyfn fn;
        int32_t id;
        int32_t channel;
        // methods
        void send(const char *data, int32_t n){
            fn(endpoint, data, n);
        }
    };
    std::vector<sink_desc> sinks_;
    // helper methods
    sink_desc * find_sink(void *endpoint, int32_t id);

    int32_t set_format(aoo_format& f);

    void update();

    void update_historybuffer();

    void send_data(sink_desc& sink, const aoo::data_packet& d);

    void send_format(sink_desc& sink);

    void handle_request(void *endpoint, aoo_replyfn fn, int32_t id);

    void handle_resend(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt,
                        int32_t count, osc::ReceivedMessageArgumentIterator it);

    void handle_ping(void *endpoint, aoo_replyfn fn, int32_t id);

    int32_t make_salt();
};

} // aoo
