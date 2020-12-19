/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo/codec/aoo_pcm.h"
#include "common/utils.hpp"

#include <cassert>
#include <cstring>

namespace {

// conversion routines between aoo_sample and PCM data
union convert {
    int8_t b[8];
    int16_t i16;
    int32_t i32;
    int64_t i64;
    float f;
    double d;
};

int32_t bytes_per_sample(int32_t bd)
{
    switch (bd){
    case AOO_PCM_INT16:
        return 2;
    case AOO_PCM_INT24:
        return 3;
    case AOO_PCM_FLOAT32:
        return 4;
    case AOO_PCM_FLOAT64:
        return 8;
    default:
        assert(false);
        return 0;
    }
}

void sample_to_int16(aoo_sample in, char *out)
{
    convert c;
    int32_t temp = in * 0x7fff + 0.5f;
    c.i16 = (temp > INT16_MAX) ? INT16_MAX : (temp < INT16_MIN) ? INT16_MIN : temp;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(out, c.b, 2); // optimized away
#else
    out[0] = c.b[1];
    out[1] = c.b[0];
#endif
}

void sample_to_int24(aoo_sample in, char *out)
{
    convert c;
    int32_t temp = in * 0x7fffffff + 0.5f;
    c.i32 = (temp > INT32_MAX) ? INT32_MAX : (temp < INT32_MIN) ? INT32_MIN : temp;
    // only copy the highest 3 bytes!
#if BYTE_ORDER == BIG_ENDIAN
    out[0] = c.b[0];
    out[1] = c.b[1];
    out[2] = c.b[2];
#else
    out[0] = c.b[3];
    out[1] = c.b[2];
    out[2] = c.b[1];
#endif
}

void sample_to_float32(aoo_sample in, char *out)
{
    aoo::to_bytes<float>(in, out);
}

void sample_to_float64(aoo_sample in, char *out)
{
    aoo::to_bytes<double>(in, out);
}

aoo_sample int16_to_sample(const char *in){
    convert c;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(c.b, in, 2); // optimized away
#else
    c.b[0] = in[1];
    c.b[1] = in[0];
#endif
    return(aoo_sample)c.i16 / 32768.f;
}

aoo_sample int24_to_sample(const char *in)
{
    convert c;
    // copy to the highest 3 bytes!
#if BYTE_ORDER == BIG_ENDIAN
    c.b[0] = in[0];
    c.b[1] = in[1];
    c.b[2] = in[2];
    c.b[3] = 0;
#else
    c.b[0] = 0;
    c.b[1] = in[2];
    c.b[2] = in[1];
    c.b[3] = in[0];
#endif
    return (aoo_sample)c.i32 / 0x7fffffff;
}

aoo_sample float32_to_sample(const char *in)
{
    return aoo::from_bytes<float>(in);
}

aoo_sample float64_to_sample(const char *in)
{
    return aoo::from_bytes<double>(in);
}

void print_settings(const aoo_format_pcm& f)
{
    LOG_VERBOSE("PCM settings: "
                << "nchannels = " << f.header.nchannels
                << ", blocksize = " << f.header.blocksize
                << ", samplerate = " << f.header.samplerate
                << ", bitdepth = " << bytes_per_sample(f.bitdepth));
}

/*//////////////////// codec //////////////////////////*/

struct codec {
    codec(){
        memset(&format, 0, sizeof(aoo_format_pcm));
    }
    aoo_format_pcm format;
};

aoo_error setformat(void *enc, aoo_format *f)
{
    if (strcmp(f->codec, AOO_CODEC_PCM)){
        return AOO_ERROR_UNSPECIFIED;
    }
    if (f->size < sizeof(aoo_format_pcm)){
        return AOO_ERROR_UNSPECIFIED;
    }
    auto c = static_cast<codec *>(enc);
    auto fmt = reinterpret_cast<aoo_format_pcm *>(f);

    fmt->header.codec = AOO_CODEC_PCM; // static string!
    fmt->header.size = sizeof(aoo_format_pcm); // actual size!

    // validate blocksize
    if (fmt->header.blocksize <= 0){
        LOG_WARNING("PCM: bad blocksize " << fmt->header.blocksize
                    << ", using 64 samples");
        fmt->header.blocksize = 64;
    }
    // validate samplerate
    if (fmt->header.samplerate <= 0){
        LOG_WARNING("PCM: bad samplerate " << fmt->header.samplerate
                    << ", using 44100");
        fmt->header.samplerate = 44100;
    }
    // validate channels
    if (fmt->header.nchannels <= 0 || fmt->header.nchannels > 255){
        LOG_WARNING("PCM: bad channel count " << fmt->header.nchannels
                    << ", using 1 channel");
        fmt->header.nchannels = 1;
    }
    // validate bitdepth
    if (fmt->bitdepth < 0 || fmt->bitdepth > AOO_PCM_BITDEPTH_SIZE){
        LOG_WARNING("PCM: bad bitdepth, using 32bit float");
        fmt->bitdepth = AOO_PCM_FLOAT32;
    }

    // save and print settings
    memcpy(&c->format, fmt, sizeof(aoo_format_pcm));
    print_settings(c->format);

    return AOO_ERROR_OK;
}

aoo_error getformat(void *x, aoo_format *f)
{
    auto c = static_cast<codec *>(x);
    // check if format has been set
    if (c->format.header.codec){
        if (f->size >= c->format.header.size){
            memcpy(f, &c->format, sizeof(aoo_format_pcm));
            return AOO_ERROR_OK;
        } else {
            return AOO_ERROR_UNSPECIFIED;
        }
    } else {
        return AOO_ERROR_UNSPECIFIED;
    }
}

void *encoder_new(){
    return new codec;
}

void encoder_free(void *enc){
    delete (codec *)enc;
}

aoo_error encode(void *enc,
                 const aoo_sample *s, int32_t n,
                 char *buf, int32_t *size)
{
    auto bitdepth = static_cast<codec *>(enc)->format.bitdepth;
    auto samplesize = bytes_per_sample(bitdepth);
    auto nbytes = samplesize * n;

    if (*size < nbytes){
        return AOO_ERROR_UNSPECIFIED;
    }

    auto samples_to_blob = [&](auto fn){
        auto b = buf;
        for (int i = 0; i < n; ++i){
            fn(s[i], b);
            b += samplesize;
        }
    };

    switch (bitdepth){
    case AOO_PCM_INT16:
        samples_to_blob(sample_to_int16);
        break;
    case AOO_PCM_INT24:
        samples_to_blob(sample_to_int24);
        break;
    case AOO_PCM_FLOAT32:
        samples_to_blob(sample_to_float32);
        break;
    case AOO_PCM_FLOAT64:
        samples_to_blob(sample_to_float64);
        break;
    default:
        // unknown bitdepth
        break;
    }

    *size = nbytes;

    return AOO_ERROR_OK;
}

void *decoder_new(){
    return new codec;
}

void decoder_free(void *dec){
    delete (codec *)dec;
}

aoo_error decode(void *dec,
                 const char *buf, int32_t size,
                 aoo_sample *s, int32_t *n)
{
    auto c = static_cast<codec *>(dec);
    assert(c->format.header.blocksize != 0);

    if (!buf){
        for (int i = 0; i < *n; ++i){
            s[i] = 0;
        }
        return AOO_ERROR_OK; // dropped block
    }

    auto samplesize = bytes_per_sample(c->format.bitdepth);
    auto nsamples = size / samplesize;

    if (*n < nsamples){
        return AOO_ERROR_UNSPECIFIED;
    }

    auto blob_to_samples = [&](auto convfn){
        auto b = buf;
        for (int i = 0; i < *n; ++i, b += samplesize){
            s[i] = convfn(b);
        }
    };

    switch (c->format.bitdepth){
    case AOO_PCM_INT16:
        blob_to_samples(int16_to_sample);
        break;
    case AOO_PCM_INT24:
        blob_to_samples(int24_to_sample);
        break;
    case AOO_PCM_FLOAT32:
        blob_to_samples(float32_to_sample);
        break;
    case AOO_PCM_FLOAT64:
        blob_to_samples(float64_to_sample);
        break;
    default:
        // unknown bitdepth
        return AOO_ERROR_UNSPECIFIED;
    }

    *n = nsamples;

    return AOO_ERROR_OK;
}

aoo_error serialize(const aoo_format *f, char *buf, int32_t *size)
{
    if (*size >= 4){
        auto fmt = (const aoo_format_pcm *)f;
        aoo::to_bytes<int32_t>(fmt->bitdepth, buf);
        *size = 4;

        return AOO_ERROR_OK;
    } else {
        LOG_ERROR("PCM: couldn't write settings - buffer too small!");
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error deserialize(const aoo_format *header, const char *buf,
                      int32_t size, aoo_format *f)
{
    if (size < 4){
        LOG_ERROR("PCM: couldn't read format - not enough data!");
        return AOO_ERROR_UNSPECIFIED;
    }
    if (f->size < sizeof(aoo_format_pcm)){
        LOG_ERROR("PCM: output format storage too small");
        return AOO_ERROR_UNSPECIFIED;
    }
    auto fmt = (aoo_format_pcm *)f;
    // header
    fmt->header.codec = AOO_CODEC_PCM; // static string!
    fmt->header.size = sizeof(aoo_format_pcm); // actual size!
    fmt->header.blocksize = header->blocksize;
    fmt->header.nchannels = header->nchannels;
    fmt->header.samplerate = header->samplerate;
    // options
    fmt->bitdepth = (aoo_pcm_bitdepth)aoo::from_bytes<int32_t>(buf);

    return AOO_ERROR_OK;
}

aoo_codec codec_class = {
    AOO_CODEC_PCM,
    // encoder
    encoder_new,
    encoder_free,
    setformat,
    getformat,
    encode,
    // decoder
    decoder_new,
    decoder_free,
    setformat,
    getformat,
    decode,
    // helper
    serialize,
    deserialize
};

} // namespace

void aoo_codec_pcm_setup(aoo_codec_registerfn fn){
    fn(AOO_CODEC_PCM, &codec_class);
}
