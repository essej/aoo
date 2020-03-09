#include "aoo/aoo_opus.h"
#include "aoo/aoo_utils.hpp"

#include <cassert>
#include <cstring>
#include <memory>

namespace {

struct encoder {
    encoder(){
        memset(&format, 0, sizeof(format));
    }
    ~encoder(){
        if (state){
            opus_encoder_destroy(state);
        }
    }
    OpusEncoder *state = nullptr;
    aoo_format_opus format;
};

struct decoder {
    ~decoder(){
        if (state){
            opus_decoder_destroy(state);
        }
    }
    OpusDecoder * state = nullptr;
    aoo_format_opus format;
};

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

void *encoder_new(){
    return new encoder;
}

void encoder_free(void *enc){
    delete (encoder *)enc;
}

void encoder_setup(void *enc, aoo_format *f){
    assert(!strcmp(f->codec, AOO_CODEC_OPUS));
    auto c = static_cast<encoder *>(enc);
    auto fmt = reinterpret_cast<aoo_format_opus *>(f);

    // validate samplerate
    switch (fmt->header.samplerate){
    case 8000:
    case 12000:
    case 16000:
    case 24000:
    case 48000:
        break;
    default:
        LOG_VERBOSE("Opus: samplerate " << fmt->header.samplerate
                    << " not supported - using 48000");
        fmt->header.samplerate = 48000;
        break;
    }
    // validate channels (LATER support multichannel!)
    if (fmt->header.nchannels < 1 || fmt->header.nchannels > 2){
        LOG_WARNING("Opus: channel count " << fmt->header.nchannels <<
                    " out of range - using 1 channels");
        fmt->header.nchannels = 1;
    }
    // validate blocksize
    int minblocksize = fmt->header.samplerate / 400; // 120 samples @ 48 kHz
    int maxblocksize = minblocksize * 24; // 2880 samples @ 48 kHz
    int blocksize = fmt->header.blocksize;
    if (blocksize < minblocksize){
        fmt->header.blocksize = minblocksize;
    } else if (blocksize > maxblocksize){
        fmt->header.blocksize = maxblocksize;
    } else {
        // round down
        while (blocksize > (minblocksize * 2)){
            minblocksize *= 2;
        }
        fmt->header.blocksize = minblocksize;
    }
    // bitrate, complexity and signal type should be validated by opus

    int error = 0;
    if (c->state){
        opus_encoder_destroy(c->state);
    }
    c->state = opus_encoder_create(fmt->header.samplerate,
                                       fmt->header.nchannels,
                                       OPUS_APPLICATION_AUDIO,
                                       &error);
    if (error == OPUS_OK){
        assert(c->state != nullptr);
        // apply settings
        // complexity
        opus_encoder_ctl(c->state, OPUS_SET_COMPLEXITY(fmt->complexity));
        opus_encoder_ctl(c->state, OPUS_GET_COMPLEXITY(&fmt->complexity));
        // bitrate
        opus_encoder_ctl(c->state, OPUS_SET_BITRATE(fmt->bitrate));
        opus_encoder_ctl(c->state, OPUS_GET_BITRATE(&fmt->bitrate));
        // signal type
        opus_encoder_ctl(c->state, OPUS_SET_SIGNAL(fmt->signal_type));
        opus_encoder_ctl(c->state, OPUS_GET_SIGNAL(&fmt->signal_type));
    } else {
        LOG_ERROR("Opus: opus_encoder_create() failed with error code " << error);
    }

    // save and print settings
    memcpy(&c->format, fmt, sizeof(aoo_format_opus));
    print_settings(*fmt);
}

int32_t encoder_encode(void *enc,
                       const aoo_sample *s, int32_t n,
                       char *buf, int32_t size)
{
    auto c = static_cast<encoder *>(enc);
    if (c->state){
        auto framesize = n / c->format.header.nchannels;
        auto result = opus_encode_float(c->state,
                                        s, framesize, (unsigned char *)buf, size);
        if (result > 0){
            return result;
        } else {
            LOG_VERBOSE("Opus: opus_encode_float() failed with error code " << result);
        }
    }
    return 0;
}

int32_t encoder_write(void *enc, int32_t *nchannels,int32_t *samplerate,
                      int32_t *blocksize, char *buf, int32_t size){
    if (size >= 12){
        auto c = static_cast<encoder *>(enc);
        *nchannels = c->format.header.nchannels;
        *samplerate = c->format.header.samplerate;
        *blocksize = c->format.header.blocksize;
        aoo::to_bytes<int32_t>(c->format.bitrate, buf);
        aoo::to_bytes<int32_t>(c->format.complexity, buf + 4);
        aoo::to_bytes<int32_t>(c->format.signal_type, buf + 8);

        return 12;
    } else {
        LOG_WARNING("Opus: couldn't write settings");
        return -1;
    }
}

void *decoder_new(){
    return new decoder;
}

void decoder_free(void *dec){
    delete (decoder *)dec;
}

int32_t decoder_decode(void *dec,
                       const char *buf, int32_t size,
                       aoo_sample *s, int32_t n)
{
    auto c = static_cast<decoder *>(dec);
    if (c->state){
        auto framesize = n / c->format.header.nchannels;
        auto result = opus_decode_float(c->state, (const unsigned char *)buf, size, s, framesize, 0);
        if (result > 0){
            return result;
        } else {
            LOG_VERBOSE("Opus: opus_decode_float() failed with error code " << result);
        }
    }
    return 0;
}

int32_t decoder_read(void *dec, int32_t nchannels, int32_t samplerate,
                     int32_t blocksize, const char *buf, int32_t size){
    if (size >= 12){
        auto c = static_cast<decoder *>(dec);
        c->format.header.nchannels = nchannels;
        c->format.header.samplerate = samplerate;
        c->format.header.blocksize = blocksize;
        c->format.bitrate = aoo::from_bytes<int32_t>(buf);
        c->format.complexity = aoo::from_bytes<int32_t>(buf + 4);
        c->format.signal_type = aoo::from_bytes<int32_t>(buf + 8);

        // TODO validate format

        if (c->state){
            opus_decoder_destroy(c->state);
        }
        int error = 0;
        c->state = opus_decoder_create(c->format.header.samplerate,
                                           c->format.header.nchannels,
                                           &error);
        if (error == OPUS_OK){
            assert(c->state != nullptr);
            // apply settings
            // complexity
            opus_decoder_ctl(c->state, OPUS_SET_COMPLEXITY(c->format.complexity));
            opus_decoder_ctl(c->state, OPUS_GET_COMPLEXITY(&c->format.complexity));
            // bitrate
            opus_decoder_ctl(c->state, OPUS_SET_BITRATE(c->format.bitrate));
            opus_decoder_ctl(c->state, OPUS_GET_BITRATE(&c->format.bitrate));
            // signal type
            opus_decoder_ctl(c->state, OPUS_SET_SIGNAL(c->format.signal_type));
            opus_decoder_ctl(c->state, OPUS_GET_SIGNAL(&c->format.signal_type));
        } else {
            LOG_ERROR("Opus: opus_decoder_create() failed with error code " << error);
        }

        print_settings(c->format);

        return 12;
    } else {
        LOG_ERROR("Opus: couldn't read settings - too little data!");
        return -1;
    }
}

aoo_codec codec_class = {
    AOO_CODEC_OPUS,
    encoder_new,
    encoder_free,
    encoder_setup,
    encoder_encode,
    encoder_write,
    decoder_new,
    decoder_free,
    decoder_decode,
    decoder_read
};

} // namespace

void aoo_codec_opus_setup(aoo_codec_registerfn fn){
    fn(AOO_CODEC_OPUS, &codec_class);
}

