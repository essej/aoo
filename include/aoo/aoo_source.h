/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C interface for AOO source
 */

#pragma once

#include "aoo.h"
#include "aoo_events.h"
#include "aoo_controls.h"

typedef struct AooSource AooSource;

/** \brief create a new AOO source instance
 *
 * \param id the ID
 * \param flags optional flags
 * \param[out] err error code on failure
 * \return new AooSource instance on success; `NULL` on failure
 */
AOO_API AooSource * AOO_CALL AooSource_new(
        AooId id, AooFlag flags, AooError *err);

/** \brief destroy the AOO source instance */
AOO_API void AOO_CALL AooSource_free(AooSource *source);

/** \brief setup AOO source
 *
 * \warning Not threadsafe - needs to be synchronized with other method calls!
 *
 * \param source the AOO source
 * \param sampleRate the sample rate
 * \param blockSize the max. blocksize
 * \param numChannels the max. number of channels
 */
AOO_API AooError AOO_CALL AooSource_setup(
        AooSource *source, AooSampleRate sampleRate,
        AooInt32 blockSize, AooInt32 numChannels);

/** \brief handle sink messages
 *
 * \note Threadsafe; call on the network thread
 *
 * \param source the AOO source
 * \param data the message data
 * \param size the message size in bytes
 * \param address the remote socket address
 * \param addrlen the socket address length
 */
AOO_API AooError AOO_CALL AooSource_handleMessage(
        AooSource *source, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen);

/** \brief send outgoing messages
 *
 * \note Threadsafe; call on the network thread
 *
 * \param source the AOO source
 * \param fn the send function
 * \param user the user data (passed to the send function)
 */
AOO_API AooError AOO_CALL AooSource_send(
        AooSource *source, AooSendFunc fn, void *user);

/** \brief process audio
 *
 * \note Threadsafe and RT-safe; call on the audio thread
 *
 * \param source the AOO source
 * \param data an array of audio channels; the number of
 *        channels must match the number in #AooSource_setup.
 * \param numSamples the number of samples per channel
 * \param t current NTP time; \see aoo_getCurrentNtpTime
 */
AOO_API AooError AOO_CALL AooSource_process(
        AooSource *source, AooSample **data, AooInt32 numSamples, AooNtpTime t);

/** \brief set event handler function and event handling mode
 *
 * \warning Not threadsafe - only call in the beginning! */
AOO_API AooError AOO_CALL AooSource_setEventHandler(
        AooSource *source, AooEventHandler fn, void *user, AooEventMode mode);

/** \brief check for pending events
 *
 * \note Threadsafe and RT-safe */
AOO_API AooBool AOO_CALL AooSource_eventsAvailable(AooSource *source);

/** \brief poll events
 *
 * \note Threadsafe and RT-safe, but not reentrant.
 *
 * This function will call the registered event handler one or more times.
 * \attention The event handler must have been registered with #kAooEventModePoll.
 */
AOO_API AooError AOO_CALL AooSource_pollEvents(AooSource *source);

/** \brief Start a new stream
 *
 * \note Threadsafe, RT-safe and reentrant
 *
 * You can pass an optional AooDataView structure which will be sent as
 * additional stream metadata. For example, it could contain information
 * about the channel layout, the musical content, etc.
 */
AOO_API AooError AOO_CALL AooSource_startStream(
        AooSource *source, const AooDataView *metadata);

/** \brief Stop the current stream */
AOO_API AooError AOO_CALL AooSource_stopStream(AooSource *source);

/** \brief sink should start active
 * \see AooSource_addSink */
#define kAooSinkActive 0x01 // start active

/** \brief add sink
 *
 * Unless you pass the #kAooSinkActive flag, sinks are initially deactivated
 * and have to be activated manually with AooSource_activateSink().
 */
AOO_API AooError AOO_CALL AooSource_addSink(
        AooSource *source, const AooEndpoint *sink, AooFlag flags);

