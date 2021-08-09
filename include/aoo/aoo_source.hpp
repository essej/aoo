#pragma once

#include "aoo_source.h"

#include <memory>

struct AooSource {
public:
    class Deleter {
    public:
        void operator()(AooSource *obj){
            AooSource_free(obj);
        }
    };

    // smart pointer for AOO source instance
    using Ptr = std::unique_ptr<AooSource, Deleter>;

    // create a new managed AOO source instance
    static Ptr create(AooId id, AooFlag flags, AooError *err) {
        return Ptr(AooSource_new(id, flags, err));
    }

    //------------------ methods ----------------------//

    // setup the source - needs to be synchronized with other method calls!
    virtual AooError AOO_CALL setup(
            AooSampleRate sampleRate, AooInt32 blockSize, AooInt32 numChannels) = 0;

    // handle messages from sinks (threadsafe, called from a network thread)
    virtual AooError AOO_CALL handleMessage(
            const AooByte *data, AooInt32 size,
            const void *address, AooAddrSize addrlen) = 0;

    // send outgoing messages (threadsafe, called from a network thread)
    virtual AooError AOO_CALL send(AooSendFunc fn, void *user) = 0;

    // process audio blocks (threadsafe, called from the audio thread)
    // data:        array of channel data (non-interleaved)
    // numSamples:  number of samples per channel
    // t:           current NTP timestamp (see aoo_osctime_get)
    virtual AooError AOO_CALL process(
            AooSample **data, AooInt32 numSamples, AooNtpTime t) = 0;

    // set event handler callback + mode
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    // check for pending events (always thread safe)
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    // poll events (threadsafe, but not reentrant).
    // will call the event handler function one or more times.
    // NOTE: the event handler must have been registered with kAooEventModePoll.
    virtual AooError AOO_CALL pollEvents() = 0;

    // Start a new stream
    // ---
    // Can be called from any thread. Realtime safe!
    // You can pass an optional AooDataView structure which will be sent as
    // additional stream metadata. For example, it could contain information
    // about the channel layout, the musical content, etc.
    virtual AooError AOO_CALL startStream(
            const AooDataView *metadata) = 0;

    // Stop the stream
    virtual AooError AOO_CALL stopStream() = 0;

    // add sink
    // ---
    // Unless you pass the kAooSinkActive flag, sinks are
    // initially deactivated and have to be activated
    // manually with the kAooCtlActivate control.
    virtual AooError AOO_CALL addSink(
            const AooEndpoint& sink, AooFlag flags) = 0;

    // remove the given sink
    virtual AooError AOO_CALL removeSink(const AooEndpoint& sink) = 0;

    // remove all sinks
    virtual AooError AOO_CALL removeAll() = 0;

    // accept/decline an invitation
    // ---
    // When you receive an AooEventInvite event, you can decide to
    // accept or decline the invitation.
    // If you choose to accept it, you have to call this function with
    // the 'token' of the corresponding event; before you might want to
    // perform certain actions, e.g. based on the metadata.
    // (Calling this with a valid token essentially activates the sink.)
    // If you choose to decline it, call it with kAooIdInvalid.
    virtual AooError AOO_CALL acceptInvitation(
            const AooEndpoint& sink, AooId token) = 0;

    // accept/decline an uninvitation (index: sink, arg: AooBool)
    // ---
    // When you receive an AooEventUninvite event, you can decide to
    // accept or decline the uninvitation.
    // If you choose to accept it, you have to call this function with
    // the 'token' of the corresponding event.
    // (Calling this with a valid token essentially deactivates the sink.)
    // If you choose to decline it, call it with kAooIdInvalid.
    virtual AooError AOO_CALL acceptUninvitation(
            const AooEndpoint& sink, AooId token) = 0;

