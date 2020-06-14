/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo.h"

#include "time.hpp"
#include "sync.hpp"

#include <vector>
#include <array>
#include <bitset>
#include <memory>
#include <atomic>


namespace aoo {

bool check_version(uint32_t version);

uint32_t make_version();

class dynamic_resampler {
public:
    void setup(int32_t nfrom, int32_t nto, int32_t srfrom, int32_t srto, int32_t nchannels);
    void clear();
    void update(double srfrom, double srto);
    int32_t write_available();
    void write(const aoo_sample* data, int32_t n);
    int32_t read_available();
    void read(aoo_sample* data, int32_t n);

    double ratio() const { return ideal_ratio_; }
private:
    std::vector<aoo_sample> buffer_;
    int32_t nchannels_ = 0;
    double rdpos_ = 0;
    int32_t wrpos_ = 0;
    double balance_ = 0;
    double ratio_ = 1.0;
    double ideal_ratio_ = 1.0;
};

class base_codec {
public:
    base_codec(const aoo_codec *codec, void *obj)
        : codec_(codec), obj_(obj){}
    base_codec(const aoo_codec&) = delete;

    const char *name() const { return codec_->name; }
    int32_t nchannels() const { return nchannels_; }
    int32_t samplerate() const { return samplerate_; }
    int32_t blocksize() const { return blocksize_; }
protected:
    const aoo_codec *codec_;
    void *obj_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
};

class encoder : public base_codec {
public:
    using base_codec::base_codec;
    ~encoder(){
        codec_->encoder_free(obj_);
    }

    bool set_format(aoo_format& fmt);
    bool get_format(aoo_format_storage& fmt) const {
        return codec_->encoder_getformat(obj_, &fmt) > 0;
    }
    int32_t write_format(aoo_format& fmt, char *buf, int32_t size){
        return codec_->encoder_writeformat(obj_, &fmt, buf, size);
    }
    int32_t encode(const aoo_sample *s, int32_t n, char *buf, int32_t size){
        return codec_->encoder_encode(obj_, s, n, buf, size);
    }
};

class decoder : public base_codec {
public:
    using base_codec::base_codec;
    ~decoder(){
        codec_->decoder_free(obj_);
    }

    bool set_format(aoo_format& fmt);
    bool get_format(aoo_format_storage& f) const {
        return codec_->decoder_getformat(obj_, &f) > 0;
    }
    int32_t read_format(const aoo_format& fmt, const char *opt, int32_t size);
    int32_t decode(const char *buf, int32_t size, aoo_sample *s, int32_t n){
        return codec_->decoder_decode(obj_, buf, size, s, n);
    }
};

class codec {
public:
    codec(const aoo_codec *c)
        : codec_(c){}
    const char *name() const {
        return codec_->name;
    }
    std::unique_ptr<encoder> create_encoder() const;
    std::unique_ptr<decoder> create_decoder() const;
private:
    const aoo_codec *codec_;
};

const codec * find_codec(const std::string& name);

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
             const char *data, int32_t nbytes,
             int32_t nframes, int32_t framesize);
    const char* data() const { return buffer_.data(); }
    int32_t size() const { return buffer_.size(); }
    int32_t get_frame(int32_t which, char * data, int32_t n);
    int32_t frame_size(int32_t which) const;
    int32_t num_frames() const { return numframes_; }
    // data
    int32_t sequence = -1;
    double samplerate = 0;
    int32_t channel = 0;
protected:
    std::vector<char> buffer_;
    int32_t numframes_ = 0;
    int32_t framesize_ = 0;
};

class received_block : public block {
public:
    void init(int32_t seq, bool dropped);
    void init(int32_t seq, double sr, int32_t chn,
              int32_t nbytes, int32_t nframes);
    int32_t resend_count() const;
    bool dropped() const ;
    bool complete() const;
    int32_t count_frames() const;
    bool has_frame(int32_t which) const;
    void add_frame(int32_t which, const char *data, int32_t n);
    bool update(double time, double interval);
protected:
    std::bitset<256> frames_ = 0;
    double timestamp_ = 0;
    int32_t numtries_ = 0;
    bool dropped_ = false;
};

class jitter_buffer {
public:
    template<typename T, typename U>
    class base_iterator {
        T *data_;
        U *owner_;
    public:
        base_iterator(U* owner)
            : data_(nullptr), owner_(owner){}
        base_iterator(U* owner, T* data)
            : data_(data), owner_(owner){}
        base_iterator(const base_iterator&) = default;
        base_iterator& operator=(const base_iterator&) = default;
        T& operator*() { return *data_; }
        T* operator->() { return data_; }
        base_iterator& operator++() {
            auto next = data_ + 1;
            if (next == &(*owner_->data_.end())){
                next = &(*owner_->data_.begin());
            }
            if (next == &owner_->data_[owner_->head_]){
                next = nullptr; // sentinel
            }
            data_ = next;
            return *this;
        }
        base_iterator operator++(int) {
            base_iterator old = *this;
            operator++();
            return old;
        }
        bool operator==(const base_iterator& other){
            return data_ == other.data_;
        }
        bool operator!=(const base_iterator& other){
            return data_ != other.data_;
        }
    };

    using iterator = base_iterator<received_block, jitter_buffer>;
    using const_iterator = base_iterator<const received_block, const jitter_buffer>;

    void clear();
    void resize(int32_t n);
    bool empty() const;
    bool full() const;
    int32_t size() const;
    int32_t capacity() const;

    int32_t oldest() const { return oldest_; }
    int32_t newest() const { return newest_; }

    received_block* find(int32_t seq);
    received_block* push_back(int32_t seq);
    void pop_front();

    received_block& front();
    const received_block& front() const;
    received_block& back();
    const received_block& back() const;

    iterator begin();
    const_iterator begin() const;
    iterator end();
    const_iterator end() const;

    friend std::ostream& operator<<(std::ostream& os, const jitter_buffer& b);
private:
    std::vector<received_block> data_;
    int32_t size_ = 0;
    int32_t head_ = 0;
    int32_t tail_ = 0;
    int32_t oldest_ = -1;
    int32_t newest_ = -1;
};

class history_buffer {
public:
    void clear();
    int32_t capacity() const;
    void resize(int32_t n);
    block * find(int32_t seq);
    block * push();
private:
    std::vector<block> buffer_;
    int32_t oldest_ = 0;
    int32_t head_ = 0;
};

/*//////////////////////// timer //////////////////////*/

class timer {
public:
    enum class state {
        reset,
        ok,
        error
    };
    timer() = default;
    timer(const timer& other);
    timer& operator=(const timer& other);
    void setup(int32_t sr, int32_t blocksize);
    void reset();
    double get_elapsed() const;
    time_tag get_absolute() const;
    state update(time_tag t, double& error);
private:
    std::atomic<uint64_t> last_;
    std::atomic<double> elapsed_{0};

#if AOO_TIMEFILTER_CHECK
    // moving average filter to detect timing issues
    static const size_t buffersize_ = 64;

    double delta_ = 0;
    double sum_ = 0;
    std::array<double, buffersize_> buffer_;
    int32_t head_ = 0;
#endif

    spinlock lock_;
};

} // aoo
