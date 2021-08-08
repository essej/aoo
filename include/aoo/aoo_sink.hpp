#pragma once

#include "aoo_sink.h"

#include <memory>

struct AooSink {
public:
    class Deleter {
    public:
        void operator()(AooSink *obj){
            AooSink_free(obj);
        }
    };

    // smart pointer for AoO sink instance
    using Ptr = std::unique_ptr<AooSink, Deleter>;

    // create a new managed AoO sink instance
    static Ptr create(AooId id, AooFlag flags, AooError *err) {
        return Ptr(AooSink_new(id, flags, err));
    }

    //------------------- methods ----------------------------//

    // setup the sink - needs to be synchronized with other method calls!
    virtual AooError AOO_CALL setup(
            AooSampleRate sampleRate, AooInt32 blockSize, AooInt32 numChannels) = 0;

    // handle messages from sources (threadsafe, called from a network thread)
    virtual AooError AOO_CALL handleMessage(
            const AooByte *data, AooInt32 n,
            const void *address, AooAddrSize addrlen) = 0;

    // send outgoing messages (threadsafe, called from a network thread)
    virtual AooError AOO_CALL send(AooSendFunc fn, void *user) = 0;

    // process audio (threadsafe, but not reentrant)
    virtual AooError AOO_CALL process(AooSample **data, AooInt32 numSamples,
                                      AooNtpTime t) = 0;

    // set event handler callback + mode
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    // check for pending events (always thread safe)
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    // poll events (threadsafe, but not reentrant).
    // will call the event handler function one or more times.
    // NOTE: the event handler must have been registered with kAooEventModePoll.
    virtual AooError AOO_CALL pollEvents() = 0;

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
    virtual AooError AOO_CALL inviteSource(
            const AooEndpoint& source, const AooCustomData *metadata) = 0;

    // uninvite source
    // ---
    // This will continuously send uninvitation requests to the source.
    // The source can either accept the uninvitation request and stop the
    // stream, or it can ignore and continue sending, upon which the sink
    // will eventually receive an AooEventUninviteTimeout event.
    virtual AooError AOO_CALL uninviteSource(const AooEndpoint& source) = 0;

    // uninvite all sources
    virtual AooError AOO_CALL uninviteAll() = 0;

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

    AooError setId(AooId id) {
        return control(kAooCtlSetId, 0, AOO_ARG(id));
    }

    AooError getId(AooId &id) {
        return control(kAooCtlGetId, 0, AOO_ARG(id));
    }

    AooError reset() {
        return control(kAooCtlReset, 0, nullptr, 0);
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

    AooError setResendData(AooBool b) {
        return control(kAooCtlSetResendData, 0, AOO_ARG(b));
    }

    AooError getResendData(AooBool& b) {
        return control(kAooCtlGetResendData, 0, AOO_ARG(b));
    }

    AooError setResendInterval(AooSeconds s) {
        return control(kAooCtlSetResendInterval, 0, AOO_ARG(s));
    }

    AooError getResendInterval(AooSeconds& s) {
        return control(kAooCtlGetResendInterval, 0, AOO_ARG(s));
    }

    AooError setResendLimit(AooInt32 n) {
        return control(kAooCtlSetResendLimit, 0, AOO_ARG(n));
    }

    AooError getResendLimit(AooInt32& n) {
        return control(kAooCtlGetResendLimit, 0, AOO_ARG(n));
    }

    AooError setSourceTimeout(AooSeconds s) {
        return control(kAooCtlSetSourceTimeout, 0, AOO_ARG(s));
    }

    AooError getSourceTimeout(AooSeconds& s) {
        return control(kAooCtlGetSourceTimeout, 0, AOO_ARG(s));
    }

    AooError setInviteTimeout(AooSeconds s) {
        return control(kAooCtlSetInviteTimeout, 0, AOO_ARG(s));
    }

    AooError getInviteTimeout(AooSeconds& s) {
        return control(kAooCtlGetInviteTimeout, 0, AOO_ARG(s));
    }

    AooError resetSource(const AooEndpoint& source) {
        return control(kAooCtlReset, (AooIntPtr)&source, nullptr, 0);
    }

    AooError getSourceFormat(const AooEndpoint& source, AooFormatStorage& f) {
        return control(kAooCtlGetFormat, (AooIntPtr)&source, AOO_ARG(f));
    }

    AooError getBufferFillRatio(const AooEndpoint& source, double& ratio) {
        return control(kAooCtlGetBufferFillRatio, (AooIntPtr)&source, AOO_ARG(ratio));
    }
protected:
    ~AooSink(){} // non-virtual!
};
