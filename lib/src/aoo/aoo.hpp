#pragma once

#include "aoo.h"
#include "lfqueue.hpp"
#include "time_dll.hpp"

#include <vector>
#include <memory>
#include <atomic>

class aoo_source {
 public:
    aoo_source(int32_t id);
    ~aoo_source();

    void set_format(aoo_format& f);

    void set_buffersize(int32_t ms);

    void set_packetsize(int32_t nbytes);

    void set_timefilter(double bandwidth);

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
    std::unique_ptr<aoo_format> format_;
    int32_t bytespersample_ = 0;
    int32_t buffersize_ = 0;
    int32_t packetsize_ = AOO_DEFPACKETSIZE;
    int32_t sequence_ = 0;
    aoo::lfqueue<aoo_sample> audioqueue_;
    aoo::lfqueue<uint64_t> ttqueue_;
    aoo::time_dll dll_;
    double bandwidth_ = AOO_DLL_BW;
    double starttime_ = 0;
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
    void send_format(sink_desc& sink);
};

namespace aoo {

struct time_tag {
    time_tag() = default;
    time_tag(uint64_t ui){
        seconds = ui >> 32;
        nanos = (uint32_t)ui;
    }
    time_tag(double s){
        seconds = (uint64_t)s;
        double fract = s - (double)seconds;
        nanos = fract * 4294967296.0;
    }

    union {
        struct {
            uint32_t seconds = 0;
            uint32_t nanos = 0;
        };
    };
    double to_double() const {
        return (double)seconds + (double)nanos / 4294967296.0;
    }
    uint64_t to_uint64() const {
        return (uint64_t)seconds << 32 | (uint64_t)nanos;
    }
    time_tag operator+(time_tag t){
        time_tag result;
        uint64_t ns = nanos + t.nanos;
        result.nanos = ns & 0xFFFFFFFF;
        result.seconds = seconds + t.seconds + (ns >> 32);
        return result;
    }
    time_tag operator-(time_tag t){
        time_tag result;
        uint64_t ns = ((uint64_t)1 << 32) + nanos - t.nanos;
        result.nanos = ns & 0xFFFFFFFF;
        result.seconds = seconds - t.seconds - !(ns >> 32);
        return result;
    }
};

struct block {
    block(){}
    block(int32_t seq, double sr, int32_t chn,
                 int32_t nbytes, int32_t nframes);
    block(const block&) = default;
    block(block&&) = default;
    block& operator=(const block&) = default;
    block& operator=(block&&) = default;
    // methods
    bool complete() const;
    void add_frame(int which, const char *data, int32_t n);
    // data
    int32_t sequence = -1;
    double samplerate = 0;
    int32_t channel = 0;
    const char* data() const { return buffer.data(); }
    int32_t size() const { return buffer.size(); }
private:
    std::vector<char> buffer;
    int32_t numframes = 0;
    uint32_t frames = 0; // bitfield (later expand)
};

class block_queue {
public:
    void clear();
    void resize(int32_t n);
    bool empty() const;
    bool full() const;
    int32_t size() const;
    int32_t capacity() const;
    block* insert(block&& b);
    block* find(int32_t seq);
    void pop_front();
    void pop_back();

    block& front();
    block& back();
    block *begin();
    block *end();
    block& operator[](int32_t i);
private:
    std::vector<block> blocks_;
    int32_t capacity_ = 0;
};

class dynamic_resampler {
public:
    void setup(int32_t nfrom, int32_t nto, int32_t nchannels);
    void clear();
    void update(double srfrom, double srto);
    int32_t write_available();
    void write(const aoo_sample* data, int32_t n);
    int32_t read_available();
    void read(aoo_sample* data, int32_t n);
private:
    std::vector<aoo_sample> buffer_;
    int32_t nchannels_ = 0;
    double rdpos_ = 0;
    int32_t wrpos_ = 0;
    double balance_ = 0;
    double ratio_ = 1.0;
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
    aoo_format format;
    int32_t newest = 0; // sequence number of most recent block
    block_queue blockqueue;
    lfqueue<aoo_sample> audioqueue;
    struct info {
        double sr;
        int32_t channel;
    };
    lfqueue<info> infoqueue;
    dynamic_resampler resampler;
    // methods
    void send(const char *data, int32_t n);
};

} // aoo

class aoo_sink {
 public:
    aoo_sink(int32_t id)
        : id_(id) {}

    void setup(int32_t nchannels, int32_t sr, int32_t blocksize,
               aoo_processfn fn, void *user);

    void set_timefilter(double bandwidth);

    void set_buffersize(int32_t ms);

    int32_t handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn);

    int32_t process(uint64_t t);
 private:
    const int32_t id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    int32_t buffersize_ = 0;
    std::vector<aoo_sample> buffer_;
    aoo_processfn processfn_ = nullptr;
    void *user_ = nullptr;
    std::vector<aoo::source_desc> sources_;
    aoo::time_dll dll_;
    double bandwidth_ = AOO_DLL_BW;
    double starttime_ = 0;
    // helper methods
    void update_source(aoo::source_desc& src);

    void request_format(void * endpoint, aoo_replyfn fn, int32_t id);

    void handle_format_message(void *endpoint, aoo_replyfn fn,
                               int32_t id, int32_t salt, const aoo_format& format);

    void handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                             int32_t salt, int32_t seq, double sr, int32_t chn,
                             int32_t nframes, int32_t frame, const char *data, int32_t size);
};
