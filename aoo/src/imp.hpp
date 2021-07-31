#pragma once

#include "aoo/aoo.h"

#include "common/net_utils.hpp"

#include <stdint.h>
#include <cstring>
#include <utility>
#include <memory>
#include <atomic>

//--------------- helper functions ----------------//

namespace aoo {

uint32_t make_version();

bool check_version(uint32_t version);

char * copy_string(const char *s);

void free_string(char *s);

void * copy_sockaddr(const void *sa, int32_t len);

void free_sockaddr(void *sa, int32_t len);

namespace net {

AooError parse_pattern(const AooByte *msg, int32_t n,
                       AooMsgType& type, int32_t& offset);

} // net

//---------------- endpoint ------------------------//

struct endpoint {
    endpoint() = default;
    endpoint(const ip_address& _address, int32_t _id, uint32_t _flags)
        : address(_address), id(_id), flags(_flags) {}

    // data
    ip_address address;
    AooId id = 0;
    uint32_t flags = 0;
};

inline std::ostream& operator<<(std::ostream& os, const endpoint& ep){
    os << ep.address << "|" << ep.id;
    return os;
}

struct sendfn {
    sendfn(AooSendFunc fn = nullptr, void *user = nullptr)
        : fn_(fn), user_(user) {}

    void operator() (const AooByte *data, AooInt32 size,
                      const ip_address& addr, AooFlag flags = 0) const {
        fn_(user_, data, size, addr.address(), addr.length(), flags);
    }

    void operator() (const AooByte *data, AooInt32 size,
                     const endpoint& ep) const {
        fn_(user_, data, size,
            ep.address.address(), ep.address.length(), ep.flags);
    }

    AooSendFunc fn() const { return fn_; }

    void * user() const { return user_; }
private:
    AooSendFunc fn_;
    void *user_;
};

//---------------- allocator -----------------------//


#if AOO_CUSTOM_ALLOCATOR || AOO_DEBUG_MEMORY

void * allocate(size_t size);

template<class T, class... U>
T * construct(U&&... args){
    auto ptr = allocate(sizeof(T));
    new (ptr) T(std::forward<U>(args)...);
    return (T *)ptr;
}

void deallocate(void *ptr, size_t size);

template<typename T>
void destroy(T *x){
    x->~T();
    deallocate(x, sizeof(T));
}

template<class T>
class allocator {
public:
    using value_type = T;

    allocator() noexcept = default;

    template<typename U>
    allocator(const allocator<U>&) noexcept {}

    template<typename U>
    allocator& operator=(const allocator<U>&) noexcept {}

    template<class U>
    struct rebind {
        typedef allocator<U> other;
    };

    value_type* allocate(size_t n) {
        return (value_type *)aoo::allocate(sizeof(T) * n);
    }

    void deallocate(value_type* p, size_t n) noexcept {
        aoo::deallocate(p, sizeof(T) * n);
    }
};

template <class T, class U>
bool operator==(allocator<T> const&, allocator<U> const&) noexcept
{
    return true;
}

template <class T, class U>
bool operator!=(allocator<T> const& x, allocator<U> const& y) noexcept
{
    return !(x == y);
}

#else

inline void * allocate(size_t size){
    return operator new(size);
}

template<class T, class... U>
T * construct(U&&... args){
    return new T(std::forward<U>(args)...);
}

inline void deallocate(void *ptr, size_t size){
    operator delete(ptr);
}

template<typename T>
void destroy(T *x){
    delete x;
}

template<typename T>
using allocator = std::allocator<T>;

#endif

//------------------ memory --------------------//

class memory_list {
public:
    memory_list() = default;
    ~memory_list();
    memory_list(memory_list&& other)
        : list_(other.list_.exchange(nullptr)){}
    memory_list& operator=(memory_list&& other){
        list_.store(other.list_.exchange(nullptr));
        return *this;
    }
    void* allocate(size_t size);

    template<typename T, typename... Args>
    T* construct(Args&&... args){
        auto mem = allocate(sizeof(T));
        return new (mem) T (std::forward<Args>(args)...);
    }

    void deallocate(void* b);

    template<typename T>
    void destroy(T *obj){
        obj->~T();
        deallocate(obj);
    }
private:
    struct block {
        struct {
            block *next;
            size_t size;
        } header;
        char data[1];

        static block * alloc(size_t size);

        static void free(block *mem);

        static block * from_bytes(void *bytes){
            return (block *)((char *)bytes - sizeof(block::header));
        }
    };
    std::atomic<block *> list_{nullptr};
};

//------------------- misc -------------------------//

struct format_deleter {
    void operator() (void *x) const {
        auto f = static_cast<AooFormat *>(x);
        aoo::deallocate(x, f->size);
    }
};

inline AooSize flat_metadata_size(const AooCustomData& data){
    return sizeof(data) + data.size + strlen(data.type) + 1;
}

inline AooSize flat_metadata_maxsize(int32_t size) {
    return sizeof(AooCustomData) + size + kAooTypeNameMaxLen + 1;
}

inline void flat_metadata_copy(const AooCustomData& src, AooCustomData& dst) {
    auto data = (AooByte *)(&dst) + sizeof(AooCustomData);
    memcpy(data, src.data, src.size);

    auto type = (AooChar *)(data + src.size);
    memcpy(type, src.type, strlen(src.type) + 1);

    dst.type = type;
    dst.data = data;
    dst.size = src.size;
}

} // aoo
