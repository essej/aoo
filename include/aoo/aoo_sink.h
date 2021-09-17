/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C interface for AOO sink
 */

#pragma once

#include "aoo.h"
#include "aoo_events.h"
#include "aoo_controls.h"

typedef struct AooSink AooSink;

/** \brief create a new AOO sink instance
 *
 * \param id the ID
 * \param flags optional flags
 * \param[out] err error code on failure
 * \return new AooSink instance on success; `NULL` on failure
 */
AOO_API AooSink * AOO_CALL AooSink_new(
        AooId id, AooFlag flags, AooError *err);

/** \brief destroy the AOO sink instance */
AOO_API void AOO_CALL AooSink_free(AooSink *sink);

/** \brief setup AOO sink
 *
 * \warning Not threadsafe - needs to be synchronized with other method calls!
 *
 * \param sink the AOO sink
 * \param sampleRate the sample rate
 * \param blockSize the max. blocksize
 * \param numChannels the max. number of channels
 */
AOO_API AooError AOO_CALL AooSink_setup(
        AooSink *sink, AooSampleRate sampleRate,
        AooInt32 blockSize, AooInt32 numChannels);

/** \brief handle source messages
 *
 * \note Threadsafe; call on the network thread
 *
 * \param sink the AOO sink
 * \param data the message data
 * \param size the message size in bytes
 * \param address the remote socket address
 * \param addrlen the socket address length
 */
AOO_API AooError AOO_CALL AooSink_handleMessage(
        AooSink *sink, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen);

/** \brief send outgoing messages
 *
 * \note Threadsafe; call on the network thread
 *
 * \param sink the AOO sink
 * \param fn the send function
 * \param user the user data (passed to the send function)
 */
AOO_API AooError AOO_CALL AooSink_send(
        AooSink *sink, AooSendFunc fn, void *user);

/** \brief process audio
 *
 * \note Threadsafe and RT-safe; call on the audio thread
 *
 * \param sink the AOO sink
 * \param data an array of audio channels; the number of
 *        channels must match the number in #AooSource_setup.
 * \param numSamples the number of samples per channel
 * \param t current NTP time; \see aoo_getCurrentNtpTime
 */
AOO_API AooError AOO_CALL AooSink_process(
        AooSink *sink, AooSample **data, AooInt32 numSamples, AooNtpTime t);

/** \brief set event handler function and event handling mode
 *
 * \warning Not threadsafe - only call in the beginning! */
AOO_API AooError AOO_CALL AooSink_setEventHandler(
        AooSink *sink, AooEventHandler fn, void *user, AooEventMode mode);

/** \brief check for pending events
 *
 * \note Threadsafe and RT-safe */
AOO_API AooBool AOO_CALL AooSink_eventsAvailable(AooSink *sink);

/** \brief poll events
 *
 * \note Threadsafe and RT-safe, but not reentrant.
 *
 * This function will call the registered event handler one or more times.
 * \attention The event handler must have been registered with #kAooEventModePoll.
 */
AOO_API AooError AOO_CALL AooSink_pollEvents(AooSink *sink);

/** \brief invite source
 *
 * This will continuously send invitation requests to the source
 * The source can either accept the invitation request and start a
 * stream or it can ignore it, upon which the sink will eventually
 * receive an AooEventInviteTimeout event.
 * If you call this function while you are already receiving a stream,
 * it will force a new stream. For example, you might want to request
 * different format parameters or even ask for different musical content.
 *
 * \param sink the AOO sink
 * \param source the AOO source to be invited
 * \param metadata optional metadata that the source can interpret
 *        before accepting the invitation
 */
AOO_API AooError AOO_CALL AooSink_inviteSource(
        AooSink *sink, const AooEndpoint *source, const AooDataView *metadata);

/** \brief uninvite source
 *
 * This will continuously send uninvitation requests to the source.
 * The source can either accept the uninvitation request and stop the
 * stream, or it can ignore and continue sending, upon which the sink
 * will eventually receive an #kAooEventUninviteTimeout event.
 *
 * \param sink the AOO sink
 * \param source the AOO source to be uninvited
 */
AOO_API AooError AOO_CALL AooSink_uninviteSource(
        AooSink *sink, const AooEndpoint *source);

/** \brief uninvite all sources */
AOO_API AooError AOO_CALL AooSink_uninviteAll(AooSink *sink);

/** \brief control interface
 *
 * used internally by helper functions for specific controls */
