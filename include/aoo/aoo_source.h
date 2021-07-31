#pragma once

#include "aoo.h"
#include "aoo_events.h"
#include "aoo_controls.h"

typedef struct AooSource AooSource;

// create a new AoO source instance
AOO_API AooSource * AooSource_new(
        AooId id, AooFlag flags, AooError *err);

// destroy the AoO source instance
AOO_API void AooSource_free(AooSource *source);

// setup the source - needs to be synchronized with other method calls!
AOO_API AooError AOO_CALL AooSource_setup(
        AooSource *source, AooSampleRate sampleRate,
        AooInt32 blockSize, AooInt32 numChannels);

// handle messages from sinks (threadsafe, called from a network thread)
AOO_API AooError AOO_CALL AooSource_handleMessage(
        AooSource *source, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen);

// update and send outgoing messages (threadsafe, called from the network thread)
AOO_API AooError AOO_CALL AooSource_send(
        AooSource *source, AooSendFunc fn, void *user);

// process audio blocks (threadsafe, called from the audio thread)
// data:        array of channel data (non-interleaved)
// numSamples:  number of samples per channel
// t:           current NTP timestamp (see aoo_osctime_get)
AOO_API AooError AOO_CALL AooSource_process(
        AooSource *source, const AooSample **data, AooInt32 numSamples, AooNtpTime t);

// set event handler callback + mode
AOO_API AooError AOO_CALL AooSource_setEventHandler(
        AooSource *source, AooEventHandler fn, void *user, AooEventMode mode);

// check for pending events (always thread safe)
AOO_API AooBool AOO_CALL AooSource_eventsAvailable(AooSource *source);

// poll events (threadsafe, but not reentrant).
// will call the event handler function one or more times.
// NOTE: the event handler must have been registered with kAooEventModePoll.
AOO_API AooError AOO_CALL AooSource_pollEvents(AooSource *source);

