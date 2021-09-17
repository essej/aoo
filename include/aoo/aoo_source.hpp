/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C++ interface for AOO source
 */

#pragma once

#include "aoo_source.h"

#include <memory>

/** \brief AOO source interface */
struct AooSource {
public:
    /** \brief custom deleter for AooSource */
    class Deleter {
    public:
        void operator()(AooSource *obj){
            AooSource_free(obj);
        }
    };

    /** \brief smart pointer for AOO source instance */
    using Ptr = std::unique_ptr<AooSource, Deleter>;

    /** \brief create a new managed AOO source instance
     *
     * \copydetails AooSource_new()
     */
    static Ptr create(AooId id, AooFlag flags, AooError *err) {
        return Ptr(AooSource_new(id, flags, err));
    }

    /*------------------- methods -----------------------*/

    /** \copydoc AooSource_setup() */
    virtual AooError AOO_CALL setup(
            AooSampleRate sampleRate,
            AooInt32 blockSize, AooInt32 numChannels) = 0;

    /** \copydoc AooSource_handleMessage() */
    virtual AooError AOO_CALL handleMessage(
            const AooByte *data, AooInt32 size,
            const void *address, AooAddrSize addrlen) = 0;

    /** \copydoc AooSource_send() */
    virtual AooError AOO_CALL send(AooSendFunc fn, void *user) = 0;

    /** \copydoc AooSource_process() */
    virtual AooError AOO_CALL process(
            AooSample **data, AooInt32 numSamples, AooNtpTime t) = 0;

    /** \copydoc AooSource_setEventHandler() */
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    /** \copydoc AooSource_eventsAvailable() */
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    /** \copydoc AooSource_pollEvents() */
    virtual AooError AOO_CALL pollEvents() = 0;

    /** \copydoc AooSource_startStream() */
    virtual AooError AOO_CALL startStream(
            const AooDataView *metadata) = 0;

    /** \copydoc AooSource_stopStream() */
    virtual AooError AOO_CALL stopStream() = 0;

    /** \copydoc AooSource_addSink() */
    virtual AooError AOO_CALL addSink(
            const AooEndpoint& sink, AooFlag flags) = 0;

    /** \copydoc AooSource_removeSink() */
    virtual AooError AOO_CALL removeSink(const AooEndpoint& sink) = 0;

    /** \copydoc AooSource_removeAll() */
    virtual AooError AOO_CALL removeAll() = 0;

    /** \copydoc AooSource_acceptInvitation() */
    virtual AooError AOO_CALL acceptInvitation(
            const AooEndpoint& sink, AooId token) = 0;

    /** \copydoc AooSource_acceptUninvitation() */
    virtual AooError AOO_CALL acceptUninvitation(
            const AooEndpoint& sink, AooId token) = 0;