/** \brief remove sink */
AOO_API AooError AOO_CALL AooSource_removeSink(
        AooSource *source, const AooEndpoint *sink);

/** \brief remove all sinks */
AOO_API AooError AOO_CALL AooSource_removeAll(AooSource *source);

/** \brief accept/decline an invitation
 *
 * When you receive an #kAooEventInvite event, you can decide to
 * accept or decline the invitation.
 * If you choose to accept it, you have to call this function with
 * the `token` of the corresponding event; before you might want to
 * perform certain actions, e.g. based on the metadata.
 * (Calling this with a valid token essentially activates the sink.)
 * If you choose to decline it, call it with #kAooIdInvalid.
 */
AOO_API AooError AOO_CALL AooSource_acceptInvitation(
        AooSource *source, const AooEndpoint *sink, AooId token);

/** \brief accept/decline an uninvitation
 *
 * When you receive an #kAooEventUninvite event, you can decide to
 * accept or decline the uninvitation.
 * If you choose to accept it, you have to call this function with
 * the `token` of the corresponding event.
 * (Calling this with a valid token essentially deactivates the sink.)
 * If you choose to decline it, call it with #kAooIdInvalid.
 */
AOO_API AooError AOO_CALL AooSource_acceptUninvitation(
        AooSource *source, const AooEndpoint *sink, AooId token);

/** \brief control interface
 *
 * used internally by helper functions for specific controls */