// control interface (always threadsafe)
AOO_API AooError AOO_CALL AooSource_control(
        AooSource *source, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

// ------------------------------------------------------------
// type-safe convenience functions for frequently used controls

static inline AooError AooSource_startStream(
        AooSource *source, const AooCustomData *metadata)
{
    return AooSource_control(source, kAooCtlStartStream, 0,
                             (void *)metadata, metadata ? sizeof(AooCustomData) : 0);
}

static inline AooError AooSource_stopStream(AooSource *source)
{
    return AooSource_control(source, kAooCtlStopStream, 0, 0, 0);
}

static inline AooError AooSource_addSink(
        AooSource *source, const AooEndpoint *sink, AooFlag flags)
{
    return AooSource_control(source, kAooCtlAddSink, (AooIntPtr)sink, AOO_ARG(flags));
}

static inline AooError AooSource_removeSink(
        AooSource *source, const AooEndpoint *sink)
{
    return AooSource_control(source, kAooCtlRemoveSink, (AooIntPtr)sink, 0, 0);
}

static inline AooError AooSource_removeAllSinks(AooSource *source)
{
    return AooSource_control(source, kAooCtlRemoveSink, 0, 0, 0);
}

static inline AooError AooSource_setId(AooSource *source, AooId id)
{
    return AooSource_control(source, kAooCtlSetId, 0, AOO_ARG(id));
}

static inline AooError AooSource_getId(AooSource *source, AooId *id)
{
    return AooSource_control(source, kAooCtlGetId, 0, AOO_ARG(*id));
}

static inline AooError AooSource_setFormat(AooSource *source, AooFormat *f)
{
    return AooSource_control(source, kAooCtlSetFormat, 0, AOO_ARG(*f));
}

static inline AooError AooSource_getFormat(AooSource *source, AooFormatStorage *f)
{
    return AooSource_control(source, kAooCtlGetFormat, 0, AOO_ARG(*f));
}

static inline AooError AooSource_setBufferSize(AooSource *source, AooSeconds s)
{
    return AooSource_control(source, kAooCtlSetBufferSize, 0, AOO_ARG(s));
}

static inline AooError AooSource_getBufferSize(AooSource *source, AooSeconds *s)
{
    return AooSource_control(source, kAooCtlGetBufferSize, 0, AOO_ARG(*s));
}

static inline AooError AooSource_setTimerCheck(AooSource *source, AooBool b)
{
    return AooSource_control(source, kAooCtlSetTimerCheck, 0, AOO_ARG(b));
}

static inline AooError AooSource_getTimerCheck(AooSource *source, AooBool *b)
{
    return AooSource_control(source, kAooCtlGetTimerCheck, 0, AOO_ARG(*b));
}

static inline AooError AooSource_setDynamicResampling(AooSource *source, AooBool b)
{
    return AooSource_control(source, kAooCtlSetDynamicResampling, 0, AOO_ARG(b));
}

static inline AooError AooSource_getDynamicResampling(AooSource *source, AooBool *b)
{
    return AooSource_control(source, kAooCtlGetDynamicResampling, 0, AOO_ARG(*b));
}

static inline AooError AooSource_getRealSampleRate(AooSource *source, AooSample *sr)
{
    return AooSource_control(source, kAooCtlGetRealSampleRate, 0, AOO_ARG(*sr));
}

static inline AooError AooSource_setDllBandwidth(AooSource *source, double q)
{
    return AooSource_control(source, kAooCtlSetDllBandwidth, 0, AOO_ARG(q));
}

static inline AooError AooSource_getDllBandwidth(AooSource *source, double *q)
{
    return AooSource_control(source, kAooCtlGetDllBandwidth, 0, AOO_ARG(*q));
}

static inline AooError AooSource_setPacketSize(AooSource *source, AooInt32 n)
{
    return AooSource_control(source, kAooCtlSetPacketSize, 0, AOO_ARG(n));
}

static inline AooError AooSource_getPacketSize(AooSource *source, AooInt32 *n)
{
    return AooSource_control(source, kAooCtlGetPacketSize, 0, AOO_ARG(*n));
}

static inline AooError AooSource_setPingInterval(AooSource *source, AooSeconds s)
{
    return AooSource_control(source, kAooCtlSetPingInterval, 0, AOO_ARG(s));
}

static inline AooError AooSource_getPingInterval(AooSource *source, AooSeconds *s)
{
    return AooSource_control(source, kAooCtlGetPingInterval, 0, AOO_ARG(*s));
}

static inline AooError AooSource_setResendBufferSize(AooSource *source, AooSeconds s)
{
    return AooSource_control(source, kAooCtlSetResendBufferSize, 0, AOO_ARG(s));
}

static inline AooError AooSource_getResendBufferSize(AooSource *source, AooSeconds *s)
{
    return AooSource_control(source, kAooCtlGetResendBufferSize, 0, AOO_ARG(*s));
}

static inline AooError AooSource_setRedundancy(AooSource *source, AooInt32 n)
{
    return AooSource_control(source, kAooCtlSetRedundancy, 0, AOO_ARG(n));
}

static inline AooError AooSource_GetRedundancy(AooSource *source, AooInt32 *n)
{
    return AooSource_control(source, kAooCtlGetRedundancy, 0, AOO_ARG(*n));
}

static inline AooError AooSource_setBinaryDataMsg(AooSource *source, AooBool b)
{
    return AooSource_control(source, kAooCtlSetBinaryDataMsg, 0, AOO_ARG(b));
}

static inline AooError AooSource_getBinaryDataMsg(AooSource *source, AooBool *b)
{
    return AooSource_control(source, kAooCtlGetBinaryDataMsg, 0, AOO_ARG(*b));
}

static inline AooError AooSource_setStreamMetadataSize(AooSource *source, AooInt32 size)
{
    return AooSource_control(source, kAooCtlSetStreamMetadataSize, 0, AOO_ARG(size));
}

static inline AooError AooSource_getStreamMetadataSize(AooSource *source, AooInt32 *size)
{
    return AooSource_control(source, kAooCtlGetStreamMetadataSize, 0, AOO_ARG(*size));
}

static inline AooError AooSource_setSinkChannelOnset(
        AooSource *source, const AooEndpoint *sink, AooInt32 onset)
{
    return AooSource_control(source, kAooCtlSetChannelOnset, (AooIntPtr)sink, AOO_ARG(onset));
}

static inline AooError AooSource_getSinkChannelOnset(
        AooSource *source, const AooEndpoint *sink, AooInt32 *onset)
{
    return AooSource_control(source, kAooCtlGetChannelOnset, (AooIntPtr)sink, AOO_ARG(*onset));
}
