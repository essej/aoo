/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C++ interface for AOO sink
 */

#pragma once

#include "aoo_sink.h"

#include <memory>

/** \brief AOO sink interface */
struct AooSink {
public:
    /** \brief custom deleter for AooSink */
    class Deleter {
    public:
        void operator()(AooSink *obj){
            AooSink_free(obj);
        }
    };

    /** \brief smart pointer for AOO sink instance */
    using Ptr = std::unique_ptr<AooSink, Deleter>;

    /** \brief create a new managed AOO sink instance
     *
     * \copydetails AooSink_new()
     */
    static Ptr create(AooId id, AooFlag flags, AooError *err) {
        return Ptr(AooSink_new(id, flags, err));
    }

    /*-------------------- methods -----------------------------*/

    /** \copydoc AooSink_setup() */
    virtual AooError AOO_CALL setup(
            AooSampleRate sampleRate, AooInt32 blockSize, AooInt32 numChannels) = 0;

    /** \copydoc AooSink_handleMessage() */
    virtual AooError AOO_CALL handleMessage(
            const AooByte *data, AooInt32 size,
            const void *address, AooAddrSize addrlen) = 0;

    /** \copydoc AooSink_send() */
    virtual AooError AOO_CALL send(AooSendFunc fn, void *user) = 0;

    /** \copydoc AooSink_process() */
    virtual AooError AOO_CALL process(AooSample **data, AooInt32 numSamples,
                                      AooNtpTime t) = 0;

    /** \copydoc AooSink_setEventHandler() */
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    /** \copydoc AooSink_eventsAvailable() */
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    /** \copydoc AooSink_pollEvents() */
    virtual AooError AOO_CALL pollEvents() = 0;

    /** \copydoc AooSink_inviteSource() */
    virtual AooError AOO_CALL inviteSource(
            const AooEndpoint& source, const AooDataView *metadata) = 0;

    /** \copydoc AooSink_inviteSource() */
    virtual AooError AOO_CALL uninviteSource(const AooEndpoint& source) = 0;

    /** \copydoc AooSink_ininviteAll() */
    virtual AooError AOO_CALL uninviteAll() = 0;

    /** \copydoc AooSink_control() */
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    /** \copydoc AooSink_codecControl() */
    virtual AooError AOO_CALL codecControl(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    /*--------------------------------------------*/
    /*         type-safe control functions        */
    /*--------------------------------------------*/

    /** \copydoc AooSink_setId() */
    AooError setId(AooId id) {
        return control(kAooCtlSetId, 0, AOO_ARG(id));
    }

    /** \copydoc AooSink_getId() */
    AooError getId(AooId &id) {
        return control(kAooCtlGetId, 0, AOO_ARG(id));
    }

    /** \copydoc AooSink_reset() */
    AooError reset() {
        return control(kAooCtlReset, 0, nullptr, 0);
    }

    /** \copydoc AooSink_setBufferSize() */
    AooError setBufferSize(AooSeconds s) {
        return control(kAooCtlSetBufferSize, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_getBufferSize() */
    AooError getBufferSize(AooSeconds& s) {
        return control(kAooCtlGetBufferSize, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_setXRunDetection() */
    AooError setXRunDetection(AooBool b) {
        return control(kAooCtlSetXRunDetection, 0, AOO_ARG(b));
    }

    /** \copydoc AooSink_getXRunDetection() */
    AooError getXRunDetection(AooBool b) {
        return control(kAooCtlGetXRunDetection, 0, AOO_ARG(b));
    }

    /** \copydoc AooSink_setDynamicResampling() */
    AooError setDynamicResampling(AooBool b) {
        return control(kAooCtlSetDynamicResampling, 0, AOO_ARG(b));
    }

    /** \copydoc AooSink_getDynamicResampling() */
    AooError getDynamicResampling(AooBool b) {
        return control(kAooCtlGetDynamicResampling, 0, AOO_ARG(b));
    }

    /** \copydoc AooSink_getRealSampleRate() */
    AooError getRealSampleRate(AooSampleRate& sr) {
        return control(kAooCtlGetRealSampleRate, 0, AOO_ARG(sr));
    }

    /** \copydoc AooSink_setDllBandwidth() */
    AooError setDllBandwidth(double q) {
        return control(kAooCtlSetDllBandwidth, 0, AOO_ARG(q));
    }

    /** \copydoc AooSink_getDllBandwidth() */
    AooError getDllBandwidth(double& q) {
        return control(kAooCtlGetDllBandwidth, 0, AOO_ARG(q));
    }

    /** \copydoc AooSink_setPacketSize() */
    AooError setPacketSize(AooInt32 n) {
        return control(kAooCtlSetPacketSize, 0, AOO_ARG(n));
    }

    /** \copydoc AooSink_getPacketSize() */
    AooError getPacketSize(AooInt32& n) {
        return control(kAooCtlGetPacketSize, 0, AOO_ARG(n));
    }

    /** \copydoc AooSink_setResendData() */
    AooError setResendData(AooBool b) {
        return control(kAooCtlSetResendData, 0, AOO_ARG(b));
    }

    /** \copydoc AooSink_getResendData() */
    AooError getResendData(AooBool& b) {
        return control(kAooCtlGetResendData, 0, AOO_ARG(b));
    }

    /** \copydoc AooSink_setResendInterval() */
    AooError setResendInterval(AooSeconds s) {
        return control(kAooCtlSetResendInterval, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_getResendInterval() */
    AooError getResendInterval(AooSeconds& s) {
        return control(kAooCtlGetResendInterval, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_setResendLimit() */
    AooError setResendLimit(AooInt32 n) {
        return control(kAooCtlSetResendLimit, 0, AOO_ARG(n));
    }

    /** \copydoc AooSink_getResendLimit() */
    AooError getResendLimit(AooInt32& n) {
        return control(kAooCtlGetResendLimit, 0, AOO_ARG(n));
    }

    /** \copydoc AooSink_setSourceTimeout() */
    AooError setSourceTimeout(AooSeconds s) {
        return control(kAooCtlSetSourceTimeout, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_getSourceTimeout() */
    AooError getSourceTimeout(AooSeconds& s) {
        return control(kAooCtlGetSourceTimeout, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_setInviteTimeout() */
    AooError setInviteTimeout(AooSeconds s) {
        return control(kAooCtlSetInviteTimeout, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_getInviteTimeout() */
    AooError getInviteTimeout(AooSeconds& s) {
        return control(kAooCtlGetInviteTimeout, 0, AOO_ARG(s));
    }

    /** \copydoc AooSink_resetSource() */
    AooError resetSource(const AooEndpoint& source) {
        return control(kAooCtlReset, (AooIntPtr)&source, nullptr, 0);
    }

    /** \copydoc AooSink_getSourceFormat() */
    AooError getSourceFormat(const AooEndpoint& source, AooFormatStorage& f) {
        return control(kAooCtlGetFormat, (AooIntPtr)&source, AOO_ARG(f));
    }

    /** \copydoc AooSink_getBufferFillRatio() */
    AooError getBufferFillRatio(const AooEndpoint& source, double& ratio) {
        return control(kAooCtlGetBufferFillRatio, (AooIntPtr)&source, AOO_ARG(ratio));
    }
protected:
    ~AooSink(){} // non-virtual!
};
