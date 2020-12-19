#pragma once

#include <stdint.h>

namespace aoo {

uint32_t make_version();

bool check_version(uint32_t version);

char * copy_string(const char *s);

void free_string(char *s);

void * copy_sockaddr(const void *sa, int32_t len);

void free_sockaddr(void *sa, int32_t len);

namespace net {

int32_t parse_pattern(const char *msg, int32_t n, int32_t *type);

} // net

/*///////////////////// allocator ////////////////////*/

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
bool
    operator==(allocator<T> const&, allocator<U> const&) noexcept
{
    return true;
}

template <class T, class U>
bool
    operator!=(allocator<T> const& x, allocator<U> const& y) noexcept
{
    return !(x == y);
}

} // aoo
