/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo/codec/aoo_opus.h"
#include "aoo/aoo_codec.h"
#include "common/utils.hpp"

#include <cassert>
#include <cstring>
#include <memory>

namespace {

AooAllocator g_allocator {
    [](AooSize n, void *){ return operator new(n); },
    nullptr,
    [](void *ptr, AooSize, void *){ operator delete(ptr); },
    nullptr
};

void *allocate(AooSize n){
    return g_allocator.alloc(n, g_allocator.context);
}

void deallocate(void *ptr, AooSize n){
    g_allocator.free(ptr, n, g_allocator.context);
}

void print_settings(const AooFormatOpus& f){
    const char *application, *type;

    switch (f.applicationType){
    case OPUS_APPLICATION_VOIP:
        application = "VOIP";
        break;
    case OPUS_APPLICATION_RESTRICTED_LOWDELAY:
        application = "low delay";
        break;
    default:
        application = "audio";
        break;
    }

    switch (f.signalType){
    case OPUS_SIGNAL_MUSIC:
        type = "music";
        break;
    case OPUS_SIGNAL_VOICE:
        type = "voice";
        break;
    default:
        type = "auto";
        break;
    }

    LOG_VERBOSE("Opus settings: "
                << "nchannels = " << f.header.numChannels
                << ", blocksize = " << f.header.blockSize
                << ", samplerate = " << f.header.sampleRate
                << ", application = " << application
                << ", bitrate = " << f.bitrate
                << ", complexity = " << f.complexity
                << ", signal type = " << type);
}

/*/////////////////////// codec base ////////////////////////*/

struct codec {
    codec(){
        memset(&format, 0, sizeof(format));
    }
    AooFormatOpus format;
};

void validate_format(AooFormatOpus& f, bool loud = true)
{
    f.header.codec = kAooCodecOpus; // static string!
    f.header.size = sizeof(AooFormatOpus); // actual size!
    // validate samplerate
    switch (f.header.sampleRate){
    case 8000:
    case 12000:
    case 16000:
    case 24000:
    case 48000:
        break;
    default:
        if (loud){
            LOG_VERBOSE("Opus: samplerate " << f.header.sampleRate
                        << " not supported - using 48000");
        }
        f.header.sampleRate = 48000;
        break;
    }
    // validate channels (LATER support multichannel!)
    if (f.header.numChannels < 1 || f.header.numChannels > 255){
        if (loud){
            LOG_WARNING("Opus: channel count " << f.header.numChannels <<
                        " out of range - using 1 channels");
        }
        f.header.numChannels = 1;
    }
    // validate blocksize
    const int minblocksize = f.header.sampleRate / 400; // 2.5 ms (e.g. 120 samples @ 48 kHz)
    const int maxblocksize = minblocksize * 24; // 60 ms (e.g. 2880 samples @ 48 kHz)
    int blocksize = f.header.blockSize;
    if (blocksize <= minblocksize){
        f.header.blockSize = minblocksize;
    } else if (blocksize >= maxblocksize){
        f.header.blockSize = maxblocksize;
    } else {
        // round down to nearest multiple of 2.5 ms (in power of 2 steps)
        int result = minblocksize;
        while (result <= blocksize){
            result *= 2;
        }
        f.header.blockSize = result / 2;
    }
    // validate application type
    if (f.applicationType != OPUS_APPLICATION_VOIP
            && f.applicationType != OPUS_APPLICATION_AUDIO
            && f.applicationType != OPUS_APPLICATION_RESTRICTED_LOWDELAY)
    {
        if (loud){
            LOG_WARNING("Opus: bad application type, using OPUS_APPLICATION_AUDIO");
        }
        f.applicationType = OPUS_APPLICATION_AUDIO;
    }
    // bitrate, complexity and signal type should be validated by opus
}

AooError compare(codec *c, const AooFormatOpus *fmt)
{
    // copy and validate!
    AooFormatOpus f1;
    memcpy(&f1, fmt, sizeof(AooFormatOpus));

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
            f1.applicationType == f2.applicationType &&
            f1.bitrate == f2.bitrate &&
            f1.complexity == f2.complexity &&
            f1.signalType == f2.signalType;
}

AooError get_format(codec *c, AooFormat *f, AooInt32 size)
{
    // check if format has been set
    if (c->format.header.codec){
        if (size >= c->format.header.size){
            memcpy(f, &c->format, sizeof(AooFormatOpus));
            return kAooOk;
        } else {
            return kAooErrorUnknown;
        }
    } else {
        return kAooErrorUnknown;
    }
}

/*/////////////////////////// encoder //////////////////////*/

struct encoder : codec {
    ~encoder(){
        if (state){
            deallocate(state, size);
        }
    }
    OpusMSEncoder *state = nullptr;
    size_t size = 0;
};

void *encoder_new(AooError *err){
    auto obj = allocate(sizeof(encoder));
    new (obj) encoder {};
    return obj;
}

void encoder_free(void *enc){
    static_cast<encoder *>(enc)->~encoder();
    deallocate(enc, sizeof(encoder));
}

AooError encode(void *enc,
                const AooSample *s, AooInt32 n,
                AooByte *buf, AooInt32 *size)
{
    auto c = static_cast<encoder *>(enc);
    if (c->state){
        auto framesize = n / c->format.header.numChannels;
        auto result = opus_multistream_encode_float(
            c->state, s, framesize, (unsigned char *)buf, *size);
        if (result > 0){
            *size = result;
            return kAooOk;
        } else {
            LOG_VERBOSE("Opus: opus_encode_float() failed with error code " << result);
        }
    }
    return kAooErrorUnknown;
}

AooError encoder_set_format(encoder *c, AooFormatOpus *f){
    if (strcmp(f->header.codec, kAooCodecOpus)){
        return kAooErrorUnknown;
    }
    if (f->header.size < sizeof(AooFormatOpus)){
        return kAooErrorUnknown;
    }

    validate_format(*f);

    // LATER only deallocate if channels, sr and application type
    // have changed, otherwise simply reset the encoder.
    if (c->state){
        deallocate(c->state, c->size);
        c->state = nullptr;
        c->size = 0;
    }
    // setup channel mapping
    // only use decoupled streams (what's the point of coupled streams?)
    auto nchannels = f->header.numChannels;
    unsigned char mapping[256];
    for (int i = 0; i < nchannels; ++i){
        mapping[i] = i;
    }
    memset(mapping + nchannels, 255, 256 - nchannels);
    // create state
    size_t size = opus_multistream_encoder_get_size(nchannels, 0);
    auto state = (OpusMSEncoder *)allocate(size);
    if (!state){
        return kAooErrorUnknown;
    }
    auto error = opus_multistream_encoder_init(state, f->header.sampleRate,
        nchannels, nchannels, 0, mapping, f->applicationType);
    if (error != OPUS_OK){
        LOG_ERROR("Opus: opus_encoder_create() failed with error code " << error);
        return kAooErrorUnknown;
    }
    c->state = state;
    c->size = size;
    // apply settings
    // complexity
    opus_multistream_encoder_ctl(c->state, OPUS_SET_COMPLEXITY(f->complexity));
    opus_multistream_encoder_ctl(c->state, OPUS_GET_COMPLEXITY(&f->complexity));
    // bitrate
    opus_multistream_encoder_ctl(c->state, OPUS_SET_BITRATE(f->bitrate));
#if 0
    // This control is broken in opus_multistream_encoder (as of opus v1.3.2)
    // because it would always return the default bitrate.
    // The only thing we can do is omit the function and just keep the input value.
    // This means that clients have to explicitly check for OPUS_AUTO and
    // OPUS_BITRATE_MAX when reading the 'bitrate' value after encoder_setformat().
    opus_multistream_encoder_ctl(c->state, OPUS_GET_BITRATE(&f->bitrate));
#endif
    // signal type
    opus_multistream_encoder_ctl(c->state, OPUS_SET_SIGNAL(f->signalType));
    opus_multistream_encoder_ctl(c->state, OPUS_GET_SIGNAL(&f->signalType));

    // save and print settings
    memcpy(&c->format, f, sizeof(AooFormatOpus));
    print_settings(c->format);

    return kAooOk;
}

AooError encoder_ctl(void *x, AooCtl ctl, void *ptr, AooSize size){
    switch (ctl){
    case kAooCodecCtlSetFormat:
        assert(size >= sizeof(AooFormat));
        return encoder_set_format((encoder *)x, (AooFormatOpus *)ptr);
    case kAooCodecCtlGetFormat:
        return get_format((codec *)x, (AooFormat *)ptr, size);
    case kAooCodecCtlReset:
        if (opus_multistream_encoder_ctl(static_cast<encoder *>(x)->state,
                                         OPUS_RESET_STATE) == OPUS_OK) {
            return kAooOk;
        } else {
            return kAooErrorUnknown;
        }
    case kAooCodecCtlFormatEqual:
        assert(size >= sizeof(AooFormat));
        return compare((codec *)x, (const AooFormatOpus *)ptr);
    default:
        LOG_WARNING("Opus: unsupported codec ctl " << ctl);
        return kAooErrorUnknown;
    }
}

/*/////////////////////// decoder ///////////////////////////*/

struct decoder : codec {
    ~decoder(){
        if (state){
            deallocate(state, size);
        }
    }
    OpusMSDecoder * state = nullptr;
    size_t size = 0;
};

void *decoder_new(AooError *err){
    auto obj = allocate(sizeof(decoder));
    new (obj) decoder {};
    return obj;
}

void decoder_free(void *dec){
    static_cast<decoder *>(dec)->~decoder();
    deallocate(dec, sizeof(decoder));
}

AooError decode(void *dec,
                const AooByte *buf, AooInt32 size,
                AooSample *s, AooInt32 *n)
{
    auto c = static_cast<decoder *>(dec);
    if (c->state){
        auto framesize = *n / c->format.header.numChannels;
        auto result = opus_multistream_decode_float(
            c->state, (const unsigned char *)buf, size, s, framesize, 0);
        if (result > 0){
            *n = result;
            return kAooOk;
        } else if (result < 0) {
            LOG_VERBOSE("Opus: opus_decode_float() failed with error code " << result);
        }
    }
    return kAooErrorUnknown;
}

AooError decoder_set_format(decoder *c, AooFormat *f)
{
    if (strcmp(f->codec, kAooCodecOpus)){
        return kAooErrorUnknown;
    }
    if (f->size < sizeof(AooFormatOpus)){
        return kAooErrorUnknown;
    }

    auto fmt = reinterpret_cast<AooFormatOpus *>(f);

    validate_format(*fmt);

    // LATER only deallocate if channels and sr have changed,
    // otherwise simply reset the decoder.
    if (c->state){
        deallocate(c->state, c->size);
        c->state = nullptr;
        c->size = 0;
    }
    // setup channel mapping
    // only use decoupled streams (what's the point of coupled streams?)
    auto nchannels = fmt->header.numChannels;
    unsigned char mapping[256];
    for (int i = 0; i < nchannels; ++i){
        mapping[i] = i;
    }
    memset(mapping + nchannels, 255, 256 - nchannels);
    // create state
    size_t size = opus_multistream_decoder_get_size(nchannels, 0);
    auto state = (OpusMSDecoder *)allocate(size);
    if (!state){
        return kAooErrorUnknown;
    }
    auto error = opus_multistream_decoder_init(state, fmt->header.sampleRate,
                                               nchannels, nchannels, 0, mapping);
    if (error != OPUS_OK){
        LOG_ERROR("Opus: opus_decoder_create() failed with error code " << error);
        return kAooErrorUnknown;
    }
    c->state = state;
    c->size = size;
    // these are actually encoder settings and don't do anything on the decoder
#if 0
    // complexity
    opus_multistream_decoder_ctl(c->state, OPUS_SET_COMPLEXITY(f->complexity));
    opus_multistream_decoder_ctl(c->state, OPUS_GET_COMPLEXITY(&f->complexity));
    // bitrate
    opus_multistream_decoder_ctl(c->state, OPUS_SET_BITRATE(f->bitrate));
    opus_multistream_decoder_ctl(c->state, OPUS_GET_BITRATE(&f->bitrate));
    // signal type
    opus_multistream_decoder_ctl(c->state, OPUS_SET_SIGNAL(f->signalType));
    opus_multistream_decoder_ctl(c->state, OPUS_GET_SIGNAL(&f->signalType));
#endif

    // save and print settings
    memcpy(&c->format, fmt, sizeof(AooFormatOpus));
    print_settings(c->format);

    return kAooOk;
}

AooError decoder_ctl(void *x, AooCtl ctl, void *ptr, AooSize size){
    switch (ctl){
    case kAooCodecCtlSetFormat:
        assert(size >= sizeof(AooFormat));
        return decoder_set_format((decoder *)x, (AooFormat *)ptr);
    case kAooCodecCtlGetFormat:
        return get_format((decoder *)x, (AooFormat *)ptr, size);
    case kAooCodecCtlReset:
        if (opus_multistream_decoder_ctl(static_cast<decoder *>(x)->state,
                                         OPUS_RESET_STATE) == OPUS_OK) {
            return kAooOk;
        } else {
            return kAooErrorUnknown;
        }
    case kAooCodecCtlFormatEqual:
        assert(size >= sizeof(AooFormat));
        return compare((codec *)x, (const AooFormatOpus *)ptr);
    default:
        LOG_WARNING("Opus: unsupported codec ctl " << ctl);
        return kAooErrorUnknown;
    }
}

/*////////////////////// codec ////////////////////*/

AooError serialize(const AooFormat *f, AooByte *buf, AooInt32 *size){
    if (*size >= 16){
        auto fmt = (const AooFormatOpus *)f;
        aoo::to_bytes<opus_int32>(fmt->applicationType, buf);
        aoo::to_bytes<opus_int32>(fmt->bitrate, buf + 4);
        aoo::to_bytes<opus_int32>(fmt->complexity, buf + 8);
        aoo::to_bytes<opus_int32>(fmt->signalType, buf + 12);
        *size = 16;

        return kAooOk;
    } else {
        LOG_WARNING("Opus: couldn't write settings");
        return kAooErrorUnknown;
    }
}

AooError deserialize(const AooFormat *header, const AooByte *buf,
                     AooInt32 size, AooFormat *format, AooInt32 fmtsize){
    if (size < 16){
        LOG_ERROR("Opus: couldn't read format - not enough data!");
        return kAooErrorUnknown;
    }
    if (fmtsize < sizeof(AooFormatOpus)){
        LOG_ERROR("Opus: output format storage too small");
        return kAooErrorUnknown;
    }
    auto fmt = (AooFormatOpus *)format;
    // header
    fmt->header.codec = kAooCodecOpus; // static string!
    fmt->header.size = sizeof(AooFormatOpus); // actual size!
    fmt->header.blockSize = header->blockSize;
    fmt->header.numChannels = header->numChannels;
    fmt->header.sampleRate = header->sampleRate;
    // options
    fmt->applicationType = aoo::from_bytes<opus_int32>(buf);
    fmt->bitrate = aoo::from_bytes<opus_int32>(buf + 4);
    fmt->complexity = aoo::from_bytes<opus_int32>(buf + 8);
    fmt->signalType = aoo::from_bytes<opus_int32>(buf + 12);

    return kAooOk;
}

AooCodecInterface interface = {
    // encoder
    encoder_new,
    encoder_free,
    encoder_ctl,
    encode,
    // decoder
    decoder_new,
    decoder_free,
    decoder_ctl,
    decode,
    // helper
    serialize,
    deserialize,
    nullptr
};

} // namespace

void aoo_opusCodecSetup(AooCodecRegisterFunc fn,
                        AooLogFunc log, const AooAllocator *alloc){
    if (alloc){
        g_allocator = *alloc;
    }
    fn(kAooCodecOpus, &interface);
}

