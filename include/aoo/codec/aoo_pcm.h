/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_defines.h"

#include <string.h>

AOO_PACK_BEGIN

//--------------------------------//

// PCM codec

#define kAooCodecPcm "pcm"

typedef AooInt32 AooPcmBitDepth;

enum AooPcmBitDepthValues
{
    kAooPcmInt16 = 0,
    kAooPcmInt24,
    kAooPcmFloat32,
    kAooPcmFloat64,
    kAooPcmBitDepthSize
};

typedef AOO_STRUCT AooFormatPcm
{
    AooFormat header;
    AooPcmBitDepth bitDepth;
} AooFormatPcm;

//-----------------------------------//

static inline void AooFormatPcm_init(
        AooFormatPcm *f, AooInt32 numChannels, AooInt32 sampleRate,
        AooInt32 blockSize, AooPcmBitDepth bitDepth)
{
    strcpy(f->header.codec, kAooCodecPcm);
    f->header.size = sizeof(AooFormatPcm);
    f->header.numChannels = numChannels;
    f->header.sampleRate = sampleRate;
    f->header.blockSize = blockSize;
    f->bitDepth = bitDepth;
}

//----------------------------------//

AOO_PACK_END
