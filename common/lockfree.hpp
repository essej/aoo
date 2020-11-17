/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include <stdint.h>
#include <atomic>
#include <vector>
#include <cassert>

namespace aoo {
namespace lockfree {

/*////////////////////// queue /////////////////////////*/

// a lock-free single-producer/single-consumer queue which
// supports reading/writing data in fixed-sized blocks.
template<typename T>
class spsc_queue {
 public:
    spsc_queue() = default;
    // we need a move constructor so we can
    // put it in STL containers
    spsc_queue(spsc_queue&& other)
        : balance_(other.balance_.load()),
          rdhead_(other.rdhead_),
          wrhead_(other.wrhead_),
          blocksize_(other.blocksize_),
          data_(std::move(other.data_)) {}

    spsc_queue& operator=(spsc_queue&& other){
        balance_ = other.balance_.load();
        rdhead_ = other.rdhead_;
        wrhead_ = other.wrhead_;
        blocksize_ = other.blocksize_;
        data_ = std::move(other.data_);
        return *this;
    }

    void resize(int32_t blocksize, int32_t capacity) {
    #if 1
        data_.clear(); // force zero
    #endif
        data_.resize(blocksize * capacity);
        capacity_ = capacity;
        blocksize_ = blocksize;
        reset();
    }

    void resize(int32_t capacity){
        resize(1, capacity);
    }

    int32_t blocksize() const { return blocksize_; }

    // max. number of *blocks*
    int32_t capacity() const {
        return capacity_;
    }

    void reset() {
        rdhead_ = wrhead_ = 0;
        balance_ = 0;
    }
    // returns: the number of available *blocks* for reading
    int32_t read_available() const {
        return balance_.load(std::memory_order_relaxed);
    }

    void read(T& out) {
        out = std::move(data_[rdhead_]);
        read_commit(1);
    }

    const T* read_data() const {
        return &data_[rdhead_];
    }

    void read_commit() {
        read_commit(blocksize_);
    }
    // returns: the number of available *blocks* for writing
    int32_t write_available() const {
        return capacity_ - balance_.load(std::memory_order_relaxed);
    }

    template<typename U>
    void write(U&& value) {
        data_[wrhead_] = std::forward<U>(value);
        write_commit(1);
    }

    T* write_data() {
        return &data_[wrhead_];
    }

    void write_commit() {
        write_commit(blocksize_);
    }
 private:
    std::atomic<int32_t> balance_{0};
    int32_t rdhead_{0};
    int32_t wrhead_{0};
    int32_t blocksize_{0};
    int32_t capacity_{0};
    std::vector<T> data_;

    void read_commit(int32_t n){
        rdhead_ += n;
        if (rdhead_ == data_.size()){
            rdhead_ = 0;
        }
        auto b = balance_.fetch_sub(1, std::memory_order_release);
        assert(b > 0);
    }

    void write_commit(int32_t n){
        wrhead_ += n;
        if (wrhead_ == data_.size()){
            wrhead_ = 0;
        }
        auto b = balance_.fetch_add(1, std::memory_order_release);
        assert(b < capacity_);
    }
};

/*///////////////////////// list ////////////////////////*/

// a lock-free singly-linked list which supports adding items and iteration.
// clearing the list is *not* thread-safe

template<typename T>
class simple_list {
    struct node {
        node* next_;
        T data_;
        template<typename... U>
        node(U&&... args)
            : next_(nullptr), data_(std::forward<U>(args)...) {}
    };
public:
    template<typename U>
    class base_iterator {
        friend class simple_list;
        U *node_;
    public:
        base_iterator()
            : node_(nullptr){}
        base_iterator(U *n)
            : node_(n){}
        base_iterator(const base_iterator&) = default;
        base_iterator& operator=(const base_iterator&) = default;
        T& operator*() { return node_->data_; }
        T* operator->() { return &node_->data_; }
        base_iterator& operator++() {
            node_ = node_->next_;
            return *this;
        }
        base_iterator operator++(int) {
            base_iterator old = *this;
            node_ = node_->next_;
            return old;
        }
        bool operator==(const base_iterator& other){
            return node_ == other.node_;
        }
        bool operator!=(const base_iterator& other){
            return node_ != other.node_;
        }
    };
    using iterator = base_iterator<node>;
    using const_iterator = base_iterator<const node>;

    simple_list()
        : head_(nullptr){}

    simple_list(const simple_list&) = delete;

    simple_list(simple_list&& other){
        // not sure...
        auto head = other.head_.exchange(nullptr);
        head_.store(head);
    }

    ~simple_list(){
        clear();
    }

    simple_list& operator=(simple_list&& other){
        // not sure...
        auto head = other.head_.exchange(nullptr);
        head_.store(head);
        return *this;
    }

    template<typename... U>
    void emplace_front(U&&... args){
        auto n = new node(std::forward<U>(args)...);
        push_front(n);
    }

    void push_front(node* n){
        n->next_ = head_.load(std::memory_order_relaxed);
        // check if the head has changed and update it atomically
        while (!head_.compare_exchange_weak(n->next_, n)) ;
    }

    // be careful with concurrent iteration!
    node* take_front(){
        auto head = head_.load(std::memory_order_relaxed);
        // check if the head has changed and update it atomically
        while (!head_.compare_exchange_weak(head, head->next_)) ;
        return head;
    }

    // not thread-safe!
    std::pair<node*, iterator> take(iterator it){
        node* prev = nullptr;
        auto n = head_.load(std::memory_order_relaxed);
        while (n){
            if (it.node_ == n){
                if (prev){
                    prev->next_ = n->next_;
                } else {
                    head_.store(n->next_);
                }
                return { n, n->next_ };
            }
            prev = n;
            n = n->next_;
        }
        return { nullptr, iterator{} };
    }

    T& front() { return *begin(); }

    T& front() const { return *begin(); }

    iterator begin(){
        return iterator(head_.load(std::memory_order_relaxed));
    }

    const_iterator begin() const {
        return const_iterator(head_.load(std::memory_order_relaxed));
    }

    iterator end(){
        return iterator();
    }

    const_iterator end() const {
        return const_iterator();
    }

    int32_t empty() const {
        return head_.load(std::memory_order_relaxed) != nullptr;
    }

    // the deletion of nodes itself is not thread-safe!!!
    void clear(){
        auto it = head_.exchange(nullptr);
        while (it){
            auto next = it->next_;
            delete it;
            it = next;
        }
    }
private:
    std::atomic<node *> head_{nullptr};
};

} // lockfree
} // aoo
