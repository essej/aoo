/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define AOO_STRUCT struct

#if defined(__GNUC__)
# define AOO_PACK_BEGIN _Pragma("pack(push,8)")
# define AOO_PACK_END _Pragma("pack(pop)")
# elif defined(_MSC_VER)
# define AOO_PACK_BEGIN __pragma(pack(push,8))
# define AOO_PACK_END __pragma(pack(pop))
#else
# define AOO_PACK_BEGIN
# define AOO_PACK_END
#endif

// begin struct packing
AOO_PACK_BEGIN

//--------- global compile time settings ----------------//

#ifndef USE_AOO_NET
# define USE_AOO_NET 1
#endif

#ifndef AOO_STRICT
# define AOO_STRICT 0
#endif

#ifndef AOO_CUSTOM_ALLOCATOR
# define AOO_CUSTOM_ALLOCATOR 0
#endif

// default UDP packet size
#ifndef AOO_PACKET_SIZE
# define AOO_PACKET_SIZE 512
#endif

// max. UDP packet size
#ifndef AOO_MAX_PACKET_SIZE
# define AOO_MAX_PACKET_SIZE 4096
#endif

#ifndef AOO_DEBUG_MEMORY
# define AOO_DEBUG_MEMORY 0
#endif

//--------------- API ---------------//

#ifndef AOO_CALL
# ifdef _WIN32
#  define AOO_CALL __cdecl
# else
#  define AOO_CALL
# endif
#endif

#ifndef AOO_EXPORT
# ifndef AOO_STATIC
#  if defined(_WIN32) // Windows
#   if defined(AOO_BUILD)
#      if defined(DLL_EXPORT)
#        define AOO_EXPORT __declspec(dllexport)
#      else
#        define AOO_EXPORT
#      endif
#   else
#    define AOO_EXPORT __declspec(dllimport)
#   endif
#  elif defined(__GNUC__) && defined(AOO_BUILD) // GNU C
#   define AOO_EXPORT __attribute__ ((visibility ("default")))
#  else // Other
#   define AOO_EXPORT
#  endif
# else // AOO_STATIC
#  define AOO_EXPORT
# endif
#endif

#ifdef __cplusplus
# define AOO_API extern "C" AOO_EXPORT
#else
# define AOO_API AOO_EXPORT
#endif

//--------------- versioning ---------------//

#define kAooVersionMajor 2
#define kAooVersionMinor 0
#define kAooVersionPatch 0
#define kAooVersionTest 3 // 0: no pre-release

//----------- general data types ---------------//

typedef int32_t AooBool;

#define kAooTrue 1
#define kAooFalse 0

typedef char AooChar;

typedef uint8_t AooByte;

typedef int16_t AooInt16;
typedef uint16_t AooUInt16;

typedef int32_t AooInt32;
typedef uint32_t AooUInt32;

typedef int64_t AooInt64;
typedef uint64_t AooUInt64;

typedef size_t AooSize;

typedef intptr_t AooIntPtr;
typedef uintptr_t AooUIntPtr;

//----------- semantic data types -------------//

#ifndef AOO_SAMPLE_TYPE
#define AOO_SAMPLE_TYPE float
#endif

typedef AOO_SAMPLE_TYPE AooSample;

typedef AooInt32 AooId;

#define kAooIdInvalid -1
#define kAooIdMin 0
#define kAooIdMax INT32_MAX

typedef AooUInt32 AooFlag;

typedef AooUInt64 AooNtpTime;

typedef double AooSeconds;

typedef double AooSampleRate;

typedef AooInt32 AooCtl;

//--------------- error codes ---------------//

typedef AooInt32 AooError;

enum AooErrorCodes
{
    kAooErrorUnknown = -1,
    kAooErrorNone = 0,
    kAooErrorNotImplemented,
    kAooErrorBadArgument,
    kAooErrorIdle,
    kAooErrorOutOfMemory
};

#define kAooOk kAooErrorNone

//--------------- AOO events -------------------//

