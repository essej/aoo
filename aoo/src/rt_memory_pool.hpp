#pragma once

#include "common/bit_utils.hpp"
#include "common/log.hpp"
#include "common/tagged_integer.hpp"
#include "common/utils.hpp"

#include <limits.h>
#include <stdint.h>
#include <memory>
#include <atomic>
#include <array>
#include <cassert>
#include <type_traits>

#include <iostream>

#include <stdint.h>
#include <math.h>
#include <atomic>

namespace aoo {

void * allocate(size_t size);
void deallocate(void *ptr, size_t);

namespace detail {

// TODO: explain why the bitset needs to be tagged
template<typename T>
struct tagged_bitset {
    static constexpr size_t width = sizeof(T) * CHAR_BIT;
    T bits;
    uint16_t tag; // always uint16_t, so we get the same behavior across platforms

    tagged_bitset(T bits_ = 0, uint16_t tag_ = 0) noexcept
        : bits(bits_), tag(tag_) {}

    void set(size_t index, bool state) {
        if (state) {
            bits |= (T)1 << index;
        } else {
            bits &= ~((T)1 << index);
        }
    }

    bool get(size_t index) const {
        return (bits >> index) & 1;
    }

    bool empty() const {
        return bits == 0;
    }

    size_t highest_bit() const {
        assert(bits != 0);
        return 31 - clz((uint32_t)bits);
    }
};

template<typename Alloc = std::allocator<char>>
class concurrent_linear_allocator : std::allocator_traits<Alloc>::template rebind_alloc<char> {
    using alloc_type = typename std::allocator_traits<Alloc>::template rebind_alloc<char>;
public:
    concurrent_linear_allocator(const Alloc& alloc = Alloc{})
        : alloc_type(alloc) {}

    concurrent_linear_allocator(size_t size, const Alloc& alloc = Alloc{})
        : alloc_type(alloc) {
        resize(size);
    }

    concurrent_linear_allocator(concurrent_linear_allocator&& other) = delete;
    concurrent_linear_allocator& operator=(concurrent_linear_allocator&&) = delete;

    ~concurrent_linear_allocator() {
        if (alloc_size_ > 0) {
            alloc_type::deallocate(memory_, alloc_size_);
        }
    }