AOO_API AooError AOO_CALL AooSource_control(
        AooSource *source, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

/** \brief codec control interface
 *
 * used internally by helper functions for specific codec controls */
AOO_API AooError AOO_CALL AooSource_codecControl(
        AooSource *source,  AooCtl ctl, AooIntPtr index, void *data, AooSize size);

/*--------------------------------------------*/
/*         type-safe control functions        */
/*--------------------------------------------*/


/** \brief (De)activate the given sink */
AOO_INLINE AooError AooSource_activate(
        AooSource *source, const AooEndpoint *sink, AooBool active)
{
    return AooSource_control(source, kAooCtlActivate, (AooIntPtr)sink, AOO_ARG(active));
}

/** \brief Check whether the given sink is active */
AOO_INLINE AooError AooSource_isActive(
        AooSource *source, const AooEndpoint *sink, AooBool *active)
{
    return AooSource_control(source, kAooCtlIsActive, (AooIntPtr)sink, AOO_ARG(*active));
}

/** \brief Reset the source */
AOO_INLINE AooError AooSource_reset(AooSource *source)
{
    return AooSource_control(source, kAooCtlReset, 0, NULL, 0);
}

/** \brief Set the stream format
 *
 * \param[in,out] format Pointer to the format header.
 * The format struct is validated and updated on success!
 *
 * This will change the streaming format and consequently start a new stream.
 * The sink(s) will receive a `kAooEventFormatChange` event.
 */
AOO_INLINE AooError AooSource_setFormat(AooSource *source, AooFormat *format)
{
    return AooSource_control(source, kAooCtlSetFormat, 0, AOO_ARG(*format));
}

/** \brief Get the stream format
 *
 * \param[out] format Pointer to an instance of `AooFormatStorage` or a similar
 * struct that is large enough to hold any codec format.
 * The `size` member in the format header should contain the storage size;
 * on success it is updated to the actual format size
 */
AOO_INLINE AooError AooSource_getFormat(AooSource *source, AooFormat *format)
{
    return AooSource_control(source, kAooCtlGetFormat, 0, AOO_ARG(*format));
}

/** \brief Set AOO source ID
 * \param id The new ID
 */
AOO_INLINE AooError AooSource_setId(AooSource *source, AooId id)
{
    return AooSource_control(source, kAooCtlSetId, 0, AOO_ARG(id));
}

/** \brief Get AOO source ID
 * \param[out] id The current ID
 */
AOO_INLINE AooError AooSource_getId(AooSource *source, AooId *id)
{
    return AooSource_control(source, kAooCtlGetId, 0, AOO_ARG(*id));
}

/** \brief Set the buffer size in seconds (in seconds)
 *
 * This is the size of the ring buffer between the audio and network thread.
 * The value can be rather small, as you only have to compensate for the time
 * it takes to wake up the network thread.
 */
AOO_INLINE AooError AooSource_setBufferSize(AooSource *source, AooSeconds s)
{
    return AooSource_control(source, kAooCtlSetBufferSize, 0, AOO_ARG(s));
}

/** \brief Get the current buffer size (in seconds) */
AOO_INLINE AooError AooSource_getBufferSize(AooSource *source, AooSeconds *s)
{
    return AooSource_control(source, kAooCtlGetBufferSize, 0, AOO_ARG(*s));
}

/** \brief Enable/disable xrun detection
 *
 * xrun detection helps to catch timing problems, e.g. when the host accidentally
 * blocks the audio callback, which would confuse the time DLL filter.
 * Also, timing gaps are handled by sending empty blocks.
 * NOTE: only takes effect on source/sink setup!
 */
AOO_INLINE AooError AooSource_setXRunDetection(AooSource *source, AooBool b)
{
    return AooSource_control(source, kAooCtlSetXRunDetection, 0, AOO_ARG(b));
}

/** \brief Check if xrun detection is enabled */
AOO_INLINE AooError AooSource_getXRunDetection(AooSource *source, AooBool *b)
{
    return AooSource_control(source, kAooCtlGetXRunDetection, 0, AOO_ARG(*b));
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
AOO_INLINE AooError AooSource_setDynamicResampling(AooSource *source, AooBool b)
{
    return AooSource_control(source, kAooCtlSetDynamicResampling, 0, AOO_ARG(b));
}

/** \brief Check if dynamic resampling is enabled. */
AOO_INLINE AooError AooSource_getDynamicResampling(AooSource *source, AooBool *b)
{
    return AooSource_control(source, kAooCtlGetDynamicResampling, 0, AOO_ARG(*b));
}

/** \brief Get the "real" samplerate as measured by the DLL filter */
AOO_INLINE AooError AooSource_getRealSampleRate(AooSource *source, AooSample *sr)
{
    return AooSource_control(source, kAooCtlGetRealSampleRate, 0, AOO_ARG(*sr));
}

/** \brief Set DLL filter bandwidth
 *
 * Used for dynamic resampling, see AooSource_setDynamicResampling().
 */
AOO_INLINE AooError AooSource_setDllBandwidth(AooSource *source, double q)
{
    return AooSource_control(source, kAooCtlSetDllBandwidth, 0, AOO_ARG(q));
}

/** \brief get DLL filter bandwidth */
AOO_INLINE AooError AooSource_getDllBandwidth(AooSource *source, double *q)
{
    return AooSource_control(source, kAooCtlGetDllBandwidth, 0, AOO_ARG(*q));
}

/** \brief Set the max. UDP packet size in bytes
 *
 * The default value should be fine for most networks (including the internet),
 * but you might want to increase this value for local networks because larger
 * packet sizes have less overhead. If a audio block exceeds the max. UDP packet size,
 * it will be automatically broken up into several "frames" and then reassembled in the sink.
 */
AOO_INLINE AooError AooSource_setPacketSize(AooSource *source, AooInt32 n)
{
    return AooSource_control(source, kAooCtlSetPacketSize, 0, AOO_ARG(n));
}

/** \brief Get the max. UDP packet size */
AOO_INLINE AooError AooSource_getPacketSize(AooSource *source, AooInt32 *n)
{
    return AooSource_control(source, kAooCtlGetPacketSize, 0, AOO_ARG(*n));
}

/** \brief Set the ping interval (in seconds)
 *
 * The source sends a periodic ping message to each sink which the sink has
 * to answer to signify that it is actually receiving data.
 * For example, a application might choose to remove a sink after the source
 * hasn't received a ping for a certain amount of time.
 */
AOO_INLINE AooError AooSource_setPingInterval(AooSource *source, AooSeconds s)
{
    return AooSource_control(source, kAooCtlSetPingInterval, 0, AOO_ARG(s));
}

/** \brief Get the ping interval (in seconds) */
AOO_INLINE AooError AooSource_getPingInterval(AooSource *source, AooSeconds *s)
{
    return AooSource_control(source, kAooCtlGetPingInterval, 0, AOO_ARG(*s));
}

/** \brief Set the resend buffer size (in seconds)
 *
 * The source keeps the last N seconds of audio in a buffer, so it can resend
 * parts of it if requested (to handle packet loss)
 */
AOO_INLINE AooError AooSource_setResendBufferSize(AooSource *source, AooSeconds s)
{
    return AooSource_control(source, kAooCtlSetResendBufferSize, 0, AOO_ARG(s));
}

/** \brief Get the resend buffer size (in seconds) */
AOO_INLINE AooError AooSource_getResendBufferSize(AooSource *source, AooSeconds *s)
{
    return AooSource_control(source, kAooCtlGetResendBufferSize, 0, AOO_ARG(*s));
}

/** \brief Set redundancy
 *
 * The number of times each frames is sent (default = 1). This is a primitive
 * strategy to cope with possible packet loss, but it can be counterproductive:
 * packet loss is often the result of network contention and sending more data
 * would only make it worse.
 */
AOO_INLINE AooError AooSource_setRedundancy(AooSource *source, AooInt32 n)
{
    return AooSource_control(source, kAooCtlSetRedundancy, 0, AOO_ARG(n));
}

/** \brief Get redundancy */
AOO_INLINE AooError AooSource_getRedundancy(AooSource *source, AooInt32 *n)
{
    return AooSource_control(source, kAooCtlGetRedundancy, 0, AOO_ARG(*n));
}


/** \brief Enable/disable binary data messages
 *
 * Use a more compact (and faster) binary format for the audio data message
 */
AOO_INLINE AooError AooSource_setBinaryDataMsg(AooSource *source, AooBool b)
{
    return AooSource_control(source, kAooCtlSetBinaryDataMsg, 0, AOO_ARG(b));
}

/** \brief Check if binary data messages are enabled */
AOO_INLINE AooError AooSource_getBinaryDataMsg(AooSource *source, AooBool *b)
{
    return AooSource_control(source, kAooCtlGetBinaryDataMsg, 0, AOO_ARG(*b));
}

/** \brief Set the max. size of stream metadata
 *
 * Setting this property will allocate enough memory to hold any stream metadata
 * up to the given size. Use this to avoid allocating memory in kAooCtlStartStream.
 */
AOO_INLINE AooError AooSource_setStreamMetadataSize(AooSource *source, AooInt32 size)
{
    return AooSource_control(source, kAooCtlSetStreamMetadataSize, 0, AOO_ARG(size));
}

/** \brief Get the current max. size of stream metadata. */
AOO_INLINE AooError AooSource_getStreamMetadataSize(AooSource *source, AooInt32 *size)
{
    return AooSource_control(source, kAooCtlGetStreamMetadataSize, 0, AOO_ARG(*size));
}

/** \brief Set the sink channel onset
 *
 * Set channel onset of the given sink where the source signal should be received.
 * For example, if the channel onset is 5, a 2-channel source signal will be summed
 * into sink channels 5 and 6. The default is 0 (= the first channel).
 */
AOO_INLINE AooError AooSource_setSinkChannelOnset(
        AooSource *source, const AooEndpoint *sink, AooInt32 onset)
{
    return AooSource_control(source, kAooCtlSetChannelOnset, (AooIntPtr)sink, AOO_ARG(onset));
}

/** \brief Get the sink channel onset for the given sink */
AOO_INLINE AooError AooSource_getSinkChannelOnset(
        AooSource *source, const AooEndpoint *sink, AooInt32 *onset)
{
    return AooSource_control(source, kAooCtlGetChannelOnset, (AooIntPtr)sink, AOO_ARG(*onset));
}
