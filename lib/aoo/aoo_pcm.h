#pragma once

#include "aoo.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*/////////////////// PCM codec ////////////////////////*/

#define AOO_CODEC_PCM "pcm"

typedef enum
{
    AOO_PCM_INT16,
    AOO_PCM_INT24,
    AOO_PCM_FLOAT32,
    AOO_PCM_FLOAT64,
    AOO_PCM_BITDEPTH_SIZE
} aoo_pcm_bitdepth;

typedef struct aoo_format_pcm
{
    aoo_format header;
    aoo_pcm_bitdepth bitdepth;
} aoo_format_pcm;

AOO_EXTERN void aoo_codec_pcm_setup(aoo_codec_registerfn fn);

#ifdef __cplusplus
} // extern "C"
#endif