    void* allocate(size_t size) {
        // NB: do not use fetch_add!
        auto index = index_.load(std::memory_order_relaxed);
        while ((capacity_ - index) >= size) {
            if (index_.compare_exchange_weak(index, index + size,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return data_ + index;
            } // retry and reload index
        }
        return nullptr;
    }

    void resize(size_t size) {
        if (alloc_size_ > 0) {
            alloc_type::deallocate(memory_, alloc_size_);
        }
        // align to page
        const size_t page_size = 4096;
        alloc_size_ = size + page_size;
        memory_ = alloc_type::allocate(alloc_size_);
        data_ = (char *)(((uintptr_t)memory_ + page_size - 1) & ~(page_size - 1));
        capacity_ = size;
        reset();
    }

    void reset() {
        index_ = 0;
    }

    size_t count() const {
        return index_.load();
    }

    bool contains(char * ptr) const {
        return (uintptr_t)ptr >= (uintptr_t)data_ && (uintptr_t)ptr < ((uintptr_t)data_ + capacity_);
    }

    size_t capacity() const {
        return capacity_;
    }

    const char *data() const {
        return data_;
    }
private:
    char *data_{nullptr}; // pool start (aligned to page size)
    std::atomic<size_t> index_{0}; // begin of next allocation
    size_t capacity_{0}; // nominal capacity
    char *memory_{nullptr}; // pointer to memory block
    size_t alloc_size_{0}; // size of memory block
};

// NB: we use indices into the memory arena instead of pointers
// because some platforms do not support the necessary DWCAS operations.
// For example, XTensa (ESP32), 32-bit RISC-V and some 32-bit ARM CPUs
// only support 32-bit atomic operations.
using tagged_index = std::conditional_t<std::atomic<tagged_integer<uint64_t>>::is_always_lock_free,
    tagged_integer<uint64_t>, tagged_integer<uint32_t, 12>>;

// NB: make sure that max_index_value fits into size_t!
constexpr size_t max_index_value = std::min<tagged_index::type>(tagged_index::max_value, SIZE_MAX);

struct memory_block {
    union {
        size_t next;
        char data[1];
    };
};

class free_list {
public:
    void push(const void *pool, void *ptr) {
        auto block = static_cast<memory_block *>(ptr);
        auto index = (char *)ptr - (const char *)pool;
        auto head = head_.load(std::memory_order_relaxed);
        for (;;) {
            block->next = head.get_value();
            tagged_index new_head((size_t)index, head.get_tag() + 1);
            if (head_.compare_exchange_weak(head, new_head,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                break;
            }
        }
    }

    void* pop(const void *pool) {
        auto head = head_.load(std::memory_order_relaxed);
        for (;;) {
            auto index = head.get_value();
            if (index == sentinel) {
                return nullptr;
            }
            // NB: another thread may concurrently pop the head and overwrite
            // the 'next' field in the memory block. Technically, this is UB,
            // but we don't care because the CAS loop will fail anyway.
            // However, this means that must not assert that the value is
            // actually in the range [0, max_index_size(!
            auto block = reinterpret_cast<const memory_block*>((const char *)pool + index);
            tagged_index new_head(block->next, head.get_tag() + 1);
            if (head_.compare_exchange_weak(head, new_head,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return (void *)block;
            }
        }
    }

    bool empty() const {
        auto head = head_.load(std::memory_order_relaxed);
        return head.get_value() != sentinel;
    }

    void reset() {
        head_ = tagged_index(sentinel, 0);
    }

    size_t count(const void *pool) const {
        size_t n = 0;
        auto index = head_.load().get_value();
        while (index != sentinel) {
            auto block = reinterpret_cast<const memory_block*>((const char *)pool + index);
            index = block->next;
            n++;
        }
        return n;
    }
private:
    // NB: 0 (nullptr) is a valid index, so we cannot use it as our sentinel index.
    // Instead we use the largest possible representable value.
    static constexpr size_t sentinel = max_index_value;
    std::atomic<tagged_index> head_{tagged_index{sentinel, 0}};
};

} // namespace detail

#ifndef AOO_RT_MEMORY_POOL_BITSET
#define AOO_RT_MEMORY_POOL_BITSET 1
#endif

#ifndef AOO_DEBUG_RT_MEMORY
#define AOO_DEBUG_RT_MEMORY 0
#endif

#ifndef AOO_RT_MEMORY_LEAK_DETECTION
// enabled by default in debug builds
#if !defined(NDEBUG)
#define AOO_RT_MEMORY_LEAK_DETECTION 1
#else
#define AOO_RT_MEMORY_LEAK_DETECTION 0
#endif
#endif

template<bool grow = true, typename Alloc = std::allocator<char>>
class rt_memory_pool : std::allocator_traits<Alloc>::template rebind_alloc<char> {
    using alloc_type = typename std::allocator_traits<Alloc>::template rebind_alloc<char>;

    static constexpr size_t block_alignment = 64;
    static constexpr size_t bucket_count = 128;
    static constexpr size_t large_bucket_offset = 64;
    static constexpr size_t small_alloc_limit = 4096;
    static constexpr size_t small_alloc_limit_bits = 12;
    static constexpr size_t large_alloc_limit = 65535;

    using bitset = std::conditional_t<std::atomic<detail::tagged_bitset<uint32_t>>::is_always_lock_free,
                                      detail::tagged_bitset<uint32_t>, detail::tagged_bitset<uint16_t>>;
public:
    static constexpr size_t max_pool_size = detail::max_index_value;

    rt_memory_pool(const Alloc& alloc = Alloc{})
        : alloc_type(alloc), linear_allocator_(alloc) {
        // populate bucket size array
        for (size_t i = 0; i < bucket_sizes_.size(); ++i) {
            if (i >= large_bucket_offset) {
                auto pow2 = 1 << ((i - large_bucket_offset) / 16 + small_alloc_limit_bits);
                auto rem = ((i - large_bucket_offset) % 16) * pow2 / 16;
                auto step = pow2 >> 4;
                auto size = pow2 + ((rem + step) & ~(step - 1));
                bucket_sizes_[i] = size;
            } else {
                bucket_sizes_[i] = (i + 1) * block_alignment;
            }
        }
    }

    rt_memory_pool(const rt_memory_pool&) = delete;
    rt_memory_pool& operator=(const rt_memory_pool&) = delete;

    ~rt_memory_pool() { check_leaks(); }

    void reset() {
        check_leaks();
        linear_allocator_.reset();
        for (auto& fl : buckets_) {
            fl.reset();
        }
        warned_.store(false);
    }

    void resize(size_t size) {
        linear_allocator_.resize(std::min(size, max_pool_size));
        reset();
    }

    size_t capacity() const {
        return linear_allocator_.capacity();
    }

