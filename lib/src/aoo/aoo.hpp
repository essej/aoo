#pragma once

#include "aoo.h"
#include "lfqueue.hpp"
#include "time_dll.hpp"

#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>

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

    uint32_t seconds = 0;
    uint32_t nanos = 0;

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

class dynamic_resampler {
public:
    void setup(int32_t nfrom, int32_t nto, int32_t srfrom, int32_t srto, int32_t nchannels);
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

class base_codec {
public:
    int32_t nchannels() const { return nchannels_; }
    int32_t samplerate() const { return samplerate_; }
    int32_t blocksize() const { return blocksize_; }
protected:
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
};

class encoder : public base_codec {
public:
    encoder(const aoo_codec *codec, void *obj)
        : codec_(codec), obj_(obj){}
    encoder(const aoo_codec&) = delete;
    ~encoder(){
        codec_->encoder_free(obj_);
    }
    const char *name() const {
        return codec_->name;
    }
    void setup(aoo_format& fmt){
        codec_->encoder_setup(obj_, &fmt);
        // assign after validation!
        nchannels_ = fmt.nchannels;
        samplerate_ = fmt.samplerate;
        blocksize_ = fmt.blocksize;
    }
    int32_t encode(const aoo_sample *s, int32_t n, char *buf, int32_t size){
        return codec_->encoder_encode(obj_, s, n, buf, size);
    }
    int32_t write(int32_t& nchannels, int32_t& samplerate, int32_t& blocksize,
                  char *buf, int32_t size){
        return codec_->encoder_write(obj_,&nchannels, &samplerate,
                                     &blocksize,buf, size);
    }
private:
    const aoo_codec *codec_;
    void *obj_;
};

class decoder : public base_codec {
public:
    decoder(const aoo_codec *codec, void *obj)
        : codec_(codec), obj_(obj){}
    decoder(const aoo_codec&) = delete;
    ~decoder(){
        codec_->decoder_free(obj_);
    }
    const char *name() const {
        return codec_->name;
    }
    int32_t decode(const char *buf, int32_t size, aoo_sample *s, int32_t n){
        return codec_->decoder_decode(obj_, buf, size, s, n);
    }
    int32_t read(int32_t nchannels, int32_t samplerate, int32_t blocksize,
                 const char *opt, int32_t size){
        auto result = codec_->decoder_read(obj_, nchannels, samplerate,
                                    blocksize, opt, size);
        if (result >= 0){
            nchannels_ = nchannels;
            samplerate_ = samplerate;
            blocksize_ = blocksize;
        }
        return result;
    }
private:
    const aoo_codec *codec_;
    void *obj_;
};

class codec {
public:
    codec(const aoo_codec *c)
        : codec_(c){}
    const char *name() const {
        return codec_->name;
    }
    std::unique_ptr<encoder> create_encoder() const {
        auto obj = codec_->encoder_new();
        if (obj){
            return std::make_unique<encoder>(codec_, obj);
        } else {
            return nullptr;
        }
    }
    std::unique_ptr<decoder> create_decoder() const {
        auto obj = codec_->decoder_new();
        if (obj){
            return std::make_unique<decoder>(codec_, obj);
        } else {
            return nullptr;
        }
    }
private:
    const aoo_codec *codec_;
};

struct data_packet {
    int32_t sequence;
    double samplerate;
    int32_t channel;
    int32_t totalsize;
    int32_t nframes;
    int32_t framenum;
    const char *data;
    int32_t size;
};

class block {
public:
    // methods
    void set(int32_t seq, double sr, int32_t chn,
          int32_t nbytes, int32_t nframes);
    void set(int32_t seq, double sr, int32_t chn,
             const char *data, int32_t nbytes,
             int32_t nframes, int32_t framesize);
    const char* data() const { return buffer_.data(); }
    int32_t size() const { return buffer_.size(); }
    bool complete() const;
    void add_frame(int32_t which, const char *data, int32_t n);
    void get_frame(int32_t which, const char *& data, int32_t& n);
    bool has_frame(int32_t which) const;
    int32_t num_frames() const { return numframes_; }
    // data
    int32_t sequence = -1;
    double samplerate = 0;
    int32_t channel = 0;
protected:
    std::vector<char> buffer_;
    uint64_t frames_ = 0; // bitfield (later expand)
    int32_t numframes_ = 0;
    int32_t framesize_ = 0;
};

class block_queue {
public:
    void clear();
    void resize(int32_t n);
    bool empty() const;
    bool full() const;
    int32_t size() const;
    int32_t capacity() const;
    block* insert(int32_t seq, double sr, int32_t chn,
                  int32_t nbytes, int32_t nframes);
    block* find(int32_t seq);
    void pop_front();
    void pop_back();

