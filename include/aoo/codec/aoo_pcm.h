/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief PCM codec settings
 */

#pragma once

#include "aoo/aoo_config.h"
#include "aoo/aoo_defines.h"
#include "aoo/aoo_types.h"

#include <string.h>

struct AooSource;

AOO_API AooError AOO_CALL AooSource_codecControl(
        AooSource *source,  AooCtl ctl, AooIntPtr index,
        void *data, AooSize size);

AOO_PACK_BEGIN

/*--------------------------------------------------*/

#define kAooCodecPcm "pcm"

typedef AooInt32 AooPcmBitDepth;

/** \brief PCM bit depth values */
enum AooPcmBitDepthValues
{
    kAooPcmInt8 = 0,
    kAooPcmInt16,
    kAooPcmInt24,
    kAooPcmFloat32,
    kAooPcmFloat64,
    kAooPcmBitDepthSize
};

/** \brief PCM codec format */
typedef struct AooFormatPcm
{
    AooFormat header;
    AooPcmBitDepth bitDepth;
} AooFormatPcm;

/*------------------------------------------------*/

/** \brief initialize AooFormatPcm structure */
AOO_INLINE void AooFormatPcm_init(
        AooFormatPcm *fmt,
        AooInt32 numChannels, AooInt32 sampleRate,
        AooInt32 blockSize, AooPcmBitDepth bitDepth)
{
    strcpy(fmt->header.codec, kAooCodecPcm);
    AOO_STRUCT_INIT(&fmt->header, AooFormatPcm, bitDepth);
    fmt->header.numChannels = numChannels;
    fmt->header.sampleRate = sampleRate;
    fmt->header.blockSize = blockSize;
    fmt->bitDepth = bitDepth;
}

/*-----------------------------------------------*/

AOO_PACK_END