    void* allocate(size_t size) {
#if AOO_RT_MEMORY_LEAK_DETECTION || AOO_DEBUG_RT_MEMORY
        auto num_blocks = num_blocks_.fetch_add(1, std::memory_order_acquire) + 1;
        auto num_bytes = num_bytes_.fetch_add(size, std::memory_order_acquire) + size;
#if AOO_DEBUG_RT_MEMORY
        LOG_DEBUG("rt_memory_pool: allocate " << size << " bytes (" << num_bytes
                  << " bytes, " << num_blocks << " blocks total)");
#endif
#endif
        if (size > large_alloc_limit) {
            LOG_INFO("RT memory request (" << size << " bytes) too large - using default allocator");
            // fall back to heap allocation
            return alloc_type::allocate(size);
        } else if (size > 0) {
            auto ptr = find_block(size);
            if (!ptr) {
                auto alloc_size = get_alloc_size(size);
                ptr = linear_allocator_.allocate(alloc_size);
            }
            if (grow && !ptr) {
                if (!warned_.exchange(true)) {
                    LOG_WARNING("RT memory pool exhausted - using default allocator");
                }
                ptr = alloc_type::allocate(size);
            }
            return ptr;
        } else {
            return nullptr;
        }
    }

    void deallocate(void *ptr, size_t size) {
#if AOO_RT_MEMORY_LEAK_DETECTION || AOO_DEBUG_RT_MEMORY
        auto num_blocks = num_blocks_.fetch_sub(1, std::memory_order_acquire) - 1;
        auto num_bytes = num_bytes_.fetch_sub(size, std::memory_order_acquire) - size;
#if AOO_DEBUG_RT_MEMORY
        LOG_DEBUG("rt_memory_pool: deallocate " << size << " bytes (" << num_bytes
                  << " bytes, " << num_blocks << " blocks remaining)");
#endif
#endif
        if (size > large_alloc_limit) {
            // fall back to heap allocation
            alloc_type::deallocate((char *)ptr, size);
        } else if (size > 0) {
            assert(ptr != nullptr);
            if (grow && !linear_allocator_.contains((char *)ptr)) {
                alloc_type::deallocate((char *)ptr, size);
            } else {
                return_block(ptr, size);
            }
        }
    }

    void print() {
        std::cout << "total RT memory usage: " << memory_usage()
                  << " / " << capacity() << " bytes" << std::endl;

        for (size_t i = 0; i < buckets_.size(); ++i) {
            auto count = buckets_[i].count(linear_allocator_.data());
            auto bytes = count * bucket_sizes_[i];
            std::cout << "bucket " << i << ": " << count << " elements, "
                      << bytes << " bytes" << std::endl;
        }
    }

    size_t memory_usage() const {
        return linear_allocator_.count();
    }
private:
    std::array<detail::free_list, bucket_count> buckets_;
    std::array<uint32_t, bucket_count> bucket_sizes_;
#if AOO_RT_MEMORY_POOL_BITSET
    std::array<std::atomic<bitset>, bucket_count / bitset::width> bitset_;
#endif
    detail::concurrent_linear_allocator<Alloc> linear_allocator_;
#if AOO_RT_MEMORY_LEAK_DETECTION || AOO_DEBUG_RT_MEMORY
    std::atomic<ptrdiff_t> num_blocks_{0};
    std::atomic<ptrdiff_t> num_bytes_{0};
#endif
    std::atomic_bool warned_{false};

    void check_leaks() {
#if AOO_RT_MEMORY_LEAK_DETECTION || AOO_DEBUG_RT_MEMORY
        auto num_blocks = num_blocks_.exchange(0);
        auto num_bytes = num_bytes_.exchange(0);
        if (num_blocks != 0 || num_bytes != 0) {
            LOG_ERROR("rt_memory_pool: leaked " << num_blocks
                      << " blocks and " << num_bytes << " bytes");
            assert(false);
        } else if (memory_usage() > 0) {
            LOG_DEBUG("rt_memory_pool: no leaks detected");
        }
#endif
    }

    size_t get_alloc_size(size_t size) {
        if (size > small_alloc_limit) {
        #if 1
            auto index = size_to_index(size);
            return bucket_sizes_[index];
        #else
            auto ilog2 = 31 - clz(size);
            auto pow2 = 1 << ilog2;
            auto mask = pow2 - 1;
            auto rem = size & mask;
            auto step = pow2 >> 4;
            return pow2 + ((rem + step) & ~(step - 1));
        #endif
        } else {
            return (size + block_alignment - 1) & ~(block_alignment - 1);
        }
    }