AOO_API AooError AOO_CALL AooSink_control(
        AooSink *sink, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

/** \brief codec control interface
 *
 * used internally by helper functions for specific codec controls */
AOO_API AooError AOO_CALL AooSink_codecControl(
        AooSink *sink, AooCtl ctl, AooIntPtr index, void *data, AooSize size);


/* ------------------------------------------------------------ */
/* type-safe convenience functions for frequently used controls */
/* ------------------------------------------------------------ */

/** \brief Set AOO source ID
 * \param id The new ID
 */
AOO_INLINE AooError AooSink_setId(AooSink *sink, AooId id)
{
    return AooSink_control(sink, kAooCtlSetId, 0, AOO_ARG(id));
}

/** \brief Get AOO source ID
 * \param[out] id The current ID
 */
AOO_INLINE AooError AooSink_getId(AooSink *sink, AooId *id)
{
    return AooSink_control(sink, kAooCtlGetId, 0, AOO_ARG(*id));
}

/** \brief Reset the sink */
AOO_INLINE AooError AooSink_reset(AooSink *sink)
{
    return AooSink_control(sink, kAooCtlReset, 0, 0, 0);
}

/** \brief Set the buffer size in seconds (in seconds)
 *
 * This is the size of the ring buffer between the audio and network thread.
 * For the sink, a larger buffer size helps to deal with network jitter,
 * packet reordering and packet loss.
 * For local networks small buffersizes between 10-50ms should work;
 * for unreliable/unpredictable networks you might need to increase it
 * significantly if you want to avoid dropouts.
 */
AOO_INLINE AooError AooSink_setBufferSize(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetBufferSize, 0, AOO_ARG(s));
}

/** \brief Get the current buffer size (in seconds) */
AOO_INLINE AooError AooSink_getBufferSize(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetBufferSize, 0, AOO_ARG(*s));
}

/** \brief Enable/disable xrun detection
 *
 * xrun detection helps to catch timing problems, e.g. when the host accidentally
 * blocks the audio callback, which would confuse the time DLL filter.
 * Also, timing gaps are handled by dropping blocks at the sink.
 * NOTE: only takes effect on source/sink setup!
 */
AOO_INLINE AooError AooSink_setXRunDetection(AooSink *sink, AooBool b)
{
    return AooSink_control(sink, kAooCtlSetXRunDetection, 0, AOO_ARG(b));
}

/** \brief Check if xrun detection is enabled */
AOO_INLINE AooError AooSink_getXRunDetection(AooSink *sink, AooBool *b)
{
    return AooSink_control(sink, kAooCtlGetXRunDetection, 0, AOO_ARG(*b));
}

/** \brief Enable/disable dynamic resampling
 *
 * Dynamic resampling attempts to mitigate CPU clock drift between
 * two different machines.
 * A DLL filter estimates the effective sample rate on both sides
 * and the audio data is resampled accordingly. The behavior can be
 * fine-tuned with AooSource_setDllBandWidth().
 * See the paper "Using a DLL to filter time" by Fons Adriaensen.
 */
AOO_INLINE AooError AooSink_setDynamicResampling(AooSink *sink, AooBool b)
{
    return AooSink_control(sink, kAooCtlSetDynamicResampling, 0, AOO_ARG(b));
}

/** \brief Check if dynamic resampling is enabled. */
AOO_INLINE AooError AooSink_getDynamicResampling(AooSink *sink, AooBool *b)
{
    return AooSink_control(sink, kAooCtlGetDynamicResampling, 0, AOO_ARG(*b));
}

/** \brief Get the "real" samplerate as measured by the DLL filter */
AOO_INLINE AooError AooSink_getRealSampleRate(AooSink *sink, AooSampleRate *sr)
{
    return AooSink_control(sink, kAooCtlGetRealSampleRate, 0, AOO_ARG(*sr));
}

/** \brief Set DLL filter bandwidth
 *
 * Used for dynamic resampling, see AooSource_setDynamicResampling().
 */
AOO_INLINE AooError AooSink_setDllBandwidth(AooSink *sink, double q)
{
    return AooSink_control(sink, kAooCtlSetDllBandwidth, 0, AOO_ARG(q));
}

/** \brief get DLL filter bandwidth */
AOO_INLINE AooError AooSink_getDllBandwidth(AooSink *sink, double *q)
{
    return AooSink_control(sink, kAooCtlGetDllBandwidth, 0, AOO_ARG(*q));
}

