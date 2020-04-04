/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo.h"

#include <vector>
#include <array>
#include <memory>
#include <atomic>

// for shared_lock
#include <shared_mutex>
#ifndef _WIN32
#include <pthread.h>
#endif

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

    void clear(){
        seconds = 0;
        nanos = 0;
    }

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

/*////////////////// simple spin lock ////////////////////*/

class spinlock {
public:
    spinlock() = default;
    spinlock(const spinlock&) = delete;
    spinlock& operator=(const spinlock&) = delete;
    void lock();
    bool try_lock();
    void unlock();
protected:
    std::atomic<uint32_t> locked_{false};
};

/*/////////////////// shared spin lock /////////////////////////*/

class shared_spinlock {
public:
    shared_spinlock() = default;
    shared_spinlock(const shared_spinlock&) = delete;
    shared_spinlock& operator=(const shared_spinlock&) = delete;
    // exclusive
    void lock();
    bool try_lock();
    void unlock();
    // shared
    void lock_shared();
    bool try_lock_shared();
    void unlock_shared();
protected:
    const uint32_t UNLOCKED = 0;
    const uint32_t LOCKED = 0x80000000;
    std::atomic<uint32_t> state_{0};
};

// paddeded spin locks

template<typename T, size_t N>
class alignas(N) padded_class : public T {
    // pad and align to prevent false sharing
    char pad_[N - sizeof(T)];
};

static const size_t CACHELINE_SIZE = 64;

using padded_spinlock = padded_class<spinlock, CACHELINE_SIZE>;

using padded_shared_spinlock =  padded_class<shared_spinlock, CACHELINE_SIZE>;


/*//////////////////////// shared_mutex //////////////////////////*/

// The std::mutex implementation on Windows is bad on both MSVC and MinGW:
// the MSVC version apparantely has some additional overhead; winpthreads (MinGW) doesn't even use the obvious
// platform primitive (SRWLOCK), they rather roll their own mutex based on atomics and Events, which is bad for our use case.
//
// Older OSX versions (OSX 10.11 and below) don't have std:shared_mutex...
//
// Even on Linux, there's some overhead for things we don't need, so we use pthreads directly.

class shared_mutex {
public:
    shared_mutex();
    ~shared_mutex();
    shared_mutex(const shared_mutex&) = delete;
    shared_mutex& operator=(const shared_mutex&) = delete;
    // exclusive
    void lock();
    bool try_lock();
    void unlock();
    // shared
    void lock_shared();
    bool try_lock_shared();
    void unlock_shared();
private:
#ifdef _WIN32
    void* rwlock_; // avoid including windows headers (SWRLOCK is pointer sized)
#else
    pthread_rwlock_t rwlock_;
#endif
};

using shared_lock = std::shared_lock<shared_mutex>;
using unique_lock = std::unique_lock<shared_mutex>;

template<typename T>
class scoped_lock {
public:
    scoped_lock(T& lock)
        : lock_(&lock){ lock_->lock(); }
    scoped_lock(const T& lock) = delete;
    scoped_lock& operator=(const T& lock) = delete;
    ~scoped_lock() { lock_->unlock(); }
private:
    T* lock_;
};

template<typename T>
class shared_scoped_lock {
public:
    shared_scoped_lock(T& lock)
        : lock_(&lock){ lock_->lock_shared(); }
    shared_scoped_lock(const T& lock) = delete;
    shared_scoped_lock& operator=(const T& lock) = delete;
    ~shared_scoped_lock() { lock_->unlock_shared(); }
private:
    T* lock_;
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
          int32_t nbytes, int32_t nframes);
    void set(int32_t seq, double sr, int32_t chn,
             const char *data, int32_t nbytes,
             int32_t nframes, int32_t framesize);
    const char* data() const { return buffer_.data(); }
    int32_t size() const { return buffer_.size(); }
    bool complete() const;
    void add_frame(int32_t which, const char *data, int32_t n);
    int32_t get_frame(int32_t which, char * data, int32_t n);
    bool has_frame(int32_t which) const;
    int32_t frame_size(int32_t which) const;
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
    static const int32_t EMPTY = -1;
    static const int32_t DELETED = -2;

    block_ack();
    block_ack(int32_t seq, int32_t limit);

    bool check(double time, double interval);
    int32_t sequence;
private:
    int32_t count_;
    double timestamp_;
};

#define BLOCK_ACK_LIST_HASHTABLE 1
#define BLOCK_ACK_LIST_SORTED 1

class block_ack_list {
public:
    block_ack_list();

    void set_limit(int32_t limit);
    block_ack* find(int32_t seq);
    block_ack& get(int32_t seq);
    bool remove(int32_t seq);
    int32_t remove_before(int32_t seq);
    void clear();
    bool empty() const;
    int32_t size() const;

    friend std::ostream& operator<<(std::ostream& os, const block_ack_list& b);
private:
#if !BLOCK_ACK_LIST_HASHTABLE
#if BLOCK_ACK_LIST_SORTED
    std::vector<block_ack>::iterator lower_bound(int32_t seq);
#endif
#else
    void rehash();

    static const int32_t initial_size_ = 16;

    int32_t size_;
    int32_t deleted_;
    int32_t oldest_;
#endif
    int32_t limit_ = 0;
    std::vector<block_ack> data_;
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
    std::atomic<time_tag> last_;
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
