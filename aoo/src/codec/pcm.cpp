/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo/codec/aoo_pcm.h"
#include "aoo/aoo_codec.h"
#include "common/utils.hpp"

#include <cassert>
#include <cstring>

namespace {

AooAllocator g_allocator {
    [](AooSize n, void *){ return operator new(n); },
    nullptr,
    [](void *ptr, AooSize, void *){ operator delete(ptr); },
    nullptr
};

// conversion routines between AooSample and PCM data
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
    case kAooPcmInt16:
        return 2;
    case kAooPcmInt24:
        return 3;
    case kAooPcmFloat32:
        return 4;
    case kAooPcmFloat64:
        return 8;
    default:
        assert(false);
        return 0;
    }
}

void sample_to_int16(AooSample in, AooByte *out)
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

void sample_to_int24(AooSample in, AooByte *out)
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

void sample_to_float32(AooSample in, AooByte *out)
{
    aoo::to_bytes<float>(in, out);
}

void sample_to_float64(AooSample in, AooByte *out)
{
    aoo::to_bytes<double>(in, out);
}

AooSample int16_to_sample(const AooByte *in){
    convert c;
#if BYTE_ORDER == BIG_ENDIAN
    memcpy(c.b, in, 2); // optimized away
#else
    c.b[0] = in[1];
    c.b[1] = in[0];
#endif
    return(AooSample)c.i16 / 32768.f;
}

AooSample int24_to_sample(const AooByte *in)
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
    return (AooSample)c.i32 / 0x7fffffff;
}

AooSample float32_to_sample(const AooByte *in)
{
    return aoo::from_bytes<float>(in);
}

AooSample float64_to_sample(const AooByte *in)
{
    return aoo::from_bytes<double>(in);
}

void print_settings(const AooFormatPcm& f)
{
    LOG_VERBOSE("PCM settings: "
                << "nchannels = " << f.header.numChannels
                << ", blocksize = " << f.header.blockSize
                << ", samplerate = " << f.header.sampleRate
                << ", bitdepth = " << bytes_per_sample(f.bitDepth));
}

/*//////////////////// codec //////////////////////////*/

struct codec {
    codec(){
        memset(&format, 0, sizeof(AooFormatPcm));
    }
    AooFormatPcm format;
};

void validate_format(AooFormatPcm& f, bool loud = true)
{
    f.header.codec = kAooCodecPcm; // static string!
    f.header.size = sizeof(AooFormatPcm); // actual size!

    // validate block size
    if (f.header.blockSize <= 0){
        if (loud){
            LOG_WARNING("PCM: bad blocksize " << f.header.blockSize
                        << ", using 64 samples");
        }
        f.header.blockSize = 64;
    }
    // validate sample rate
    if (f.header.sampleRate <= 0){
        if (loud){
            LOG_WARNING("PCM: bad samplerate " << f.header.sampleRate
                        << ", using 44100");
        }
        f.header.sampleRate = 44100;
    }
    // validate channels
    if (f.header.numChannels <= 0 || f.header.numChannels > 255){
        if (loud){
            LOG_WARNING("PCM: bad channel count " << f.header.numChannels
                        << ", using 1 channel");
        }
        f.header.numChannels = 1;
    }
    // validate bitdepth
    if (f.bitDepth < 0 || f.bitDepth > kAooPcmBitDepthSize){
        if (loud){
            LOG_WARNING("PCM: bad bit depth, using 32bit float");
        }
        f.bitDepth = kAooPcmFloat32;
    }
}

AooError compare(codec *c, const AooFormatPcm *fmt)
{
    // copy and validate!
    AooFormatPcm f1;
    memcpy(&f1, fmt, sizeof(AooFormatPcm));

    auto& f2 = c->format;
    auto& h1 = f1.header;
    auto& h2 = f2.header;

    // check before validate()!
    if (strcmp(h1.codec, h2.codec) ||
            h1.size != h2.size) {
        return false;
    }

    validate_format(f1, false);

    return h1.blockSize == h2.blockSize &&
            h1.sampleRate == h2.sampleRate &&
            h1.numChannels == h2.numChannels &&
            f1.bitDepth == f2.bitDepth;
}

AooError set_format(codec *c, AooFormatPcm *fmt)
{
    if (strcmp(fmt->header.codec, kAooCodecPcm)){
        return kAooErrorUnknown;
    }
    if (fmt->header.size < sizeof(AooFormatPcm)){
        return kAooErrorUnknown;
    }

    validate_format(*fmt);

    // save and print settings
    memcpy(&c->format, fmt, sizeof(AooFormatPcm));
    print_settings(c->format);

    return kAooOk;
}

AooError get_format(codec *c, AooFormat *f, AooInt32 size)
{
    // check if format has been set
    if (c->format.header.codec){
        if (size >= c->format.header.size){
            memcpy(f, &c->format, sizeof(AooFormatPcm));
            return kAooOk;
        } else {
            return kAooErrorUnknown;
        }
    } else {
        return kAooErrorUnknown;
    }
}

