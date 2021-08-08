/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_defines.h"
#include "aoo/aoo_source.h"

#include <opus/opus_multistream.h>

#include <string.h>

AOO_PACK_BEGIN

//--------------------------------//

// Opus codec

#define kAooCodecOpus "opus"

typedef AOO_STRUCT AooFormatOpus
{
    AooFormat header;
    // OPUS_APPLICATION_VOIP, OPUS_APPLICATION_AUDIO or
    // OPUS_APPLICATION_RESTRICTED_LOWDELAY
    opus_int32 applicationType;
} AooFormatOpus;

//-----------------------------------------------------//

static inline void AooFormatOpus_init(
        AooFormatOpus *f, AooInt32 numChannels, AooInt32 sampleRate,
        AooInt32 blockSize, opus_int32 applicationType)
{
    strcpy(f->header.codec, kAooCodecOpus);
    f->header.size = sizeof(AooFormatOpus);
    f->header.numChannels = numChannels;
    f->header.sampleRate = sampleRate;
    f->header.blockSize = blockSize;
    f->applicationType = applicationType;
}

// helper functions for common controls

// set bitrate in bits/s, OPUS_BITRATE_MAX or OPUS_AUTO
static inline AooError AooSource_setOpusBitrate(
        AooSource *src, const AooEndpoint *sink, opus_int32 bitrate) {
    return AooSource_codecControl(
                src, OPUS_SET_BITRATE_REQUEST, (AooIntPtr)sink,
                &bitrate, sizeof(bitrate));
}

// get bitrate
static inline AooError AooSource_getOpusBitrate(
        AooSource *src, const AooEndpoint *sink, opus_int32 *bitrate) {
    return AooSource_codecControl(
                src, OPUS_GET_BITRATE_REQUEST, (AooIntPtr)sink,
                bitrate, sizeof(bitrate));
}

// set complexity (0-10 or OPUS_AUTO)
static inline AooError AooSource_setOpusComplexity(
        AooSource *src, const AooEndpoint *sink, opus_int32 complexity) {
    return AooSource_codecControl(
                src, OPUS_SET_COMPLEXITY_REQUEST, (AooIntPtr)sink,
                &complexity, sizeof(complexity));
}

// get complexity
static inline AooError AooSource_getOpusComplexity(
        AooSource *src, const AooEndpoint *sink, opus_int32 *complexity) {
    return AooSource_codecControl(
                src, OPUS_GET_COMPLEXITY_REQUEST, (AooIntPtr)sink,
                complexity, sizeof(complexity));
}

// set signal type
// (OPUS_SIGNAL_VOICE, OPUS_SIGNAL_MUSIC or OPUS_AUTO)
static inline AooError AooSource_setOpusSignalType(
        AooSource *src, const AooEndpoint *sink, opus_int32 signalType) {
    return AooSource_codecControl(
                src, OPUS_SET_SIGNAL_REQUEST, (AooIntPtr)sink,
                &signalType, sizeof(signalType));
}

// get signal type
static inline AooError AooSource_getOpusSignalType(
        AooSource *src, const AooEndpoint *sink, opus_int32 *signalType) {
    return AooSource_codecControl(
                src, OPUS_GET_SIGNAL_REQUEST, (AooIntPtr)sink,
                signalType, sizeof(signalType));
}

//------------------------------------------------------------------//

AOO_PACK_END
