#pragma once

#include "aoo.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*/////////////////// Opus codec ////////////////////////*/

#define AOO_CODEC_OPUS "opus"

typedef enum
{
    AOO_OPUS_AUTO,
    AOO_OPUS_SIGNAL_VOICE,
    AOO_OPUS_SIGNAL_MUSIC,
    AOO_OPUS_TYPE_SIZE
} aoo_opus_type;

typedef struct aoo_format_opus
{
    aoo_format header;
    int32_t bitrate; // 0: default
    int32_t complexity; // 0: default
    aoo_opus_type type;
} aoo_format_opus;

void aoo_codec_opus_setup(aoo_codec_registerfn fn);

#ifdef __cplusplus
} // extern "C"
#endif
