/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_defines.h"

#include <stdint.h>
#include <cstring>
#include <sstream>

/*------------------ alloca -----------------------*/
#ifdef _WIN32
# include <malloc.h> // MSVC or mingw on windows
# ifdef _MSC_VER
#  define alloca _alloca
# endif
#elif defined(__linux__) || defined(__APPLE__)
# include <alloca.h> // linux, mac, mingw, cygwin
#else
# include <stdlib.h> // BSDs for example
#endif

#define DO_LOG(level, msg) do { aoo::Log(level) << msg; } while (false)
#define DO_LOG_ERROR(msg) DO_LOG(kAooLogLevelError, msg)
#define DO_LOG_WARNING(msg) DO_LOG(kAooLogLevelWarning, msg)
#define DO_LOG_VERBOSE(msg) DO_LOG(kAooLogLevelVerbose, msg)
#define DO_LOG_DEBUG(msg) DO_LOG(kAooLogLevelDebug, msg)

#if AOO_LOG_LEVEL >= kAooLogLevelError
 #define LOG_ERROR(x) DO_LOG_ERROR(x)
#else
 #define LOG_ERROR(x)
#endif

#if AOO_LOG_LEVEL >= kAooLogLevelWarning
 #define LOG_WARNING(x) DO_LOG_WARNING(x)
#else
 #define LOG_WARNING(x)
#endif

#if AOO_LOG_LEVEL >= kAooLogLevelVerbose
 #define LOG_VERBOSE(x) DO_LOG_VERBOSE(x)
#else
 #define LOG_VERBOSE(x)
#endif

#if AOO_LOG_LEVEL >= kAooLogLevelDebug
 #define LOG_DEBUG(x) DO_LOG_DEBUG(x)
#else
 #define LOG_DEBUG(x)
#endif

/*------------------ endianess -------------------*/
    // endianess check taken from Pure Data (d_osc.c)
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__FreeBSD_kernel__) \
    || defined(__OpenBSD__)
#include <machine/endian.h>
#endif

#if defined(__linux__) || defined(__CYGWIN__) || defined(__GNU__) || \
    defined(__EMSCRIPTEN__) || \
    defined(ANDROID)
#include <endian.h>
#endif

#ifdef __MINGW32__
#include <sys/param.h>
#endif

#ifdef _MSC_VER
/* _MSVC lacks byte order macros */
 #ifndef LITTLE_ENDIAN
  #define LITTLE_ENDIAN 1234
 #endif
 #ifndef BIG_ENDIAN
  #define BIG_ENDIAN 4321
 #endif
 #define BYTE_ORDER LITTLE_ENDIAN
#endif

#if !defined(BYTE_ORDER)
 #error No byte order defined
#endif

namespace aoo {

void log_message(AooLogLevel level, const std::string& msg);

class Log {
public:
    Log(AooLogLevel level = kAooLogLevelDebug)
        : level_(level){}
    ~Log() {
        stream_ << "\n";
        log_message(level_, stream_.str());
    }
    template<typename T>
    Log& operator<<(T&& t) {
        stream_ << std::forward<T>(t);
        return *this;
    }
private:
    std::ostringstream stream_;
    AooLogLevel level_;
};

template<typename T>
constexpr bool is_pow2(T i){
    return (i & (i - 1)) == 0;
}

template<typename T, typename B>
T from_bytes(const B *b){
    static_assert(sizeof(B) == 1, "from_bytes() expects byte argument");
    union {
        T t;
        AooByte b[sizeof(T)];
    } c;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(c.b, b, sizeof(T));
#else
    for (size_t i = 0; i < sizeof(T); ++i){
        c.b[i] = b[sizeof(T) - i - 1];
    }
#endif
    return c.t;
}

template<typename T, typename B>
T read_bytes(const B *& b){
    auto pos = b;
    b += sizeof(T);
    return aoo::from_bytes<T, B>(pos);
}

template<typename T, typename B>
void to_bytes(T v, B *b){
    static_assert(sizeof(B) == 1, "to_bytes() expects byte argument");
    union {
        T t;
        AooByte b[sizeof(T)];
    } c;
    c.t = v;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(b, c.b, sizeof(T));
#else
    for (size_t i = 0; i < sizeof(T); ++i){
        b[i] = c.b[sizeof(T) - i - 1];
    }
#endif
}

template<typename T, typename B>
void write_bytes(T v, B *& b){
    aoo::to_bytes<T, B>(v, b);
    b += sizeof(T);
}

template<typename T>
T clamp(T in, T low, T high){
    if (in > high) return high;
    else if (in < low) return low;
    else return in;
}

} // aoo
