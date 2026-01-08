/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include <stdint.h>
#include <atomic>
#include <vector>
#include <cassert>

#if ATOMIC_LLONG_LOCK_FREE == 2
// uint64_t is always lockfree so we can used tagged handles
// (compressed pointers on 64-bit systems resp. DWCAS on 32-bit systems)
# define AOO_USE_TAGGED_HANDLE 1
# include "tagged_integer.hpp"
#else
# define AOO_USE_TAGGED_HANDLE 0
// fall back to spin lock
# include "sync.hpp"
#endif

namespace aoo {
namespace lockfree {

//-------------------- spsc_queue -----------------------//

// a lock-free single-producer/single-consumer queue which
// supports reading/writing data in fixed-sized blocks.
template<typename T, typename Alloc=std::allocator<T>>
class spsc_queue {
 public:
    spsc_queue(const Alloc& alloc = Alloc {})
        : data_(alloc) {}

    spsc_queue(const spsc_queue& other) = delete;

    // we need a move constructor so we can
    // put it in STL containers
    spsc_queue(spsc_queue&& other)
        : Alloc(static_cast<Alloc&&>(other)),
          balance_(other.balance_.load()),
          rdhead_(other.rdhead_),
          wrhead_(other.wrhead_),
          blocksize_(other.blocksize_),
          data_(std::move(other.data_)) {}

    spsc_queue& operator=(spsc_queue&& other){
        static_cast<Alloc&>(*this) = static_cast<Alloc&>(other);
        balance_ = other.balance_.load();
        rdhead_ = other.rdhead_;
        wrhead_ = other.wrhead_;
        blocksize_ = other.blocksize_;
        data_ = std::move(other.data_);
        return *this;
    }

