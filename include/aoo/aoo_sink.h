#pragma once

#include "aoo.h"
#include "aoo_events.h"
#include "aoo_controls.h"

typedef struct AooSink AooSink;

// create a new AOO sink instance
AOO_API AooSink * AOO_CALL AooSink_new(
        AooId id, AooFlag flags, AooError *err);

// destroy the AOO sink instance
AOO_API void AOO_CALL AooSink_free(AooSink *sink);

// setup the sink - needs to be synchronized with other method calls!
AOO_API AooError AOO_CALL AooSink_setup(
        AooSink *sink, AooSampleRate sampleRate,
        AooInt32 blockSize, AooInt32 numChannels);

// handle messages from sources (threadsafe, called from a network thread)
AOO_API AooError AOO_CALL AooSink_handle_message(
        AooSink *sink, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen);

// send outgoing messages (threadsafe, called from a network thread)
AOO_API AooError AOO_CALL AooSink_send(
        AooSink *sink, AooSendFunc fn, void *user);

// process audio (threadsafe, but not reentrant)
// data:        array of channel data (non-interleaved)
// nsamples:    number of samples per channel
// t:           current NTP timestamp (see aoo_getCurrentNtpTime)
AOO_API AooError AOO_CALL AooSink_process(
        AooSink *sink, AooSample **data, AooInt32 numSamples, AooNtpTime t);

// set event handler callback + mode
AOO_API AooError AOO_CALL AooSink_setEventHandler(
        AooSink *sink, AooEventHandler fn, void *user, AooEventMode mode);

// check for pending events (always thread safe)
AOO_API AooBool AOO_CALL AooSink_eventsAvailable(AooSink *sink);

// poll events (threadsafe, but not reentrant).
// will call the event handler function one or more times.
// NOTE: the event handler must have been registered with kAooEventModePoll.
AOO_API AooError AOO_CALL AooSink_pollEvents(AooSink *sink);

// uninvite source
// ---
// This will continuously send invitation requests to the source
// The source can either accept the invitation request and start a
// stream or it can ignore it, upon which the sink will eventually
// receive an AooEventInviteTimeout event.
// The invitation can contain additional metadata which the source
// can interpret before accepting the invitation.
// If you call this function while you are already receiving a stream,
// it will force a new stream. For example, you might want to request
// different format parameters or even ask for different musical content.
AOO_API AooError AOO_CALL AooSink_inviteSource(
        AooSink *sink, const AooEndpoint *source, const AooCustomData *metadata);

// uninvite source
// ---
// This will continuously send uninvitation requests to the source.
// The source can either accept the uninvitation request and stop the
// stream, or it can ignore and continue sending, upon which the sink
// will eventually receive an AooEventUninviteTimeout event.
AOO_API AooError AOO_CALL AooSink_uninviteSource(
        AooSink *sink, const AooEndpoint *source);

// uninvite all sources
AOO_API AooError AOO_CALL AooSink_uninviteAll(AooSink *sink);

// control interface (always threadsafe)
AOO_API AooError AOO_CALL AooSink_control(
        AooSink *sink, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

// codec control interface (always threadsafe)
// ---
// The available codec controls should be listed in the respective header file.
AOO_API AooError AOO_CALL AooSink_codecControl(
        AooSink *sink, AooCtl ctl, AooIntPtr index, void *data, AooSize size);


// ------------------------------------------------------------
// type-safe convenience functions for frequently used controls

static inline AooError AooSink_setId(AooSink *sink, AooId id)
{
    return AooSink_control(sink, kAooCtlSetId, 0, AOO_ARG(id));
}

static inline AooError AooSink_getId(AooSink *sink, AooId *id)
{
    return AooSink_control(sink, kAooCtlGetId, 0, AOO_ARG(*id));
}

static inline AooError AooSink_reset(AooSink *sink)
{
    return AooSink_control(sink, kAooCtlReset, 0, 0, 0);
}

static inline AooError AooSink_setBufferSize(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetBufferSize, 0, AOO_ARG(s));
}