    static uint32_t size_to_index(uint32_t size) {
        if (size > small_alloc_limit) {
            auto ilog2 = 31 - clz(size);
            // auto pow2 = 1 << ilog2;
            // auto mask = pow2 - 1;
            // auto rem = size & mask;
            const auto k = large_bucket_offset - small_alloc_limit_bits * 16;
            // 1. return (ilog2 + rem / pow2 - small_alloc_limit_bits) * 16 + large_bucket_offset;
            // 2. return ilog2 * 16 + rem * 16 / pow2 + k;
            // 3. return (ilog2 << 4) + ((size & ((1 << ilog2) - 1)) >> (ilog2 - 4)) + k;
            auto index = (ilog2 << 4) + ((size >> (ilog2 - 4)) & 15) + k;
            assert(index >= 64 && index < 128);
            return index;
        } else {
            // [1, 64] go to slot 0, [65 - 128] go to slot 1, etc.
            return (size - 1) / block_alignment;
        }
    }

    void* find_block(size_t size) {
        auto index = size_to_index(size);
        assert(index < buckets_.size());
        // first try to get block of matching size
        auto ptr = pop_block(index);
        if (!ptr) {
            ptr = find_matching_block(index);
        }
        return ptr;
    }

    void* find_matching_block(size_t start_index) {
        // find the largest available block above 'start_index'
    #if AOO_RT_MEMORY_POOL_BITSET
        // 1) iterate over bitsets (in reverse)
        size_t offset = start_index / bitset::width;
        for (int k = (int)bitset_.size() - 1; k >= (int)offset; --k) {
            auto bitset = bitset_[k].load(std::memory_order_relaxed);
            if (bitset.empty()) {
                continue;
            }
            // iterate over bits (in reverse)
            for (int i = (int)bitset.highest_bit(); i >= 0; --i) {
            #if 0
                // reload bitset
                bitset = bitset_[k].load(std::memory_order_relaxed);
            #endif
                if (bitset.get(i)) {
                    auto block_index = k * bitset::width + i;
                    if (block_index > start_index) {
                        auto ptr = pop_block(block_index);
                        if (ptr) {
                            if (block_index >= large_bucket_offset) {
                                split_block(ptr, block_index, start_index);
                            }
                            return ptr;
                        }
                        // try next bit
                    } else {
                        return nullptr;
                    }
                }
            }
        }
    #else
        // simple linear search (in reverse)
        for (auto i = buckets_.size() - 1; i > start_index; --i) {
            auto ptr = pop_block(i);
            if (ptr) {
                if (i >= large_bucket_offset) {
                    split_block(ptr, i, start_index);
                }
                return ptr;
            }
        }
    #endif
        return nullptr;
    }

    void split_block(void *ptr, size_t block_index, size_t start_index) {
        // now split the block
        auto alloc_size = bucket_sizes_[start_index];
        auto mem_size = bucket_sizes_[block_index] - alloc_size;
        auto mem_ptr = (char *)ptr + alloc_size;
        while (mem_size > 0) {
            auto index = size_to_index(mem_size);
            if (index >= large_bucket_offset) {
                // large remainder, use lower index and split
                index--;
                push_block(mem_ptr, index);
                auto block_size = bucket_sizes_[index];
                assert(mem_size >= block_size);
                mem_ptr += block_size;
                mem_size -= block_size;
            } else {
                // small remainder - always exact match
                assert(bucket_sizes_[index] == mem_size);
                push_block(mem_ptr, index);
                break;
            }
        }
    }

    void return_block(void *ptr, size_t size) {
        auto index = size_to_index(size);
        assert(index < buckets_.size());
        assert(index < linear_allocator_.capacity());
        push_block(ptr, index);
    }

#if AOO_RT_MEMORY_POOL_BITSET
    void update_bitset(size_t index) {
        auto k = index / bitset::width;
        auto i = index & (bitset::width - 1);
        // set bit atomically with ABA protection.
        // this guarantees that the most recent caller writes the correct state.
        // NB: we need to load the bitset with memory_order_acquire so that the
        // bucket check cannot be moved across!
        auto bs = bitset_[k].load(std::memory_order_acquire);
        for (;;) {
            bitset bs_new(bs.bits, bs.tag + 1);
            bs_new.set(i, !buckets_[index].empty());
            if (bitset_[k].compare_exchange_weak(bs, bs_new,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                break;
            }
        }
    }
#endif

    void push_block(void *ptr, size_t index) {
        buckets_[index].push(linear_allocator_.data(), ptr);
    #if AOO_RT_MEMORY_POOL_BITSET
        update_bitset(index);
    #endif
    }

    void* pop_block(size_t index) {
        auto ptr = buckets_[index].pop(linear_allocator_.data());
    #if AOO_RT_MEMORY_POOL_BITSET
        if (ptr) {
            update_bitset(index);
        }
    #endif
        return ptr;
    }
};

} // namespace aoo
