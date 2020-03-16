#pragma once

#include <stdint.h>
#include <atomic>
#include <vector>
#include <cassert>

namespace aoo {

// a lock-free queue which supports different read and write sizes
template<typename T>
class lfqueue {
 public:
    lfqueue() = default;
    // we need a move constructor so we can
    // put it in STL containers
    lfqueue(lfqueue&& other)
        : balance_(other.balance_.load()),
          rdhead_(other.rdhead_),
          wrhead_(other.wrhead_),
          stride_(other.stride_),
          data_(std::move(other.data_))
    {}
    lfqueue& operator=(lfqueue&& other){
        balance_ = other.balance_.load();
        rdhead_ = other.rdhead_;
        wrhead_ = other.wrhead_;
        stride_ = other.stride_;
        data_ = std::move(other.data_);
        return *this;
    }

    void resize(int32_t size, int32_t blocksize) {
        // check if size is divisible by both rdsize and wrsize
        assert(size >= blocksize);
        assert((size % blocksize) == 0);
        data_.clear(); // force zero
        data_.resize(size);
        stride_ = blocksize;
        reset();
    }

    int32_t blocksize() const { return stride_; }

    int32_t capacity() const { return data_.size(); }

    void reset() {
        rdhead_ = wrhead_ = 0;
        balance_ = 0;
    }
    // returns: the number of available *blocks* for reading
    int32_t read_available() const {
        if (stride_){
            return balance_.load(std::memory_order_acquire) / stride_;
        } else {
            return 0;
        }
    }

    void read(T& out) {
        out = data_[rdhead_];
        rdhead_ = (rdhead_ + 1) % capacity();
        --balance_;
        assert(balance_ >= 0);
    }

    const T* read_data() const {
        return &data_[rdhead_];
    }

    void read_commit() {
        rdhead_ = (rdhead_ + stride_) % capacity();
        balance_ -= stride_;
        assert(balance_ >= 0);
    }
    // returns: the number of available *blocks* for writing
    int32_t write_available() const {
        if (stride_){
            return (capacity() - balance_.load(std::memory_order_acquire)) / stride_;
        } else {
            return 0;
        }
    }

    void write(const T& value) {
        data_[wrhead_] = value;
        wrhead_ = (wrhead_ + 1) % capacity();
        ++balance_;
        assert(balance_ <= capacity());
    }

    T* write_data() {
        return &data_[wrhead_];
    }

    void write_commit() {
        wrhead_ = (wrhead_ + stride_) % capacity();
        balance_ += stride_;
        assert(balance_ <= capacity());
    }
 private:
    std::atomic<int32_t> balance_{0};
    int32_t rdhead_{0};
    int32_t wrhead_{0};
    int32_t stride_{0};
    std::vector<T> data_;
};

} // aoo
