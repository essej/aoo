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

    // smart pointer for AoO source instance
    using Ptr = std::unique_ptr<AooSource, Deleter>;

    // create a new managed AoO source instance
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
            const AooSample **data, AooInt32 numSamples, AooNtpTime t) = 0;

    // set event handler callback + mode
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    // check for pending events (always thread safe)
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    // poll events (threadsafe, but not reentrant).
    // will call the event handler function one or more times.
    // NOTE: the event handler must have been registered with kAooEventModePoll.
    virtual AooError AOO_CALL pollEvents() = 0;

    // control interface (always threadsafe)
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    // ----------------------------------------------------------
    // type-safe convenience methods for frequently used controls

    AooError startStream() {
        return control(kAooCtlStartStream, 0, nullptr, 0);
    }

    AooError stopStream() {
        return control(kAooCtlStopStream, 0, nullptr, 0);
    }

    AooError addSink(const AooEndpoint& sink, AooFlag flags = 0) {
        return control(kAooCtlAddSink, (AooIntPtr)&sink, AOO_ARG(flags));
    }

    AooError removeSink(const AooEndpoint& sink) {
        return control(kAooCtlRemoveSink, (AooIntPtr)&sink, nullptr, 0);
    }

    AooError removeAllSinks() {
        return control(kAooCtlRemoveSink, 0, nullptr, 0);
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

    AooError setTimerCheck(AooBool b) {
        return control(kAooCtlSetTimerCheck, 0, AOO_ARG(b));
    }

    AooError getTimerCheck(AooBool b) {
        return control(kAooCtlGetTimerCheck, 0, AOO_ARG(b));
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
protected:
    ~AooSource(){} // non-virtual!
};