/** \brief Set the max. UDP packet size in bytes
 *
 * The default value should be fine for most networks (including the internet),
 * but you might want to increase this value for local networks because larger
 * packet sizes have less overhead. If a audio block exceeds the max. UDP packet size,
 * it will be automatically broken up into several "frames" and then reassembled in the sink.
 */
AOO_INLINE AooError AooSink_setPacketSize(AooSink *sink, AooInt32 n)
{
    return AooSink_control(sink, kAooCtlSetPacketSize, 0, AOO_ARG(n));
}

/** \brief Get the max. UDP packet size */
AOO_INLINE AooError AooSink_getPacketSize(AooSink *sink, AooInt32 *n)
{
    return AooSink_control(sink, kAooCtlGetPacketSize, 0, AOO_ARG(*n));
}

/** \brief Enable/disable data resending */
AOO_INLINE AooError AooSink_setResendData(AooSink *sink, AooBool b)
{
    return AooSink_control(sink, kAooCtlSetResendData, 0, AOO_ARG(b));
}

/** \brief Check if data resending is enabled */
AOO_INLINE AooError AooSink_getResendData(AooSink *sink, AooBool *b)
{
    return AooSink_control(sink, kAooCtlGetResendData, 0, AOO_ARG(*b));
}

/** \brief Set resend interval (in seconds)
 *
 * This is the interval between individual resend attempts for a specific frame.
 * Since there is always a certain roundtrip delay between source and sink,
 * it makes sense to wait between resend attempts to not spam the network
 * with redundant /resend messages.
 */
AOO_INLINE AooError AooSink_setResendInterval(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetResendInterval, 0, AOO_ARG(s));
}

/** \brief Get resend interval (in seconds) */
AOO_INLINE AooError AooSink_getResendInterval(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetResendInterval, 0, AOO_ARG(*s));
}

/** \brief Set the frame resend limit
 *
 * This is the max. number of frames to request in a single process call.
 */
AOO_INLINE AooError AooSink_setResendLimit(AooSink *sink, AooInt32 n)
{
    return AooSink_control(sink, kAooCtlSetResendLimit, 0, AOO_ARG(n));
}

/** \brief Get the frame resend limit */
AOO_INLINE AooError AooSink_getResendLimit(AooSink *sink, AooInt32 *n)
{
    return AooSink_control(sink, kAooCtlGetResendLimit, 0, AOO_ARG(*n));
}

/** \brief Set source timeout (in seconds)
 *
 * The time to wait before removing inactive sources
 */
AOO_INLINE AooError AooSink_setSourceTimeout(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetSourceTimeout, 0, AOO_ARG(s));
}

/** \brief Get source timeout (in seconds) */
AOO_INLINE AooError AooSink_getSourceTimeout(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetSourceTimeout, 0, AOO_ARG(*s));
}

/** \brief Set (un)invite timeout (in seconds)
 *
 * Time to wait before stopping the (un)invite process.
 */
AOO_INLINE AooError AooSink_setInviteTimeout(AooSink *sink, AooSeconds s)
{
    return AooSink_control(sink, kAooCtlSetInviteTimeout, 0, AOO_ARG(s));
}

/** \brief Get (un)invite timeout (in seconds) */
AOO_INLINE AooError AooSink_getInviteTimeout(AooSink *sink, AooSeconds *s)
{
    return AooSink_control(sink, kAooCtlGetInviteTimeout, 0, AOO_ARG(*s));
}

/** \brief Reset a specific source */
AOO_INLINE AooError AooSink_resetSource(AooSink *sink, const AooEndpoint *source)
{
    return AooSink_control(sink, kAooCtlReset, (AooIntPtr)source, 0, 0);
}

/** \brief Get the source stream format
 *
 * \param source The source endpoint.
 * \param[out] format Pointer to an instance of `AooFormatStorage` or a similar
 * struct that is large enough to hold any codec format.
 * The `size` member in the format header should contain the storage size;
 * on success it is updated to the actual format size
 */
AOO_INLINE AooError AooSink_getSourceFormat(
        AooSink *sink, const AooEndpoint *source, AooFormat *format)
{
    return AooSink_control(sink, kAooCtlGetFormat, (AooIntPtr)source, AOO_ARG(*format));
}


/** \brief Get the current buffer fill ratio
 *
 * \param[out] ratio The current fill ratio (0.0: empty, 1.0: full)
 */
AOO_INLINE AooError AooSink_getBufferFillRatio(
        AooSink *sink, const AooEndpoint *source, double *ratio)
{
    return AooSink_control(sink, kAooCtlGetBufferFillRatio, (AooIntPtr)source, AOO_ARG(*ratio));
}