    void resize(int32_t blocksize, int32_t capacity) {
    #if 0
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

    void shrink_to_fit(){
        data_.shrink_to_fit();
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
        return balance_.load(std::memory_order_acquire);
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

    template<typename Fn>
    void consume(Fn&& func) {
        func(data_[rdhead_]);
        read_commit(1);
    }

    template<typename Fn>
    void consume_all(Fn&& func) {
        while (read_available() > 0){
            consume(std::forward<Fn>(func));
        }
    }

    // returns: the number of available *blocks* for writing
    int32_t write_available() const {
        return capacity_ - balance_.load(std::memory_order_acquire);
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

    template<typename Fn>
    void produce(Fn&& func) {
        func(data_[wrhead_]);
        write_commit(1);
    }
 private:
    std::vector<T, Alloc> data_;
    std::atomic<int32_t> balance_{0};
    int32_t rdhead_{0};
    int32_t wrhead_{0};
    int32_t blocksize_{0};
    int32_t capacity_{0};

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

//--------------------- concurrent_queue -------------------------//

// based on https://www.drdobbs.com/parallel/writing-lock-free-code-a-corrected-queue/210604448
// and https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf.

namespace detail {

template<typename T>
struct node {
    template<typename... U>
    node(U&&... args)
        : next_(nullptr), data_(std::forward<U>(args)...) {}
    node* next_;
    T data_;
};

template<typename T>
struct atomic_node {
    std::atomic<atomic_node*> next_;
    T data_;

    template<typename... U>
    atomic_node(U&&... args)
        : next_(nullptr), data_(std::forward<U>(args)...) {}
};

#if AOO_USE_TAGGED_HANDLE
using tagged_handle = tagged_integer<uint64_t, 16>;

static_assert(std::atomic<tagged_handle>::is_always_lock_free, "tagged_handle not lockfree!");

template<typename T>
struct tagged_node {
    std::atomic<tagged_handle> next_{};
    T data_;

    template<typename... U>
    tagged_node(U&&... args)
        : data_(std::forward<U>(args)...) {}
};
#endif

template<typename Node, typename Alloc>
class node_allocator_base :
        std::allocator_traits<Alloc>::template rebind_alloc<Node>
{
    typedef typename std::allocator_traits<Alloc>::template rebind_alloc<Node> alloc_base;

protected:
    typedef Node node_type;

    node_allocator_base(const Alloc& alloc)
        : alloc_base(alloc) {}

    node_type* allocate() {
        return alloc_base::allocate(1);
    }

    void deallocate(node_type* node) {
        alloc_base::deallocate(node, 1);
    }
};

// generic helper to convert between node pointers and tagged handles
template<typename T, bool multi_producer>
struct node_traits {
#if AOO_USE_TAGGED_HANDLE
    using node_type = std::conditional_t<multi_producer, tagged_node<T>, node<T>>;
    using handle_type = std::conditional_t<multi_producer, tagged_handle, node<T>*>;

    static constexpr bool use_tagged_handle = multi_producer;

    static node_type* get_pointer(tagged_handle h) {
        return reinterpret_cast<node_type*>(h.get_value());
    }

    static node_type* get_pointer(node_type* n) {
        return n;
    }

    node_type* get_next_node(node_type* n) {
        if constexpr (use_tagged_handle) {
            return get_pointer(n->next_.load(std::memory_order_relaxed));
        } else {
            return n->next_;
        }
    }

    handle_type make_handle(node_type* n, size_t tag = 0) {
        if constexpr (use_tagged_handle) {
            return tagged_handle(reinterpret_cast<uintptr_t>(n), tag);
        } else {
            return n;
        }
    }

    handle_type make_handle(tagged_handle h, size_t tag) {
        return tagged_handle(h.get_value(), tag);
    }
#else
    using node_type = node<T>;
    using handle_type = node_type*;

    static constexpr bool use_tagged_handle = false;

    node_type* get_next_node(node_type* n) {
        return n->next_;
    }

    handle_type make_handle(node_type* n) {
        return n;
    }

    static node_type* get_pointer(node_type* n) {
        return n;
    }
#endif
};

} // detail

// Unbounded lock-free queue for one or more producers and a single consumer.
// If the template argument 'multi_producer' is 'true', multiple producers can push items
// without external synchronization. Why we don't support multiple consumers:
// - it would make it hard to support move-only types
// - we do not need it
//
// The general algorithm follows Herb Sutter's design in his article "A Corrected One-Producer,
// One-Consumer Lock-Free Queue". It has been modified to (optionally) support multiple producers
// by following the lock-free algorithm in "Simple, Fast, and Practical Non-Blocking and Blocking
// Concurrent Queue Algorithms" by Maged M. Michael and Michael L. Scott, as implemented in
// boost::lockfree::queue by Tim Blechmann.
//
// If the platform does not support lock-free 64-bit integers, concurrent producers are
// protected by a simple spinlock. Otherwise we prefer a true lock-free algorithm because
// spinlocks can be problematic if the producer threads have different priorities.
template<typename T, bool multi_producer = true, typename Alloc = std::allocator<T>>
class concurrent_queue : detail::node_traits<T, multi_producer>,
    detail::node_allocator_base<typename detail::node_traits<T, multi_producer>::node_type, Alloc>
{
    using node_traits = detail::node_traits<T, multi_producer>;
    using node_type = typename node_traits::node_type;
    using handle_type = typename node_traits::handle_type;

    using alloc_base = detail::node_allocator_base<node_type, Alloc>;

    static constexpr bool use_tagged_handle = node_traits::use_tagged_handle;

    using node_traits::get_pointer;
    using node_traits::get_next_node;
    using node_traits::make_handle;

#if !AOO_USE_TAGGED_HANDLE
    struct dummy_lock {
        void lock() {}
        void unlock() {}
    };
#endif

public:
    concurrent_queue(const Alloc& alloc = Alloc{})
        : alloc_base(alloc) {
        // add dummy node
        auto node = alloc_base::allocate();
        new (node) node_type();
        auto handle = make_handle(node);
        first_ = handle;
        divider_ = node;
        last_ = handle;
    }

    concurrent_queue(const concurrent_queue&) = delete;

    concurrent_queue(concurrent_queue&& other) = delete;

    ~concurrent_queue() {
        auto ptr = get_pointer(first_.load());
        while (ptr) {
            auto next = get_pointer(ptr->next_);
            ptr->~node_type();
            alloc_base::deallocate(ptr);
            ptr = next;
        }
    }

    // not thread-safe!
    void reserve(size_t n) {
        // check for existing empty nodes
        for (auto ptr = get_pointer(first_.load()), end = divider_.load();
             ptr != end; ptr = get_next_node(ptr)) {
            n--;
        }
        // add empty nodes
        for (size_t i = 0; i < n; ++i) {
            auto node = alloc_base::allocate();
            new (node) node_type();
            auto head = get_pointer(first_.load(std::memory_order_relaxed));
            node->next_ = make_handle(head);
            first_.store(make_handle(node), std::memory_order_relaxed);
        }
    }

    // if 'multi_producer' is true, this method can be called concurrently
    // by several threads without external synchronization.
    template<typename... U>
    void emplace(U&&... args) {
        auto node = get_node();
        node->data_ = T{std::forward<U>(args)...};
        push_node(node);
    }

    template<typename U>
    void push(U&& value) {
        auto node = get_node();
        node->data_ = std::forward<U>(value);
        push_node(node);
    }

    // see emplace()
    template<typename Fn>
    void produce(Fn&& func) {
        auto node = get_node();
        func(node->data_);
        push_node(node);
    }

    // must be called from a single thread!
    bool pop(T& result) {
        if constexpr (use_tagged_handle) {
            for (;;) {
                auto head = divider_.load(std::memory_order_relaxed);
                auto tail = last_.load(std::memory_order_acquire);
                // use node *after* divider, because the divider itself is always a dummy!
                // NB: load the next pointer *after* loading the tail! See CAS below.
                auto node = get_pointer(head->next_.load(std::memory_order_acquire));
                if (head == get_pointer(tail)) {
                    if (node == nullptr) {
                        return false;
                    }
                    // emplace() in progress -> help to advance tail pointer
                    auto new_tail = make_handle(node, tail.get_tag() + 1);
                    last_.compare_exchange_strong(tail, new_tail);
                } else {
                    result = std::move(node->data_); // get the data
                    divider_.store(node, std::memory_order_release); // publish new divider
                    return true;
                }
            }
        } else {
            auto head = divider_.load(std::memory_order_relaxed);
            if (head == get_pointer(last_.load(std::memory_order_acquire))) {
                return false;
            }
            // use node *after* divider, because the divider itself is always a dummy!
            auto node = get_next_node(head);
            result = std::move(node->data_); // get the data
            divider_.store(node, std::memory_order_release); // publish new divider
            return true;
        }
    }

    template<typename Fn>
    bool consume_one(Fn&& func) {
        // We *could* pass the original data to the function before storing
        // the divider to save an extra copy/move. However, this would delay
        // the reclamation of the consumed node. It's a trade-off. For trivially
        // copyable/movable types, the compiler will very likely optimize away
        // the temporary copy/move.
        T result;
        if (pop(result)) {
            func(std::move(result));
            return true;
        }
        return false;
    }

    template<typename Fn>
    size_t consume_all(Fn&& func) {
        size_t count = 0;
        while (consume_one(func)) {
            count++;
        }
        return count;
    }

    bool empty() const {
        return divider_.load(std::memory_order_acquire)
               == get_pointer(last_.load(std::memory_order_acquire));
    }

    // not thread-safe (?)
    void clear() {
        divider_.store(get_pointer(last_));
    }

private:
    std::atomic<handle_type> first_;
    std::atomic<node_type*> divider_; // only written by a single thread
    std::atomic<handle_type> last_;
#if !AOO_USE_TAGGED_HANDLE
    std::conditional_t<multi_producer, sync::spinlock, dummy_lock> lock_;
#endif

    node_type* try_pop_free_node() {
        if constexpr (use_tagged_handle) {
            // multi-producer with tagged handle
            auto handle = first_.load(std::memory_order_relaxed);
            for (;;) {
                auto ptr = get_pointer(handle);
                assert(ptr != nullptr);
                if (ptr == divider_.load(std::memory_order_acquire)) {
                    return nullptr;
                }
                auto next = ptr->next_.load(std::memory_order_relaxed);
                auto new_handle = make_handle(next, handle.get_tag() + 1);
                if (first_.compare_exchange_weak(handle, new_handle,
                                                 std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    ptr->next_ = make_handle(nullptr); // !
                    return ptr;
                }
            }
        } else {
            // single producer or multi-producer protected by spinlock
#if !AOO_USE_TAGGED_HANDLE
            std::unique_lock l(lock_); // may be dummy lock
#endif
            auto node = first_.load(std::memory_order_relaxed);
            assert(node != nullptr);
            if (node != divider_.load(std::memory_order_acquire)) {
                first_.store(node->next_, std::memory_order_relaxed);
                node->next_ = nullptr; // !
                return node;
            }
        }
        return nullptr;
    }

    node_type* get_node() {
        // try to reuse existing node
        auto node = try_pop_free_node();
        if (node == nullptr) {
            // allocate new node
            auto mem = alloc_base::allocate();
            node = new (mem) node_type{};
        }
        return node;
    }

    void push_node(node_type* node) {
        if constexpr (use_tagged_handle) {
            // multi-producer with tagged handle
            for (;;) {
                auto tail = last_.load(std::memory_order_acquire);
                auto tail_ptr = get_pointer(tail);
                auto next = tail_ptr->next_.load(std::memory_order_acquire);
                auto next_ptr = get_pointer(next);
                // make sure that tail and next are consistent.
                if (last_.load(std::memory_order_acquire) == tail) {
                    if (next_ptr == nullptr) {
                        auto new_tail_next = make_handle(node, next.get_tag() + 1);
                        if (tail_ptr->next_.compare_exchange_weak(next, new_tail_next)) {
                        #if 1
                            // this is not in the original algorithm
                            auto new_tail = make_handle(node, tail.get_tag() + 1);
                            last_.compare_exchange_strong(tail, new_tail);
                        #endif
                            return;
                        }
                    } else {
                        // caught another thread trying to set the tail's next pointer.
                        // help it to succeed before continuing!
                        auto new_tail = make_handle(next_ptr, tail.get_tag() + 1);
                        last_.compare_exchange_strong(tail, new_tail);
                    }
                }
            }
        } else {
            // single producer or multi-producer protected by spinlock
#if !AOO_USE_TAGGED_HANDLE
            std::lock_guard l(lock_); // may be dummy lock
#endif
            auto last = last_.load(std::memory_order_relaxed);
            last->next_ = node;
            last_.store(node, std::memory_order_release); // publish
        }
    }
};

//------------------------ concurrent_list ---------------------------------//

// A lock-free singly-linked list with RCU algorithm.
// It supports concurrent iteration and adding/removal of items,
// with a few restrictions:
// - you may only call methods while the list is locked; the only exception is reclaim()
// - you may only access list items while the list is (still) locked
// - nodes must not be removed concurrently resp. without external synchronization
template<typename T, typename Alloc = std::allocator<T>>
class concurrent_list :
    detail::node_allocator_base<detail::atomic_node<T>, Alloc>
{
    using alloc_base = detail::node_allocator_base<detail::atomic_node<T>, Alloc>;
    using node_type = detail::atomic_node<T>;

public:
    template<typename U>
    class base_iterator {
        friend class concurrent_list;
        U* node_;
    public:
        typedef std::ptrdiff_t difference_type;
        typedef U value_type;
        typedef U& reference;
        typedef U* pointer;
        typedef std::forward_iterator_tag iterator_category;

        base_iterator()
            : node_(nullptr) {}

        base_iterator(U* n)
            : node_(n) {}

        base_iterator(const base_iterator&) = default;
        base_iterator& operator=(const base_iterator&) = default;

        T& operator*() { return node_->data_; }

        T* operator->() { return &node_->data_; }

        base_iterator& operator++() {
            node_ = node_->next_.load(std::memory_order_acquire);
            return *this;
        }

        base_iterator operator++(int) {
            base_iterator old(node_);
            node_ = node_->next_.load(std::memory_order_acquire);
            return old;
        }

        bool operator==(const base_iterator& other) {
            return node_ == other.node_;
        }

        bool operator!=(const base_iterator& other) {
            return node_ != other.node_;
        }
    };

    using iterator = base_iterator<node_type>;
    using const_iterator = base_iterator<const node_type>;

    concurrent_list(const Alloc& alloc = Alloc{})
        : alloc_base(alloc) {}

    concurrent_list(const concurrent_list&) = delete;
    concurrent_list(concurrent_list&& other) = delete;

    ~concurrent_list() {
        destroy_list(head_.load());
        destroy_list(freed_.load());
    }

    // NB: can be called concurrently (while the list is locked)
    template<typename... U>
    iterator emplace_front(U&&... args) {
        auto mem = alloc_base::allocate();
        auto node = new (mem) node_type(std::forward<U>(args)...);
        auto next = head_.load(std::memory_order_relaxed);
        do {
            node->next_.store(next, std::memory_order_relaxed);
            // check if the head has changed and update it atomically.
            // (if the CAS fails, 'next' is updated to the current head)
        } while (!head_.compare_exchange_weak(next, node, std::memory_order_acq_rel)) ;
        return iterator(node);
    }

    iterator push_front(T&& v) {
        return emplace_front(std::forward<T>(v));
    }

    // pop the first element. This is UB if the list is empty!
    // NB: don't call concurrently with other pop/erase/clear methods!
    void pop_front() {
        T* head = head_.load(std::memory_order_relaxed);
        T* next;
        do {
            next = head->next_.load(std::memory_order_relaxed);
            // check if the head has changed and update it atomically.
            // (if the CAS fails, 'head' is updated to the current head)
            // NB: there is no ABA problem because pop_front() must not
            // be called concurrently.
        } while (!head_.compare_exchange_weak(head, next, std::memory_order_acq_rel));

        dispose_node(head);
    }

    // tries to erase the given element. On success, it returns an iterator
    // pointing to the next element in the list. On failure, it returns
    // an empty iterator.
    // NB: don't call concurrently with other pop/erase/clear methods!
    iterator erase(iterator it) {
        for (;;) {
            auto n = head_.load(std::memory_order_acquire);
            if (n == it.node_) {
                // try to remove head
                // NB: there is no ABA problem because erase() must not
                // be called concurrently.
                auto next = n->next_.load(std::memory_order_acquire);
                if (head_.compare_exchange_strong(n, next, std::memory_order_acq_rel)) {
                    dispose_node(n);
                    return iterator(next); // success
                }
                // someone pushed a new node in between, try again!
            } else {
                // find the node before it
                while (n) {
                    auto next = n->next_.load(std::memory_order_acquire);
                    if (next == it.node_) {
                        // unlink the node
                        auto next2 = next->next_.load(std::memory_order_acquire);
                        n->next_.store(next2, std::memory_order_release);
                        dispose_node(next);
                        return iterator(next2);
                    }
                    n = next;
                }
                // reached end of list (shouldn't happen)
                return iterator{};
            }
        }
    }

    // get a reference to the first element. This is UB if the list is empty.
    T& front() { return *begin(); }

    T& front() const { return *begin(); }

    iterator begin() {
        return iterator(head_.load(std::memory_order_acquire));
    }

    const_iterator begin() const {
        return const_iterator(head_.load(std::memory_order_acquire));
    }

    iterator end() {
        return iterator();
    }

    const_iterator end() const {
        return const_iterator();
    }

    bool empty() const {
        // I *think* this could be a relaxed load, but I'm not entirely sure...
        return head_.load(std::memory_order_acquire) == nullptr;
    }

    // NB: cannot be called concurrently with other pop/erase/clear methods!
    void clear() {
        // atomically unlink the whole list
        auto head = head_.exchange(nullptr);
        if (head) {
            // and move it to the free list
            dispose_list(head);
        }
    }

    void lock() {
        refcount_.fetch_add(1, std::memory_order_acquire);
    }

    void unlock() {
        refcount_.fetch_sub(1, std::memory_order_release);
    }

    bool need_reclaim() const {
        return freed_.load(std::memory_order_relaxed) != nullptr;
    }

    // This method is called periodically from a non-RT thread to collect garbage.
    // Always call in unlocked state!
    bool reclaim() {
        // check if the list appears be non-empty. if yes, also check the refcount
        if (need_reclaim() && !refcount_.load(std::memory_order_acquire)) {
            // atomically unlink the whole freelist
            auto f = freed_.exchange(nullptr);
            if (!f) {
                return false; // shouldn't really happen...
            }
            // check the refcount again.
            // use RMW operation to prevent reordering in both directions.
            int32_t expected = 0;
            if (refcount_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel)) {
                // from this point the refcount may go up again, but it wouldn't
                // refer to our list items, so we can safely free the memory.
                destroy_list(f);
                return true;
            } else {
                // A reader aquired access in the meantime, so we put the items back
                // to the free list and try again later. If the free list is still empty,
                // we can simply atomically exchange the head pointer.
                node_type* expected = nullptr;
                if (!freed_.compare_exchange_strong(expected, f)) {
                    // otherwise prepend the old free list to the new one
                    dispose_list(f);
                }
            }
        }
        return false;
    }
private:
    std::atomic<node_type*> head_{nullptr};
    std::atomic<node_type*> freed_{nullptr};
    std::atomic<int32_t> refcount_{0};

    void dispose_node(node_type* n) {
        // atomically add node to free list
        auto next = freed_.load(std::memory_order_relaxed);
        do {
            n->next_.store(next, std::memory_order_relaxed);
            // check if the head has changed and update it atomically.
            // (if the CAS fails, 'next' is updated to the current head)
        } while (!freed_.compare_exchange_weak(next, n, std::memory_order_acq_rel));
    }

    void dispose_list(node_type* list) {
        // get last node in list
        auto tail = list;
        for (;;) {
            auto next = tail->next_.load(std::memory_order_relaxed);
            if (next) {
                tail = next;
            } else {
                break;
            }
        }
        // prepend to the free list; 'list' becomes new head
        // NB: there is no ABA problem because this method is only called
        // from reclaim(), so nobody can concurrently *remove* items.
        // (The ABA problem can only occur with concurrent pop operations.)
        // New items might be pushed concurrently by erase().
        auto head = freed_.load(std::memory_order_relaxed);
        do {
            tail->next_.store(head, std::memory_order_relaxed);
            // check if the head has changed and update it atomically.
            // (if the CAS fails, 'head' is updated to the actual list head)
        } while (!freed_.compare_exchange_weak(head, list, std::memory_order_acq_rel)) ;
    }

    void destroy_list(node_type* n) {
        while (n) {
            auto tmp = n;
            n = n->next_.load(std::memory_order_relaxed);
            tmp->~node_type();
            alloc_base::deallocate(tmp);
        }
    }
};

} // lockfree
} // aoo
