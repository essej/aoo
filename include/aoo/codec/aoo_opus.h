/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_defines.h"

#include <opus/opus_multistream.h>

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
    // bitrate in bits/s, OPUS_BITRATE_MAX or OPUS_AUTO
    opus_int32 bitrate;
    // complexity 0-10 or OPUS_AUTO
    opus_int32 complexity;
    // OPUS_SIGNAL_VOICE, OPUS_SIGNAL_MUSIC or OPUS_AUTO
    opus_int32 signalType;
} AooFormatOpus;

//--------------------------------//

AOO_PACK_END
