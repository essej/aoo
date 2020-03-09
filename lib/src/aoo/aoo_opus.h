#pragma once

#include "aoo.h"
#include "opus/include/opus.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*/////////////////// Opus codec ////////////////////////*/

#define AOO_CODEC_OPUS "opus"

typedef struct aoo_format_opus
{
    aoo_format header;
    int32_t bitrate; // 0: default
    int32_t complexity; // 0: default
    int32_t signal_type;
} aoo_format_opus;

void aoo_codec_opus_setup(aoo_codec_registerfn fn);

#ifdef __cplusplus
} // extern "C"
#endif
