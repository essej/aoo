/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief types and constants
 *
 * contains typedefs, constants, enumerations and struct declarations
 */

#pragma once

#include "aoo_config.h"

/* check for C++11
 * NB: MSVC does not correctly set __cplusplus by default! */
#if defined(__cplusplus) && (__cplusplus >= 201103L || ((defined(_MSC_VER) && _MSC_VER >= 1900)))
    #define AOO_HAVE_CXX11 1
#else
    #define AOO_HAVE_CXX11 0
#endif


#if defined(__cplusplus)
# define AOO_INLINE inline
#else
# if (__STDC_VERSION__ >= 199901L)
#  define AOO_INLINE static inline
# else
#  define AOO_INLINE static
# endif
#endif

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
#  else /* Other */
#   define AOO_EXPORT
#  endif
# else /* AOO_STATIC */
#  define AOO_EXPORT
# endif
#endif

#ifdef __cplusplus
# define AOO_API extern "C" AOO_EXPORT
#else
# define AOO_API AOO_EXPORT
#endif

/*---------- struct packing -----------------*/

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

/*------------------- utilities --------------------*/

/** \brief calculate the size of a versioned struct */
#define AOO_STRUCT_SIZE(type, field) \
    (offsetof(type, field) + sizeof(((type *)NULL)->field))

/** \brief initialize a versioned struct */
#define AOO_STRUCT_INIT(ptr, type, field) \
    (ptr)->structSize = AOO_STRUCT_SIZE(type, field)

/** \brief check if a versioned struct has a specific field */
#define AOO_CHECK_FIELD(ptr, type, field) \
    (((ptr)->structSize) >= AOO_STRUCT_SIZE(type, field))

/*-------------------- versioning --------------------*/

/** \brief the AOO major version */
#define kAooVersionMajor 2
/** \brief the AOO minor version */
#define kAooVersionMinor 0
/** \brief the AOO bugfix version */
#define kAooVersionPatch 0
/** \brief the AOO test version (0: stable release) */
#define kAooVersionTest 3

/*-------------------- constants --------------------*/

/** \brief list of available error codes (`AooError`) */
enum
{
    /** unknown/unspecified error */
    kAooErrorUnknown = -1,
    /** no error (= success) */
    kAooErrorNone = 0,
    /** operation/control not implemented */
    kAooErrorNotImplemented,
    /** bad argument for function/method call */
    kAooErrorBadArgument,
    /** AOO source/sink is idle;
     * no need to call `send()` resp. notify the send thread */
    kAooErrorIdle,
    /** operation would overflow */
    kAooErrorOverflow,
    /** out of memory */
    kAooErrorOutOfMemory,
    /** resource not found */
    kAooErrorNotFound,
    /** insufficient buffer size */
    kAooErrorInsufficientBuffer
};

/** \brief alias for success result */
#define kAooOk kAooErrorNone

/** \brief log levels */
enum
{
    /** no logging */
    kAooLogLevelNone = 0,
    /** only errors */
    kAooLogLevelError = 1,
    /** only errors and warnings */
    kAooLogLevelWarning = 2,
    /** errors, warnings and notifications */
    kAooLogLevelVerbose = 3,
    /** errors, warnings, notifications and debug messages */
    kAooLogLevelDebug = 4
};

/** \brief AOO message destination types */
enum
{
    /** AOO source */
    kAooTypeSource = 0,
    /** AOO sink */
    kAooTypeSink,
    /** AOO server */
    kAooTypeServer,
    /** AOO client */
    kAooTypeClient,
    /** AOO peer */
    kAooTypePeer,
    /** relayed message */
    kAooTypeRelay,
    kAooTypeSentinel
};

/** \brief thread levels (`AooThreadLevel`) */
enum
{
    /** unknown thread level */
    kAooThreadLevelUnknown = 0,
    /** audio thread */
    kAooThreadLevelAudio = 1,
    /** network thread(s) */
    kAooThreadLevelNetwork = 2
};

/** \brief event modes (`AooEventMode`) */
enum
{
    /** no events */
    kAooEventModeNone = 0,
    /** use event callback */
    kAooEventModeCallback = 1,
    /** poll for events */
    kAooEventModePoll = 2
};

