#pragma once

#include <stdint.h>
#include <atomic>
#include <vector>
#include <cassert>


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
          rdsize_(other.rdsize_),
          wrsize_(other.wrsize_),
          data_(std::move(other.data_))
    {}
    lfqueue& operator=(lfqueue&& other){
        balance_ = other.balance_.load();
        rdhead_ = other.rdhead_;
        wrhead_ = other.wrhead_;
        rdsize_ = other.rdsize_;
        wrsize_ = other.wrsize_;
        data_ = std::move(other.data_);
        return *this;
    }

    void resize(int32_t size, int32_t rdsize, int32_t wrsize) {
        // check if size is divisible by both rdsize and wrsize
        assert(size >= rdsize);
        assert(size >= wrsize);
        assert((size % rdsize) == 0);
        assert((size % wrsize) == 0);
        data_.clear(); // force zero
        data_.resize(size);
        rdsize_ = rdsize;
        wrsize_ = wrsize;
        reset();
    }

    int32_t capacity() const { return data_.size(); }

    void reset() {
        rdhead_ = wrhead_ = 0;
        balance_ = 0;
    }
    // returns: the number of available *blocks* for reading
    int32_t read_available() const {
        return balance_.load(std::memory_order_acquire) / rdsize_;
    }

    const T* read_data() const {
        return &data_[rdhead_];
    }

    int32_t read_size() const {
        return rdsize_;
    }

    void read_commit() {
        rdhead_ = (rdhead_ + rdsize_) % capacity();
        balance_ -= rdsize_;
        assert(balance_ >= 0);
    }
    // returns: the number of available *blocks* for writing
    int32_t write_available() const {
        return (capacity() - balance_.load(std::memory_order_acquire)) / wrsize_;
    }

    T* write_data() {
        return &data_[wrhead_];
    }

    int32_t write_size() const {
        return wrsize_;
    }

    void write_commit() {
        wrhead_ = (wrhead_ + wrsize_) % capacity();
        balance_ += wrsize_;
        assert(balance_ <= capacity());
    }
 private:
    std::atomic<int32_t> balance_{0};
    int32_t rdhead_{0};
    int32_t wrhead_{0};
    int32_t rdsize_{0};
    int32_t wrsize_{0};
    std::vector<T> data_;
};
