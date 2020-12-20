/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef AOO_API
# ifndef AOO_STATIC
#  if defined(_WIN32) // Windows
#   if defined(AOO_BUILD)
#      if defined(DLL_EXPORT)
#        define AOO_API __declspec(dllexport)
#      else
#        define AOO_API
#      endif
#   else
#    define AOO_API __declspec(dllimport)
#   endif
#  elif defined(__GNUC__) && defined(AOO_BUILD) // GNU C
#   define AOO_API __attribute__ ((visibility ("default")))
#  else // Other
#   define AOO_API
#  endif
# else // AOO_STATIC
#  define AOO_API
# endif
#endif

#ifndef AOO_STRICT
#define AOO_STRICT 0
#endif

#define AOO_VERSION_MAJOR 2
#define AOO_VERSION_MINOR 0
#define AOO_VERSION_PATCH 0
#define AOO_VERSION_PRERELEASE 3 // 0: no pre-release

#define AOO_MSG_DOMAIN "/aoo"
#define AOO_MSG_DOMAIN_LEN 4

typedef int32_t aoo_id;
#define AOO_ID_NONE INT32_MIN

// default UDP packet size
#ifndef AOO_PACKETSIZE
 #define AOO_PACKETSIZE 512
#endif

// max. UDP packet size
#define AOO_MAXPACKETSIZE 4096 // ?

#ifndef AOO_SAMPLETYPE
#define AOO_SAMPLETYPE float
#endif

typedef AOO_SAMPLETYPE aoo_sample;

#ifndef USE_AOO_NET
#define USE_AOO_NET 1
#endif

typedef int32_t aoo_type;

enum aoo_types
{
    AOO_TYPE_SOURCE = 0,
    AOO_TYPE_SINK,
#if USE_AOO_NET
    AOO_TYPE_SERVER = 1000,
    AOO_TYPE_CLIENT,
    AOO_TYPE_PEER,
    AOO_TYPE_RELAY
#endif
};

typedef int32_t aoo_bool;

#define AOO_TRUE 1
#define AOO_FALSE 0

typedef int32_t aoo_error;

enum aoo_error_codes
{
    AOO_ERROR_UNSPECIFIED = -1,
    AOO_OK = 0
};

#ifdef __cplusplus
namespace aoo {
    class isource;
    class isink;
}
using aoo_source = aoo::isource;
using aoo_sink = aoo::isink;
#else
typedef struct aoo_source aoo_source;
typedef struct aoo_sink aoo_sink;
#endif

typedef struct aoo_allocator
{
    void* (*alloc)(size_t, void *);        // args: size, context
    void* (*realloc)(void *, size_t);      // args: ptr, old size, new size, context
    void (*free)(void *, size_t, void *);  // args: ptr, size, context
    void* context;                         // context passed to functions
} aoo_allocator;

// log function
typedef void (*aoo_logfunction)(const char *);

// reply function
typedef int32_t (*aoo_sendfn)(
        void *,         // user
        const char *,   // data
        int32_t,        // number of bytes
        const void *,   // address
        int32_t,        // address size
        uint32_t        // flags
);

// base event
typedef struct aoo_event
{
    int32_t type;
} aoo_event;

// event handler
typedef void (*aoo_eventhandler)(
        void *user,          // user
        const aoo_event *e   // event
);

#ifdef __cplusplus
} // extern "C"
#endif