static inline AooError AooSink_getBufferSize(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetBufferSize, 0, AOO_ARG(*s));
}

static inline AooError AooSink_setXRunDetection(AooSink *sink, AooBool b)
{
    return AooSink_control(sink, kAooCtlSetXRunDetection, 0, AOO_ARG(b));
}

static inline AooError AooSink_getXRunDetection(AooSink *sink, AooBool *b)
{
    return AooSink_control(sink, kAooCtlGetXRunDetection, 0, AOO_ARG(*b));
}

static inline AooError AooSink_setDynamicResampling(AooSink *sink, AooBool b)
{
    return AooSink_control(sink, kAooCtlSetDynamicResampling, 0, AOO_ARG(b));
}

static inline AooError AooSink_getDynamicResampling(AooSink *sink, AooBool *b)
{
    return AooSink_control(sink, kAooCtlGetDynamicResampling, 0, AOO_ARG(*b));
}

static inline AooError AooSink_getRealSampleRate(AooSink *sink, AooSampleRate *sr)
{
    return AooSink_control(sink, kAooCtlGetRealSampleRate, 0, AOO_ARG(*sr));
}

static inline AooError AooSink_setDllBandwidth(AooSink *sink, double q)
{
    return AooSink_control(sink, kAooCtlSetDllBandwidth, 0, AOO_ARG(q));
}

static inline AooError AooSink_getDllBandwidth(AooSink *sink, double *q)
{
    return AooSink_control(sink, kAooCtlGetDllBandwidth, 0, AOO_ARG(*q));
}

static inline AooError AooSink_setPacketSize(AooSink *sink, AooInt32 n)
{
    return AooSink_control(sink, kAooCtlSetPacketSize, 0, AOO_ARG(n));
}

static inline AooError AooSink_getPacketSize(AooSink *sink, AooInt32 *n)
{
    return AooSink_control(sink, kAooCtlGetPacketSize, 0, AOO_ARG(*n));
}

static inline AooError AooSink_setResendData(AooSink *sink, AooBool b)
{
    return AooSink_control(sink, kAooCtlSetResendData, 0, AOO_ARG(b));
}

static inline AooError AooSink_getResendData(AooSink *sink, AooBool *b)
{
    return AooSink_control(sink, kAooCtlGetResendData, 0, AOO_ARG(*b));
}

static inline AooError AooSink_setResendInterval(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetResendInterval, 0, AOO_ARG(s));
}

static inline AooError AooSink_getResendInterval(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetResendInterval, 0, AOO_ARG(*s));
}

static inline AooError AooSink_setResendLimit(AooSink *sink, AooInt32 n)
{
    return AooSink_control(sink, kAooCtlSetResendLimit, 0, AOO_ARG(n));
}

static inline AooError AooSink_getResendLimit(AooSink *sink, AooInt32 *n)
{
    return AooSink_control(sink, kAooCtlGetResendLimit, 0, AOO_ARG(*n));
}

static inline AooError AooSink_setSourceTimeout(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetSourceTimeout, 0, AOO_ARG(s));
}

static inline AooError AooSink_getSourceTimeout(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetSourceTimeout, 0, AOO_ARG(*s));
}

static inline AooError AooSink_setInviteTimeout(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetInviteTimeout, 0, AOO_ARG(s));
}

static inline AooError AooSink_getInviteTimeout(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetInviteTimeout, 0, AOO_ARG(*s));
}

static inline AooError AooSink_resetSource(AooSink *sink, const AooEndpoint *source)
{
    return AooSink_control(sink, kAooCtlReset, (AooIntPtr)source, 0, 0);
}

static inline AooError AooSink_getSourceFormat(
        AooSink *sink, const AooEndpoint *source, AooFormatStorage *f)
{
    return AooSink_control(sink, kAooCtlGetFormat, (AooIntPtr)source, AOO_ARG(*f));
}

static inline AooError AooSink_getBufferFillRatio(
        AooSink *sink, const AooEndpoint *source, double *r)
{
    return AooSink_control(sink, kAooCtlGetBufferFillRatio, (AooIntPtr)source, AOO_ARG(*r));
}