    block& front();
    block& back();
    block *begin();
    block *end();
    block& operator[](int32_t i);

    friend std::ostream& operator<<(std::ostream& os, const block_queue& b);
private:
    std::vector<block> blocks_;
    int32_t size_ = 0;
};

class block_ack {
public:
    block_ack(int32_t seq, int32_t limit);

    bool check(double time, double interval);
    int32_t sequence;
private:
    int32_t retransmit_count_;
    double retransmit_timestamp_;
};

class block_ack_list {
public:
    void setup(int32_t limit);
    block_ack* find(int32_t seq);
    block_ack& get(int32_t seq);
    void remove(int32_t seq);
    void remove_before(int32_t seq);
    void clear();
    bool empty() const;
    int32_t size() const;

    friend std::ostream& operator<<(std::ostream& os, const block_ack_list& b);
private:
    std::unordered_map<int32_t, block_ack> data_;
    int32_t limit_ = 0;
};

class history_buffer {
public:
    void clear();
    int32_t capacity() const;
    void resize(int32_t n);
    block * find(int32_t seq);
    void push(int32_t seq, double sr,
             const char *data, int32_t nbytes,
             int32_t nframes, int32_t framesize);
private:
    std::vector<block> buffer_;
    int32_t oldest_ = 0;
    int32_t head_ = 0;
};

} // aoo

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

namespace aoo {

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
    block_queue blockqueue;
    block_ack_list ack_list;
    lfqueue<aoo_sample> audioqueue;
    struct info {
        double sr;
        int32_t channel;
        aoo_source_state state;
    };
    lfqueue<info> infoqueue;
    aoo_source_state laststate;
    dynamic_resampler resampler;
    // methods
    void send(const char *data, int32_t n);
};

class threadsafe_counter {
public:
    threadsafe_counter()
        : time_(0){}
    threadsafe_counter(const threadsafe_counter& other)
        : time_(other.time_.load()){}
    threadsafe_counter& operator=(const threadsafe_counter& other){
        time_ = other.time_.load();
        return *this;
    }
    void reset() { time_ = 0; }
    double get() const { return time_; }
    void set(double t) { time_ = t; }
    void advance(double t){ time_ = time_.load() + t; }
private:
    std::atomic<double> time_;
};

} // aoo

class aoo_sink {
 public:
    aoo_sink(int32_t id)
        : id_(id) {}

    void setup(aoo_sink_settings& settings);

    int32_t handle_message(const char *data, int32_t n, void *endpoint, aoo_replyfn fn);

    int32_t process(uint64_t t);
 private:
    const int32_t id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
    int32_t buffersize_ = 0;
    int32_t resend_limit_ = 0;
    int32_t resend_interval_ = 0;
    int32_t resend_maxnumframes_ = 0;
    int32_t resend_packetsize_ = 0;
    std::vector<aoo_sample> buffer_;
    aoo_processfn processfn_ = nullptr;
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

    void handle_format_message(void *endpoint, aoo_replyfn fn,
                               int32_t id, int32_t salt, const aoo_format& f,
                               const char *setting, int32_t size);

    void handle_data_message(void *endpoint, aoo_replyfn fn, int32_t id,
                             int32_t salt, const aoo::data_packet& d);
};
