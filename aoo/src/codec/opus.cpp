/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo/codec/aoo_opus.h"
#include "common/utils.hpp"

#include <cassert>
#include <cstring>
#include <memory>

namespace {

void print_settings(const aoo_format_opus& f){
    const char *type;
    switch (f.signal_type){
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
                << "nchannels = " << f.header.nchannels
                << ", blocksize = " << f.header.blocksize
                << ", samplerate = " << f.header.samplerate
                << ", bitrate = " << f.bitrate
                << ", complexity = " << f.complexity
                << ", signal type = " << type);
}

/*/////////////////////// codec base ////////////////////////*/

struct codec {
    codec(){
        memset(&format, 0, sizeof(format));
    }
    aoo_format_opus format;
};

void validate_format(aoo_format_opus& f)
{
    f.header.codec = AOO_CODEC_OPUS; // static string!
    f.header.size = sizeof(aoo_format_opus); // actual size!
    // validate samplerate
    switch (f.header.samplerate){
    case 8000:
    case 12000:
    case 16000:
    case 24000:
    case 48000:
        break;
    default:
        LOG_VERBOSE("Opus: samplerate " << f.header.samplerate
                    << " not supported - using 48000");
        f.header.samplerate = 48000;
        break;
    }
    // validate channels (LATER support multichannel!)
    if (f.header.nchannels < 1 || f.header.nchannels > 255){
        LOG_WARNING("Opus: channel count " << f.header.nchannels <<
                    " out of range - using 1 channels");
        f.header.nchannels = 1;
    }
    // validate blocksize
    const int minblocksize = f.header.samplerate / 400; // 2.5 ms (e.g. 120 samples @ 48 kHz)
    const int maxblocksize = minblocksize * 24; // 60 ms (e.g. 2880 samples @ 48 kHz)
    int blocksize = f.header.blocksize;
    if (blocksize <= minblocksize){
        f.header.blocksize = minblocksize;
    } else if (blocksize >= maxblocksize){
        f.header.blocksize = maxblocksize;
    } else {
        // round down to nearest multiple of 2.5 ms (in power of 2 steps)
        int result = minblocksize;
        while (result <= blocksize){
            result *= 2;
        }
        f.header.blocksize = result / 2;
    }
    // bitrate, complexity and signal type should be validated by opus
}

aoo_error getformat(void *x, aoo_format *f)
{
    auto c = static_cast<codec *>(x);
    if (c->format.header.codec){
        if (f->size >= c->format.header.size){
            memcpy(f, &c->format, sizeof(aoo_format_opus));
            return AOO_ERROR_OK;
        } else {
            return AOO_ERROR_UNSPECIFIED;
        }
    } else {
        return AOO_ERROR_UNSPECIFIED;
    }
}

/*/////////////////////////// encoder //////////////////////*/

struct encoder : codec {
    ~encoder(){
        if (state){
            opus_multistream_encoder_destroy(state);
        }
    }
    OpusMSEncoder *state = nullptr;
};

void *encoder_new(){
    return new encoder;
}

void encoder_free(void *enc){
    delete (encoder *)enc;
}

aoo_error encoder_encode(void *enc,
                         const aoo_sample *s, int32_t n,
                         char *buf, int32_t *size)
{
    auto c = static_cast<encoder *>(enc);
    if (c->state){
        auto framesize = n / c->format.header.nchannels;
        auto result = opus_multistream_encode_float(
            c->state, s, framesize, (unsigned char *)buf, *size);
        if (result > 0){
            *size = result;
            return AOO_ERROR_OK;
        } else {
            LOG_VERBOSE("Opus: opus_encode_float() failed with error code " << result);
        }
    }
    return AOO_ERROR_UNSPECIFIED;
}

aoo_error encoder_setformat(void *enc, aoo_format *f){
    if (strcmp(f->codec, AOO_CODEC_OPUS)){
        return AOO_ERROR_UNSPECIFIED;
    }
    if (f->size < sizeof(aoo_format_opus)){
        return AOO_ERROR_UNSPECIFIED;
    }

    auto c = static_cast<encoder *>(enc);
    auto fmt = reinterpret_cast<aoo_format_opus *>(f);

    validate_format(*fmt);

    int error = 0;
    if (c->state){
        opus_multistream_encoder_destroy(c->state);
    }
    // setup channel mapping
    // only use decoupled streams (what's the point of coupled streams?)
    auto nchannels = fmt->header.nchannels;
    unsigned char mapping[256];
    for (int i = 0; i < nchannels; ++i){
        mapping[i] = i;
    }
    memset(mapping + nchannels, 255, 256 - nchannels);
    // create state
    c->state = opus_multistream_encoder_create(fmt->header.samplerate,
                                       nchannels, nchannels, 0, mapping,
                                       OPUS_APPLICATION_AUDIO, &error);
    if (error == OPUS_OK){
        assert(c->state != nullptr);
        // apply settings
        // complexity
        opus_multistream_encoder_ctl(c->state, OPUS_SET_COMPLEXITY(fmt->complexity));
        opus_multistream_encoder_ctl(c->state, OPUS_GET_COMPLEXITY(&fmt->complexity));
        // bitrate
        opus_multistream_encoder_ctl(c->state, OPUS_SET_BITRATE(fmt->bitrate));
    #if 0
        // This control is broken in opus_multistream_encoder (as of opus v1.3.2)
        // because it would always return the default bitrate.
        // The only thing we can do is omit the function and just keep the input value.
        // This means that clients have to explicitly check for OPUS_AUTO and
        // OPUS_BITRATE_MAX when reading the 'bitrate' value after encoder_setformat().
        opus_multistream_encoder_ctl(c->state, OPUS_GET_BITRATE(&fmt->bitrate));
    #endif
        // signal type
        opus_multistream_encoder_ctl(c->state, OPUS_SET_SIGNAL(fmt->signal_type));
        opus_multistream_encoder_ctl(c->state, OPUS_GET_SIGNAL(&fmt->signal_type));
    } else {
        LOG_ERROR("Opus: opus_encoder_create() failed with error code " << error);
        return AOO_ERROR_UNSPECIFIED;
    }
    // save and print settings
    memcpy(&c->format, fmt, sizeof(aoo_format_opus));
    print_settings(c->format);

    return AOO_ERROR_OK;
}

#define encoder_getformat getformat

/*/////////////////////// decoder ///////////////////////////*/

struct decoder : codec {
    ~decoder(){
        if (state){
            opus_multistream_decoder_destroy(state);
        }
    }
    OpusMSDecoder * state = nullptr;
};

void *decoder_new(){
    return new decoder;
}

void decoder_free(void *dec){
    delete (decoder *)dec;
}

aoo_error decoder_decode(void *dec,
                         const char *buf, int32_t size,
                         aoo_sample *s, int32_t *n)
{
    auto c = static_cast<decoder *>(dec);
    if (c->state){
        auto framesize = *n / c->format.header.nchannels;
        auto result = opus_multistream_decode_float(
            c->state, (const unsigned char *)buf, size, s, framesize, 0);
        if (result > 0){
            *n = result;
            return AOO_ERROR_OK;
        } else {
            LOG_VERBOSE("Opus: opus_decode_float() failed with error code " << result);
        }
    }
    return AOO_ERROR_UNSPECIFIED;
}

aoo_error decoder_setformat(void *dec, aoo_format *f)
{
    if (strcmp(f->codec, AOO_CODEC_OPUS)){
        return AOO_ERROR_UNSPECIFIED;
    }
    if (f->size < sizeof(aoo_format_opus)){
        return AOO_ERROR_UNSPECIFIED;
    }

    auto c = static_cast<decoder *>(dec);
    auto fmt = reinterpret_cast<aoo_format_opus *>(f);

    validate_format(*fmt);

    if (c->state){
        opus_multistream_decoder_destroy(c->state);
    }
    // setup channel mapping
    // only use decoupled streams (what's the point of coupled streams?)
    auto nchannels = fmt->header.nchannels;
    unsigned char mapping[256];
    for (int i = 0; i < nchannels; ++i){
        mapping[i] = i;
    }
    memset(mapping + nchannels, 255, 256 - nchannels);
    // create state
    int error = 0;
    c->state = opus_multistream_decoder_create(fmt->header.samplerate,
        nchannels, nchannels, 0, mapping, &error);
    if (error == OPUS_OK){
        assert(c->state != nullptr);
        // these are actually encoder settings and do anything on the decoder
    #if 0
        // complexity
        opus_multistream_decoder_ctl(c->state, OPUS_SET_COMPLEXITY(fmt->complexity));
        opus_multistream_decoder_ctl(c->state, OPUS_GET_COMPLEXITY(&fmt->complexity));
        // bitrate
        opus_multistream_decoder_ctl(c->state, OPUS_SET_BITRATE(fmt->bitrate));
        opus_multistream_decoder_ctl(c->state, OPUS_GET_BITRATE(&fmt->bitrate));
        // signal type
        opus_multistream_decoder_ctl(c->state, OPUS_SET_SIGNAL(fmt->signal_type));
        opus_multistream_decoder_ctl(c->state, OPUS_GET_SIGNAL(&fmt->signal_type));
    #endif
        // save and print settings
        memcpy(&c->format, fmt, sizeof(aoo_format_opus));
        print_settings(c->format);
        return AOO_ERROR_OK;
    } else {
        LOG_ERROR("Opus: opus_decoder_create() failed with error code " << error);
        return AOO_ERROR_UNSPECIFIED;
    }
}

#define decoder_getformat getformat

/*////////////////////// codec ////////////////////*/

aoo_error serialize(const aoo_format *f, char *buf, int32_t *size){
    if (*size >= 12){
        auto fmt = (const aoo_format_opus *)f;
        aoo::to_bytes<int32_t>(fmt->bitrate, buf);
        aoo::to_bytes<int32_t>(fmt->complexity, buf + 4);
        aoo::to_bytes<int32_t>(fmt->signal_type, buf + 8);
        *size = 12;

        return AOO_ERROR_OK;
    } else {
        LOG_WARNING("Opus: couldn't write settings");
        return AOO_ERROR_UNSPECIFIED;
    }
}

aoo_error deserialize(const aoo_format *header, const char *buf,
                      int32_t size, aoo_format *f){
    if (size < 12){
        LOG_ERROR("Opus: couldn't read format - not enough data!");
        return AOO_ERROR_UNSPECIFIED;
    }
    if (f->size < sizeof(aoo_format_opus)){
        LOG_ERROR("Opus: output format storage too small");
        return AOO_ERROR_UNSPECIFIED;
    }
    auto fmt = (aoo_format_opus *)f;
    // header
    fmt->header.codec = AOO_CODEC_OPUS; // static string!
    fmt->header.size = sizeof(aoo_format_opus); // actual size!
    fmt->header.blocksize = header->blocksize;
    fmt->header.nchannels = header->nchannels;
    fmt->header.samplerate = header->samplerate;
    // options
    fmt->bitrate = aoo::from_bytes<int32_t>(buf);
    fmt->complexity = aoo::from_bytes<int32_t>(buf + 4);
    fmt->signal_type = aoo::from_bytes<int32_t>(buf + 8);

    return AOO_ERROR_OK;
}

aoo_codec codec_class = {
    AOO_CODEC_OPUS,
    // encoder
    encoder_new,
    encoder_free,
    encoder_setformat,
    encoder_getformat,
    encoder_encode,
    // decoder
    decoder_new,
    decoder_free,
    decoder_setformat,
    decoder_getformat,
    decoder_decode,
    // helper
    serialize,
    deserialize
};

} // namespace

void aoo_codec_opus_setup(aoo_codec_registerfn fn){
    fn(AOO_CODEC_OPUS, &codec_class);
}