AooError pcm_ctl(void *x, AooCtl ctl, void *ptr, AooSize size){
    switch (ctl){
    case kAooCodecCtlSetFormat:
        assert(size >= sizeof(AooFormat));
        return set_format((codec *)x, (AooFormatPcm *)ptr);
    case kAooCodecCtlGetFormat:
        return get_format((codec *)x, (AooFormat *)ptr, size);
    case kAooCodecCtlReset:
        // no op
        return kAooOk;
    case kAooCodecCtlFormatEqual:
        assert(size >= sizeof(AooFormat));
        return compare((codec *)x, (AooFormatPcm *)ptr);
    default:
        LOG_WARNING("PCM: unsupported codec ctl " << ctl);
        return kAooErrorUnknown;
    }
}

void *codec_new(AooError *err){
    auto obj = g_allocator.alloc(sizeof(codec), g_allocator.context);
    new (obj) codec {};
    return obj;
}

void codec_free(void *enc){
    static_cast<codec *>(enc)->~codec();
    g_allocator.free(enc, sizeof(codec), g_allocator.context);
}

AooError encode(void *enc,
                const AooSample *s, AooInt32 n,
                AooByte *buf, AooInt32 *size)
{
    auto bitdepth = static_cast<codec *>(enc)->format.bitDepth;
    auto samplesize = bytes_per_sample(bitdepth);
    auto nbytes = samplesize * n;

    if (*size < nbytes){
        LOG_WARNING("PCM: size mismatch! input bytes: "
                    << nbytes << ", output bytes " << *size);
        return kAooErrorUnknown;
    }

    auto samples_to_blob = [&](auto fn){
        auto b = buf;
        for (int i = 0; i < n; ++i){
            fn(s[i], b);
            b += samplesize;
        }
    };

    switch (bitdepth){
    case kAooPcmInt16:
        samples_to_blob(sample_to_int16);
        break;
    case kAooPcmInt24:
        samples_to_blob(sample_to_int24);
        break;
    case kAooPcmFloat32:
        samples_to_blob(sample_to_float32);
        break;
    case kAooPcmFloat64:
        samples_to_blob(sample_to_float64);
        break;
    default:
        // unknown bitdepth
        break;
    }

    *size = nbytes;

    return kAooOk;
}

AooError decode(void *dec,
                const AooByte *buf, AooInt32 size,
                AooSample *s, AooInt32 *n)
{
    auto c = static_cast<codec *>(dec);
    assert(c->format.header.blockSize != 0);

    if (!buf){
        for (int i = 0; i < *n; ++i){
            s[i] = 0;
        }
        return kAooOk; // dropped block
    }

    auto samplesize = bytes_per_sample(c->format.bitDepth);
    auto nsamples = size / samplesize;

    if (*n < nsamples){
        LOG_WARNING("PCM: size mismatch! input samples: "
                    << nsamples << ", output samples " << *n);
        return kAooErrorUnknown;
    }

    auto blob_to_samples = [&](auto convfn){
        auto b = buf;
        for (int i = 0; i < *n; ++i, b += samplesize){
            s[i] = convfn(b);
        }
    };

    switch (c->format.bitDepth){
    case kAooPcmInt16:
        blob_to_samples(int16_to_sample);
        break;
    case kAooPcmInt24:
        blob_to_samples(int24_to_sample);
        break;
    case kAooPcmFloat32:
        blob_to_samples(float32_to_sample);
        break;
    case kAooPcmFloat64:
        blob_to_samples(float64_to_sample);
        break;
    default:
        // unknown bitdepth
        return kAooErrorUnknown;
    }

    *n = nsamples;

    return kAooOk;
}

AooError serialize(const AooFormat *f, AooByte *buf, AooInt32 *size)
{
    if (*size >= 4){
        auto fmt = (const AooFormatPcm *)f;
        aoo::to_bytes<AooPcmBitDepth>(fmt->bitDepth, buf);
        *size = 4;

        return kAooOk;
    } else {
        LOG_ERROR("PCM: couldn't write settings - buffer too small!");
        return kAooErrorUnknown;
    }
}

AooError deserialize(const AooFormat *header, const AooByte *buf,
                     AooInt32 size, AooFormat *f, AooInt32 fmtsize)
{
    if (size < 4){
        LOG_ERROR("PCM: couldn't read format - not enough data!");
        return kAooErrorUnknown;
    }
    if (fmtsize < sizeof(AooFormatPcm)){
        LOG_ERROR("PCM: output format storage too small");
        return kAooErrorUnknown;
    }
    auto fmt = (AooFormatPcm *)f;
    // header
    fmt->header.codec = kAooCodecPcm; // static string!
    fmt->header.size = sizeof(AooFormatPcm); // actual size!
    fmt->header.blockSize = header->blockSize;
    fmt->header.numChannels = header->numChannels;
    fmt->header.sampleRate = header->sampleRate;
    // options
    fmt->bitDepth = aoo::from_bytes<AooPcmBitDepth>(buf);

    return kAooOk;
}

AooCodecInterface interface = {
    // encoder
    codec_new,
    codec_free,
    pcm_ctl,
    encode,
    // decoder
    codec_new,
    codec_free,
    pcm_ctl,
    decode,
    // helper
    serialize,
    deserialize,
    nullptr
};

} // namespace

void aoo_pcmCodecSetup(AooCodecRegisterFunc fn,
                       AooLogFunc log, const AooAllocator *alloc){
    if (alloc){
        g_allocator = *alloc;
    }
    fn(kAooCodecPcm, &interface);
}

