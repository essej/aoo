/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo_defines.h"

//------------- compile time settings -------------//

#ifndef AOO_CLIP_OUTPUT
# define AOO_CLIP_OUTPUT 0
#endif

#ifndef AOO_DEBUG_DLL
# define AOO_DEBUG_DLL 0
#endif

#ifndef AOO_DEBUG_DATA
# define AOO_DEBUG_DATA 0
#endif

#ifndef AOO_DEBUG_TIMER
# define AOO_DEBUG_TIMER 0
#endif

#ifndef AOO_DEBUG_RESAMPLING
# define AOO_DEBUG_RESAMPLING 0
#endif

#ifndef AOO_DEBUG_AUDIO_BUFFER
# define AOO_DEBUG_AUDIO_BUFFER 0
#endif

#ifndef AOO_DEBUG_JITTER_BUFFER
# define AOO_DEBUG_JITTER_BUFFER 0
#endif

//---------------- default values --------------//

// source buffer size in seconds
#ifndef AOO_SOURCE_BUFFER_SIZE
 #define AOO_SOURCE_BUFFER_SIZE 0.025
#endif

// sink buffer size in seconds
#ifndef AOO_SINK_BUFFER_SIZE
 #define AOO_SINK_BUFFER_SIZE 0.05
#endif

// sink buffer size in seconds
#ifndef AOO_STREAM_METADATA_SIZE
 #define AOO_STREAM_METADATA_SIZE 256
#endif

// use binary data message format
#ifndef AOO_BINARY_DATA_MSG
 #define AOO_BINARY_DATA_MSG 1
#endif

// enable/disable dynamic resampling
#ifndef AOO_DYNAMIC_RESAMPLING
 #define AOO_DYNAMIC_RESAMPLING 1
#endif

// time DLL filter bandwidth
#ifndef AOO_DLL_BANDWIDTH
 #define AOO_DLL_BANDWIDTH 0.012
#endif

// enable/disable timer check to
// catch timing issues (e.g. blocking audio thread)
#ifndef AOO_TIMER_CHECK
 #define AOO_TIMER_CHECK 1
#endif

// the tolerance for deviations from the nominal
// block period in (fractional) blocks
#ifndef AOO_TIMER_TOLERANCE
 #define AOO_TIMER_TOLERANCE 0.25
#endif

// ping interval (sink to source) in seconds
#ifndef AOO_PING_INTERVAL
 #define AOO_PING_INTERVAL 1.0
#endif

// resend buffer size in seconds
#ifndef AOO_RESEND_BUFFER_SIZE
 #define AOO_RESEND_BUFFER_SIZE 1.0
#endif

// send redundancy
#ifndef AOO_SEND_REDUNDANCY
 #define AOO_SEND_REDUNDANCY 1
#endif

// enable/disable packet resending
#ifndef AOO_RESEND_DATA
 #define AOO_RESEND_DATA 1
#endif

// interval between resend attempts in seconds
#ifndef AOO_RESEND_INTERVAL
 #define AOO_RESEND_INTERVAL 0.01
#endif

// max. number of frames to request per call
#ifndef AOO_RESEND_LIMIT
 #define AOO_RESEND_LIMIT 16
#endif

// max. number of seconds to wait before removing inactive source
#ifndef AOO_SOURCE_TIMEOUT
 #define AOO_SOURCE_TIMEOUT 10.0
#endif

//---------------- public OSC interface --------//

// OSC address patterns
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
#define kAooMsgInvite "/invite"
#define kAooMsgInviteLen 7
#define kAooMsgUninvite "/uninvite"
#define kAooMsgUninviteLen 9

//------------------- binary message ---------------------//

// domain (int32), type (int16), msg_type (int16), id (int32)
#define kAooBinMsgHeaderSize 12

#define kAooBinMsgDomain "\0aoo"
#define kAooBinMsgDomainSize 4

#define kAooBinMsgCmdData 0

enum AooBinMsgDataFlags
{
    kAooBinMsgDataSampleRate = 0x01,
    kAooBinMsgDataFrames = 0x02
};

//------------------ library functions --------------------//

// initialize AoO library (call before creating any objects!)
AOO_API void AOO_CALL aoo_initialize(void);

// initialize with log function and/or custom allocator;
// set to NULL if not needed.
AOO_API void AOO_CALL aoo_initializeEx(
        AooLogFunc log, const AooAllocator *alloc);

// terminate AoO library (call before program exit)
AOO_API void AOO_CALL aoo_terminate(void);

// get the AoO version numbers
AOO_API void AOO_CALL aoo_getVersion(
        AooInt32 *major, AooInt32 *minor, AooInt32 *patch, AooInt32 *test);

// get the AoO version string: <major>[.<minor>][.<patch>][-test<test>]
AOO_API const AooChar * AOO_CALL aoo_getVersionString(void);

// get a textual description for an error code
AOO_API const AooChar * AOO_CALL aoo_strerror(AooError e);

// get the current NTP time stamp
AOO_API AooNtpTime AOO_CALL aoo_getCurrentNtpTime(void);

// convert NTP time stamp to seconds
AOO_API AooSeconds AOO_CALL aoo_ntpTimeToSeconds(AooNtpTime t);

// convert seconds to NTP time stamp
AOO_API AooNtpTime AOO_CALL aoo_ntpTimeFromSeconds(AooSeconds s);

// get time difference in seconds between two NTP time stamps
AOO_API AooSeconds AOO_CALL aoo_ntpTimeDuration(AooNtpTime t1, AooNtpTime t2);

// get the Aoo type and ID from an AoO OSC message, e.g. /aoo/src/<id>/data
// 'offset' gives the position of the address pattern component after the ID.
AOO_API AooError AOO_CALL aoo_parsePattern(
        const AooByte *msg, AooInt32 size, AooMsgType *type, AooId *id, AooInt32 *offset);

