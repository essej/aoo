#pragma once

#include "aoo/aoo.hpp"
#include "aoo_imp.hpp"
#include "lfqueue.hpp"
#include "time_dll.hpp"

class aoo_source {
 public:
    aoo_source(int32_t id);
    ~aoo_source();

    void set_format(aoo_format& f);

    void setup(aoo_source_settings& settings);

    void add_sink(void *sink, int32_t id, aoo_replyfn fn);

    void remove_sink(void *sink, int32_t id);

    void remove_all();

    void set_sink_channel(void *sink, int32_t id, int32_t chn);

    void handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn);

    bool send();

    bool process(const aoo_sample **data, int32_t n, uint64_t t);
 private:
    const int32_t id_;
    int32_t salt_ = 0;
    std::unique_ptr<aoo::encoder> encoder_;
    int32_t nchannels_ = 0;
    int32_t blocksize_ = 0;
    int32_t samplerate_ = 0;
    int32_t buffersize_ = 0;
    int32_t packetsize_ = AOO_DEFPACKETSIZE;
    int32_t resend_buffersize_ = 0;
    int32_t sequence_ = 0;
    aoo::dynamic_resampler resampler_;
    aoo::lfqueue<aoo_sample> audioqueue_;
    aoo::lfqueue<double> srqueue_;
    aoo::time_dll dll_;
    double bandwidth_ = AOO_DLL_BW;
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
    void update();
    void send_data(sink_desc& sink, const aoo::data_packet& d);
    void send_format(sink_desc& sink);
    int32_t make_salt();
};