typedef AooInt32 AooEventType;

typedef struct AooEvent
{
    AooEventType type;
} AooEvent;

typedef AooInt32 AooThreadLevel;

#define kAooThreadLevelUnknown 0
#define kAooThreadLevelAudio 1
#define kAooThreadLevelNetwork 2

typedef AooInt32 AooEventMode;

#define kAooEventModeNone 0
#define kAooEventModeCallback 1
#define kAooEventModePoll 2

// event handler
typedef void (AOO_CALL *AooEventHandler)(
        void *user,
        const AooEvent *event,
        AooThreadLevel level
);

//--------------- AOO endpoint -----------------//

typedef AooUInt32 AooAddrSize;

// endpoint
typedef struct AooEndpoint
{
    const void *address;
    AooAddrSize addrlen;
    AooId id;
} AooEndpoint;

enum AooEndpointFlags
{
    kAooEndpointRelay = 0x01
};

typedef AooInt32 (AOO_CALL *AooSendFunc)(
        void *,             // user
        const AooByte *,    // data
        AooInt32,           // number of bytes
        const void *,       // address
        AooAddrSize,        // address size
        AooFlag             // flags
);

//------------ AOO message types ---------------//

typedef AooInt32 AooMsgType;

enum AooMsgTypes
{
    kAooTypeSource = 0,
    kAooTypeSink,
#if USE_AOO_NET
    kAooTypeServer,
    kAooTypeClient,
    kAooTypePeer,
    kAooTypeRelay
#endif
};

//------------ AOO custom data --------------//

typedef struct AooDataView
{
    const AooChar *type;
    const AooByte *data;
    AooSize size;
} AooDataView;

// max. length of data type name strings
#define kAooDataTypeMaxLen 63

// pre-defined data formats

// plain text (UTF-8 encoded)
#define kAooDataTypeText "text"
// JSON (UTF-8 encoded)
#define kAooDataTypeJSON "json"
// XML (UTF-8 encoded)
#define kAooDataTypeXML "xml"
// OSC message (Open Sound Control)
#define kAooDataTypeOSC "osc"
// FUDI (Pure Data)
#define kAooDataTypeFUDI "fudi"

#define kAooDataTypeInvalid ""

// Users can define their own data formats.
// Make sure to use a unique type name!
// HINT: you can use an ASCII encoded UUID.

//--------------- AOO format -------------------//

#define kAooCodecNameMaxLen 16

typedef struct AooFormat
{
    AooChar codec[kAooCodecNameMaxLen];
    AooInt32 size;
    AooInt32 numChannels;
    AooInt32 sampleRate;
    AooInt32 blockSize;
} AooFormat;

#define kAooFormatExtMaxSize 64

typedef struct AooFormatStorage
{
    AooFormat header;
    AooByte data[kAooFormatExtMaxSize];
} AooFormatStorage;

//----------- memory allocation -------------//

typedef struct AooAllocator
{
    // allocate memory; args: size, context
    void* (AOO_CALL *alloc)(AooSize, void *);
    // reallocate memory; args: ptr, old size, new size, context
    void* (AOO_CALL *realloc)(void *, AooSize, AooSize, void *);
    // free memory; args: ptr, size, context
    void (AOO_CALL *free)(void *, AooSize, void *);
    // user context passed to functions
    void* context;
} AooAllocator;

//--------------- logging ---------------//

typedef AooInt32 AooLogLevel;

#define kAooLogLevelNone 0
#define kAooLogLevelError 1
#define kAooLogLevelWarning 2
#define kAooLogLevelVerbose 3
#define kAooLogLevelDebug 4

#ifndef AOO_LOG_LEVEL
#define AOO_LOG_LEVEL kAooLogLevelWarning
#endif

typedef void
#ifndef _MSC_VER
    __attribute__((format(printf, 2, 3 )))
#endif
    (AOO_CALL *AooLogFunc)
        (AooLogLevel level, const AooChar *msg, ...);

//------------------------------------//
// end struct packing
AOO_PACK_END
