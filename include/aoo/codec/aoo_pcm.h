/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_defines.h"

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

//--------------------------------//

AOO_PACK_END