/** \brief AOO data types */
enum
{
    /** unspecified data type */
    kAooDataUnspecified = -1,
    /** raw or binary data */
    kAooDataRaw = 0,
    kAooDataBinary = 0,
    /** plain text (UTF-8 encoded) */
    kAooDataText,
    /** OSC message (Open Sound Control) */
    kAooDataOSC,
    /** MIDI */
    kAooDataMIDI,
    /** FUDI (Pure Data) */
    kAooDataFUDI,
    /** JSON (UTF-8 encoded) */
    kAooDataJSON,
    /** XML (UTF-8 encoded) */
    kAooDataXML,
    /** start of user specified types */
    kAooDataUser = 1000
};

/** \brief flags for AooSource_addSink / AooSource::addSink */
enum
{
    /** sink should start active */
    kAooSinkActive = 0x01
};

/** \brief flags for AooClient_sendMessage / AooClient::sendMessage */
enum
{
    /** message should be delivered reliable */
    kAooMessageReliable = 0x01
};

/*---------------- OSC address patterns ----------------*/

#define kAooMsgDomain "/aoo"
#define kAooMsgDomainLen 4

#define kAooMsgSource "/src"
#define kAooMsgSourceLen 4

#define kAooMsgSink "/sink"
#define kAooMsgSinkLen 5

#define kAooMsgStart "/start"
#define kAooMsgStartLen 6

#define kAooMsgStop "/stop"
#define kAooMsgStopLen 5

#define kAooMsgData "/data"
#define kAooMsgDataLen 5

#define kAooMsgPing "/ping"
#define kAooMsgPingLen 5

#define kAooMsgPong "/pong"
#define kAooMsgPongLen 5

#define kAooMsgInvite "/invite"
#define kAooMsgInviteLen 7

#define kAooMsgUninvite "/uninvite"
#define kAooMsgUninviteLen 9

#define kAooMsgDecline "/decline"
#define kAooMsgDeclineLen 8

#define kAooMsgServer "/server"
#define kAooMsgServerLen 7

#define kAooMsgClient "/client"
#define kAooMsgClientLen 7

#define kAooMsgPeer "/peer"
#define kAooMsgPeerLen 5

#define kAooMsgRelay "/relay"
#define kAooMsgRelayLen 6

#define kAooMsgMessage "/msg"
#define kAooMsgMessageLen 4

#define kAooMsgAck "/ack"
#define kAooMsgAckLen 4

#define kAooMsgLogin "/login"
#define kAooMsgLoginLen 6

#define kAooMsgQuery "/query"
#define kAooMsgQueryLen 6

#define kAooMsgGroup "/group"
#define kAooMsgGroupLen 6

#define kAooMsgUser "/user"
#define kAooMsgUserLen 5

#define kAooMsgJoin "/join"
#define kAooMsgJoinLen 5

#define kAooMsgLeave "/leave"
#define kAooMsgLeaveLen 6

#define kAooMsgUpdate "/update"
#define kAooMsgUpdateLen 7

#define kAooMsgChanged "/changed"
#define kAooMsgChangedLen 8

#define kAooMsgRequest "/request"
#define kAooMsgRequestLen 8

/** \brief flags for /login message */
enum
{
    kAooLoginServerRelay = 0x01
};

/*------------------- binary messages ---------------------*/

/* domain bit + type (uint8), size bit + cmd (uint8)
 * a) sink ID (uint8), source ID (uint8)
 * b) padding (uint16), sink ID (int32), source ID (int32)
 */

#define kAooBinMsgHeaderSize 4
#define kAooBinMsgLargeHeaderSize 12
#define kAooBinMsgDomainBit 0x80
#define kAooBinMsgSizeBit 0x80

/** \brief commands for 'data' binary message */
enum
{
    kAooBinMsgCmdData = 0
};

/** \brief flags for 'data' binary message */
enum
{
    kAooBinMsgDataSampleRate = 0x01,
    kAooBinMsgDataFrames = 0x02,
    kAooBinMsgDataStreamMessage = 0x04
};

/** \brief commands for 'message' binary message */
enum
{
    kAooBinMsgCmdMessage = 0,
    kAooBinMsgCmdAck = 1
};

/** \brief flags for 'message' binary message */
enum
{
    kAooBinMsgMessageReliable = 0x01,
    kAooBinMsgMessageFrames = 0x02,
    kAooBinMsgMessageTimestamp = 0x04
};

/** \brief commands for 'relay' binary message */
enum
{
    kAooBinMsgCmdRelayIPv4 = 0,
    kAooBinMsgCmdRelayIPv6 = 1
};
