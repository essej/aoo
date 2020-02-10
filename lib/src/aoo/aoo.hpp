#pragma once

#include "aoo.h"
#include "lfqueue.hpp"

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

    void add_sink(void *sink, int32_t id, aoo_replyfn fn);

    void remove_sink(void *sink, int32_t id);

    void remove_all();

    void set_sink_channel(void *sink, int32_t id, int32_t chn);

    void handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn);

    bool send();

    bool process(const aoo_sample **data, int32_t n);
 private:
    const int32_t id_;
    int32_t salt_ = 0;
    std::unique_ptr<aoo_format> format_;
    int32_t bytespersample_ = 0;
    int32_t buffersize_ = 0;
    int32_t packetsize_ = AOO_DEFPACKETSIZE;
    int32_t sequence_ = 0;
    lfqueue<aoo_sample> lfqueue_;
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
        fraction = ui & 0xFFFF;
    }
    uint32_t seconds = 0;
    uint32_t fraction = 0;
};

struct block {
    block(){}
    block(int32_t seq, time_tag tt, int32_t chn,
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
    time_tag timetag;
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

struct source_desc {
    source_desc(void *endpoint, aoo_replyfn fn, int32_t id, int32_t salt);
    source_desc(source_desc&& other);
    source_desc& operator=(source_desc&& other);
    // data
    void *endpoint;
    aoo_replyfn fn;
    int32_t id;
    int32_t salt;
    aoo_format format;
    std::atomic<int32_t> channel; // can be set dynamically!
    int32_t newest; // sequence number of most recent block
    lfqueue<aoo_sample> audioqueue;
    block_queue blockqueue;
    // methods
    void send(const char *data, int32_t n){
        fn(endpoint, data, n);
    }
};

} // aoo

class aoo_sink {
 public:
    aoo_sink(int32_t id)
        : id_(id) {}

    void setup(int32_t nchannels, int32_t sr, int32_t blocksize,
               aoo_processfn fn, void *user);

    void set_buffersize(int32_t ms);

    int32_t handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn);

    int32_t process();
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
    // helper methods
    void update_source(aoo::source_desc& src);

    void request_format(void * endpoint, aoo_replyfn fn, int32_t id);

    void handle_format_message(void *endpoint, aoo_replyfn fn,
                               int32_t id, int32_t salt, const aoo_format& format);

    void handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                             int32_t salt, int32_t seq, aoo::time_tag tt, int32_t chn,
                             int32_t nframes, int32_t frame, const char *data, int32_t size);
};