    /** \copydoc AooSource_control() */
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    /** \copydoc AooSource_codecControl() */
    virtual AooError AOO_CALL codecControl(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    /*--------------------------------------------*/
    /*         type-safe control functions        */
    /*--------------------------------------------*/

    /** \copydoc AooSource_activate() */
    AooError activate(const AooEndpoint& sink, AooBool active) {
        return control(kAooCtlActivate, (AooIntPtr)&sink, AOO_ARG(active));
    }

    /** \copydoc AooSource_isActive() */
    AooError isActive(const AooEndpoint& sink, AooBool& active) {
        return control(kAooCtlIsActive, (AooIntPtr)&sink, AOO_ARG(active));
    }

    /** \copydoc AooSource_reset() */
    AooError reset() {
        return control(kAooCtlReset, 0, nullptr, 0);
    }

    /** \copydoc AooSource_setFormat() */
    AooError setFormat(AooFormat& format) {
        return control(kAooCtlSetFormat, 0, AOO_ARG(format));
    }

    /** \copydoc AooSource_getFormat() */
    AooError getFormat(AooFormatStorage& format) {
        return control(kAooCtlGetFormat, 0, AOO_ARG(format));
    }

    /** \copydoc AooSource_setId() */
    AooError setId(AooId id) {
        return control(kAooCtlSetId, 0, AOO_ARG(id));
    }

    /** \copydoc AooSource_getId() */
    AooError getId(AooId &id) {
        return control(kAooCtlGetId, 0, AOO_ARG(id));
    }

    /** \copydoc AooSource_setSinkChannelOnset() */
    AooError setSinkChannelOnset(const AooEndpoint& sink, AooInt32 onset) {
        return control(kAooCtlSetChannelOnset, (AooIntPtr)&sink, AOO_ARG(onset));
    }

    /** \copydoc AooSource_getSinkChannelOnset() */
    AooError getSinkChannelOnset(const AooEndpoint& sink, AooInt32& onset) {
        return control(kAooCtlSetChannelOnset, (AooIntPtr)&sink, AOO_ARG(onset));
    }

    /** \copydoc AooSource_setBufferSize() */
    AooError setBufferSize(AooSeconds s) {
        return control(kAooCtlSetBufferSize, 0, AOO_ARG(s));
    }

    /** \copydoc AooSource_getBufferSize() */
    AooError getBufferSize(AooSeconds& s) {
        return control(kAooCtlGetBufferSize, 0, AOO_ARG(s));
    }

    /** \copydoc AooSource_setXRunDetection() */
    AooError setXRunDetection(AooBool b) {
        return control(kAooCtlSetXRunDetection, 0, AOO_ARG(b));
    }

    /** \copydoc AooSource_getXRunDetection() */
    AooError getXRunDetection(AooBool b) {
        return control(kAooCtlGetXRunDetection, 0, AOO_ARG(b));
    }

    /** \copydoc AooSource_setDynamicResampling() */
    AooError setDynamicResampling(AooBool b) {
        return control(kAooCtlSetDynamicResampling, 0, AOO_ARG(b));
    }

    /** \copydoc AooSource_getDynamicResampling() */
    AooError getDynamicResampling(AooBool b) {
        return control(kAooCtlGetDynamicResampling, 0, AOO_ARG(b));
    }

    /** \copydoc AooSource_getRealSampleRate() */
    AooError getRealSampleRate(AooSampleRate& sr) {
        return control(kAooCtlGetRealSampleRate, 0, AOO_ARG(sr));
    }

    /** \copydoc AooSource_setDllBandwidth() */
    AooError setDllBandwidth(double q) {
        return control(kAooCtlSetDllBandwidth, 0, AOO_ARG(q));
    }

    /** \copydoc AooSource_getDllBandwidth() */
    AooError getDllBandwidth(double& q) {
        return control(kAooCtlGetDllBandwidth, 0, AOO_ARG(q));
    }

    /** \copydoc AooSource_setPacketSize() */
    AooError setPacketSize(AooInt32 n) {
        return control(kAooCtlSetPacketSize, 0, AOO_ARG(n));
    }

    /** \copydoc AooSource_getPacketSize() */
    AooError getPacketSize(AooInt32& n) {
        return control(kAooCtlGetPacketSize, 0, AOO_ARG(n));
    }

    /** \copydoc AooSource_setResendBufferSize() */
    AooError setResendBufferSize(AooSeconds s) {
        return control(kAooCtlSetResendBufferSize, 0, AOO_ARG(s));
    }

    /** \copydoc AooSource_getResendBufferSize() */
    AooError getResendBufferSize(AooSeconds& s) {
        return control(kAooCtlGetResendBufferSize, 0, AOO_ARG(s));
    }

    /** \copydoc AooSource_setRedundancy() */
    AooError setRedundancy(AooInt32 n) {
        return control(kAooCtlSetRedundancy, 0, AOO_ARG(n));
    }

    /** \copydoc AooSource_getRedundancy() */
    AooError getRedundancy(AooInt32& n) {
        return control(kAooCtlGetRedundancy, 0, AOO_ARG(n));
    }

    /** \copydoc AooSource_setPingInterval() */
    AooError setPingInterval(AooSeconds s) {
        return control(kAooCtlSetPingInterval, 0, AOO_ARG(s));
    }

    /** \copydoc AooSource_getPingInterval() */
    AooError getPingInterval(AooSeconds& s) {
        return control(kAooCtlGetPingInterval, 0, AOO_ARG(s));
    }

    /** \copydoc AooSource_setBinaryDataMsg() */
    AooError setBinaryDataMsg(AooBool b) {
        return control(kAooCtlSetBinaryDataMsg, 0, AOO_ARG(b));
    }

    /** \copydoc AooSource_getBinaryDataMsg() */
    AooError getBinaryDataMsg(AooBool& b) {
        return control(kAooCtlGetBinaryDataMsg, 0, AOO_ARG(b));
    }

    /** \copydoc AooSource_setStreamMetaDataSize() */
    AooError setStreamMetaDataSize(AooInt32 size) {
        return control(kAooCtlSetStreamMetadataSize, 0, AOO_ARG(size));
    }

    /** \copydoc AooSource_getStreamMetaDataSize() */
    AooError getStreamMetaDataSize(AooInt32& size) {
        return control(kAooCtlGetStreamMetadataSize, 0, AOO_ARG(size));
    }
protected:
    ~AooSource(){} // non-virtual!
};