    // control interface (always threadsafe)
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    // codec control interface (always threadsafe)
    // ---
    // The available codec controls should be listed in the respective header file.
    virtual AooError AOO_CALL codecControl(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    // ----------------------------------------------------------
    // type-safe convenience methods for frequently used controls

    AooError activate(const AooEndpoint& sink, AooBool active) {
        return control(kAooCtlActivate, (AooIntPtr)&sink, AOO_ARG(active));
    }

    AooError isActive(const AooEndpoint& sink, AooBool& active) {
        return control(kAooCtlIsActive, (AooIntPtr)&sink, AOO_ARG(active));
    }

    AooError setFormat(AooFormat& format) {
        return control(kAooCtlSetFormat, 0, AOO_ARG(format));
    }

    AooError get_format(AooFormatStorage& format) {
        return control(kAooCtlGetFormat, 0, AOO_ARG(format));
    }

    AooError setId(AooId id) {
        return control(kAooCtlSetId, 0, AOO_ARG(id));
    }

    AooError getId(AooId &id) {
        return control(kAooCtlGetId, 0, AOO_ARG(id));
    }

    AooError setSinkChannelOnset(const AooEndpoint& sink, AooInt32 onset) {
        return control(kAooCtlSetChannelOnset, (AooIntPtr)&sink, AOO_ARG(onset));
    }

    AooError getSinkChannelOnset(const AooEndpoint& sink, AooInt32& onset) {
        return control(kAooCtlSetChannelOnset, (AooIntPtr)&sink, AOO_ARG(onset));
    }

    AooError setBufferSize(AooSeconds s) {
        return control(kAooCtlSetBufferSize, 0, AOO_ARG(s));
    }

    AooError getBufferSize(AooSeconds& s) {
        return control(kAooCtlGetBufferSize, 0, AOO_ARG(s));
    }

    AooError setXRunDetection(AooBool b) {
        return control(kAooCtlSetXRunDetection, 0, AOO_ARG(b));
    }

    AooError getXRunDetection(AooBool b) {
        return control(kAooCtlGetXRunDetection, 0, AOO_ARG(b));
    }

    AooError setDynamicResampling(AooBool b) {
        return control(kAooCtlSetDynamicResampling, 0, AOO_ARG(b));
    }

    AooError getDynamicResampling(AooBool b) {
        return control(kAooCtlGetDynamicResampling, 0, AOO_ARG(b));
    }

    AooError getRealSampleRate(AooSampleRate& sr) {
        return control(kAooCtlGetRealSampleRate, 0, AOO_ARG(sr));
    }

    AooError setDllBandwidth(double q) {
        return control(kAooCtlSetDllBandwidth, 0, AOO_ARG(q));
    }

    AooError getDllBandwidth(double& q) {
        return control(kAooCtlGetDllBandwidth, 0, AOO_ARG(q));
    }

    AooError setPacketSize(AooInt32 n) {
        return control(kAooCtlSetPacketSize, 0, AOO_ARG(n));
    }

    AooError getPacketSize(AooInt32& n) {
        return control(kAooCtlGetPacketSize, 0, AOO_ARG(n));
    }

    AooError setResendBufferSize(AooSeconds s) {
        return control(kAooCtlSetResendBufferSize, 0, AOO_ARG(s));
    }

    AooError getResendBufferSize(AooSeconds& s) {
        return control(kAooCtlGetResendBufferSize, 0, AOO_ARG(s));
    }

    AooError setRedundancy(AooInt32 n) {
        return control(kAooCtlSetRedundancy, 0, AOO_ARG(n));
    }

    AooError getRedundancy(AooInt32& n) {
        return control(kAooCtlGetRedundancy, 0, AOO_ARG(n));
    }

    AooError setPingInterval(AooSeconds s) {
        return control(kAooCtlSetPingInterval, 0, AOO_ARG(s));
    }

    AooError getPingInterval(AooSeconds& s) {
        return control(kAooCtlGetPingInterval, 0, AOO_ARG(s));
    }

    AooError setBinaryDataMsg(AooBool b) {
        return control(kAooCtlSetBinaryDataMsg, 0, AOO_ARG(b));
    }

    AooError getBinaryDataMsg(AooBool& b) {
        return control(kAooCtlGetBinaryDataMsg, 0, AOO_ARG(b));
    }

    AooError setStreamMetaDataSize(AooInt32 size) {
        return control(kAooCtlSetStreamMetadataSize, 0, AOO_ARG(size));
    }

    AooError getStreamMetaDataSize(AooInt32& size) {
        return control(kAooCtlGetStreamMetadataSize, 0, AOO_ARG(size));
    }
protected:
    ~AooSource(){} // non-virtual!
};
