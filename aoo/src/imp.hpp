#pragma once

#include "aoo/aoo.h"

#include <stdint.h>
#include <utility>
#include <memory>

#if !AOO_USE_ALLOCATOR
#include <stdlib.h>
#endif

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


#if AOO_USE_ALLOCATOR

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

#else

inline void * allocate(size_t size){
    return ::malloc(size);
}

template<class T, class... U>
T * construct(U&&... args){
    return new T(std::forward<U>(args)...);
}

inline void deallocate(void *ptr, size_t size){
    ::free(ptr);
}

template<typename T>
void destroy(T *x){
    delete x;
}

template<typename T>
using allocator = std::allocator<T>;

#endif

template<template<class, class> class C, class T>
using container = C<T, allocator<T>>;

} // aoo
