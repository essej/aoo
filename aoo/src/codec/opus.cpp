/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_codec.h"
#include "codec/aoo_opus.h"

#include "../detail.hpp"

#include "common/utils.hpp"

#include <cassert>
#include <cstring>
#include <memory>

namespace {

//---------------- helper functions -----------------//

void print_format(const AooFormatOpus& f){
#if AOO_LOG_LEVEL >= kAooLogLevelInfo
    const char *application;

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

    LOG_INFO("Opus settings: "
             << "nchannels = " << f.header.numChannels
             << ", blocksize = " << f.header.blockSize
             << ", samplerate = " << f.header.sampleRate
             << ", application = " << application);
#endif
}

bool validate_format(AooFormatOpus& f, bool loud = true)
{
    if (f.header.structSize < AOO_STRUCT_SIZE(AooFormatOpus, applicationType)) {
        return false;
    }

    if (strcmp(f.header.codecName, kAooCodecOpus)){
        return false;
    }

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
            LOG_INFO("Opus: samplerate " << f.header.sampleRate
                     << " not supported - using 48000");
        }
        f.header.sampleRate = 48000;
        break;
    }

    // validate channels
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

    return true;
}

template<typename T>
T& as(void *p){
    return *reinterpret_cast<T *>(p);
}

#define CHECKARG(type) assert(size == sizeof(type))

//------------------------------- encoder --------------------------//

struct Encoder : AooCodec {
    Encoder();
    ~Encoder();

    OpusMSEncoder *state_ = nullptr;
    size_t size_ = 0;
    int sampleRate_ = 0;
    int applicationType_ = 0;
#if AOO_SAMPLE_SIZE == 64
    int numChannels_ = 0;
    std::vector<float, aoo::allocator<float>> buffer_;
#endif
};

AooCodec * AOO_CALL Encoder_new() {
    return aoo::construct<Encoder>();
}

void Encoder_free(AooCodec *c){
    aoo::destroy(static_cast<Encoder *>(c));
}

AooError AOO_CALL Encoder_setup(AooCodec *c, AooFormat *f) {
    auto enc = static_cast<Encoder *>(c);

    auto fmt = (AooFormatOpus *)f;
    if (!validate_format(*fmt, true)){
        return kAooErrorBadArgument;
    }

    print_format(*fmt);

    // setup channel mapping
    // only use decoupled streams (what's the point of coupled streams?)
    auto nchannels = fmt->header.numChannels;
    unsigned char mapping[256];
    for (int i = 0; i < nchannels; ++i){
        mapping[i] = i;
    }
    memset(mapping + nchannels, 255, 256 - nchannels);

    // TODO: don't reallocate if size hasn't changed)
    auto size = opus_multistream_encoder_get_size(nchannels, 0);
    auto state = (OpusMSEncoder *)aoo::allocate(size);
    if (!state){
        return kAooErrorOutOfMemory;
    }

    auto err = opus_multistream_encoder_init(
        state, fmt->header.sampleRate, nchannels,
        nchannels, 0, mapping, fmt->applicationType);
    if (err != OPUS_OK){
        LOG_ERROR("Opus: opus_encoder_create() failed with error code " << err);
        aoo::deallocate(state, size);
        return kAooErrorBadArgument;
    }

    if (enc->state_) {
        aoo::deallocate(enc->state_, enc->size_);
    }

    enc->state_ = state;
    enc->size_ = size;
    enc->sampleRate_ = fmt->header.sampleRate;
    enc->applicationType_ = fmt->applicationType;
#if AOO_SAMPLE_SIZE == 64
    enc->numChannels_ = nchannels;
    enc->buffer_.resize(nchannels * fmt->header.blockSize);
#endif

    return kAooOk;
}

AooError AOO_CALL Encoder_control(
        AooCodec *c, AooCtl ctl, void *ptr, AooSize size) {
    auto e = static_cast<Encoder *>(c);
    switch (ctl){
    case kAooCodecCtlReset:
    {
        auto err = opus_multistream_encoder_ctl(
                    e->state_, OPUS_RESET_STATE);
        if (err != OPUS_OK) {
            return kAooErrorBadArgument;
        }
        break;
    }
    case kAooCodecCtlGetLatency:
    {
        CHECKARG(AooInt32);
        // restricted low-delay: 2.5 ms, audio and VOIP: 6.5 ms
        auto latency = (e->applicationType_ == OPUS_APPLICATION_RESTRICTED_LOWDELAY) ? 2.5 : 6.5;
        as<AooInt32>(ptr) = latency * 0.001 * e->sampleRate_ + 0.5;
        break;
    }
    case OPUS_SET_COMPLEXITY_REQUEST:
    {
        CHECKARG(opus_int32);
        auto complexity = as<opus_int32>(ptr);
        auto err = opus_multistream_encoder_ctl(
                    e->state_, OPUS_SET_COMPLEXITY(complexity));
        if (err != OPUS_OK){
            return kAooErrorBadArgument;
        }
        break;
    }
    case OPUS_GET_COMPLEXITY_REQUEST:
    {
        CHECKARG(opus_int32);
        auto complexity = (opus_int32 *)ptr;
        auto err = opus_multistream_encoder_ctl(
                    e->state_, OPUS_GET_COMPLEXITY(complexity));
        if (err != OPUS_OK){
            return kAooErrorBadArgument;
        }
        break;
    }
    case OPUS_SET_BITRATE_REQUEST:
    {
        CHECKARG(opus_int32);
        auto bitrate = as<opus_int32>(ptr);
        auto err = opus_multistream_encoder_ctl(
                    e->state_, OPUS_SET_BITRATE(bitrate));
        if (err != OPUS_OK){
            return kAooErrorBadArgument;
        }
        break;
    }
    case OPUS_GET_BITRATE_REQUEST:
    {
        CHECKARG(opus_int32);
        auto bitrate = (opus_int32 *)ptr;
    #if 1
        // This control is broken in opus_multistream_encoder (as of opus v1.3.2)
        // because it would always return the default bitrate.
        // As a temporary workaround, we just return OPUS_AUTO unconditionally.
        // LATER validate and cache the value instead.
        *bitrate = OPUS_AUTO;
    #else
        auto err = opus_multistream_encoder_ctl(
                    e->state_, OPUS_GET_BITRATE(bitrate));
        if (err != OPUS_OK){
            return kAooErrorBadArgument;
        }
    #endif
        break;
    }
    case OPUS_SET_SIGNAL_REQUEST:
    {
        CHECKARG(opus_int32);
        auto signal = as<opus_int32>(ptr);
        auto err = opus_multistream_encoder_ctl(
                    e->state_, OPUS_SET_SIGNAL(signal));
        if (err != OPUS_OK){
            return kAooErrorBadArgument;
        }
        break;
    }
    case OPUS_GET_SIGNAL_REQUEST:
    {
        CHECKARG(opus_int32);
        auto signal = (opus_int32 *)ptr;
        auto err = opus_multistream_encoder_ctl(
                    e->state_, OPUS_GET_SIGNAL(signal));
        if (err != OPUS_OK){
            return kAooErrorBadArgument;
        }
        break;
    }
    default:
        LOG_WARNING("Opus: unsupported codec ctl " << ctl);
        return kAooErrorNotImplemented;
    }
    return kAooOk;
}

AooError Encoder_encode(
        AooCodec *c, const AooSample *inSamples, AooInt32 frameSize,
        AooByte *outData, AooInt32 *outSize)
{
    auto enc = static_cast<Encoder*>(c);
#if AOO_SAMPLE_SIZE == 64
    auto nsamples = frameSize * enc->numChannels_;
    auto buffer = enc->buffer_.data();
    for (int32_t i = 0; i < nsamples; ++i) {
        buffer[i] = inSamples[i];
    }
    auto result = opus_multistream_encode_float(
        enc->state_, buffer, frameSize, (unsigned char *)outData, *outSize);
#else
    auto result = opus_multistream_encode_float(
        enc->state_, inSamples, frameSize, (unsigned char *)outData, *outSize);
#endif
    if (result > 0){
        *outSize = result;
        return kAooOk;
    } else {
        LOG_INFO("Opus: opus_encode_float() failed with error code " << result);
        // LATER try to translate Opus error codes to AOO error codes?
        return kAooErrorCodec;
    }
}

//------------------------- decoder -----------------------//

struct Decoder : AooCodec {
    Decoder();
    ~Decoder();

    OpusMSDecoder *state_ = nullptr;
    size_t size_ = 0;
    int sampleRate_ = 0;
    int applicationType_ = 0;
#if AOO_SAMPLE_SIZE == 64
    int numChannels_ = 0;
    std::vector<float, aoo::allocator<float>> buffer_;
#endif
};

AooCodec * AOO_CALL Decoder_new() {
    return aoo::construct<Decoder>();
}

void Decoder_free(AooCodec *c){
    aoo::destroy(static_cast<Decoder *>(c));
}

AooError AOO_CALL Decoder_setup(AooCodec *c, AooFormat *f) {
    auto dec = static_cast<Decoder *>(c);

    auto fmt = (AooFormatOpus *)f;
    if (!validate_format(*fmt, true)){
        return kAooErrorBadArgument;
    }

    print_format(*fmt);

    // setup channel mapping
    // only use decoupled streams (what's the point of coupled streams?)
    auto nchannels = fmt->header.numChannels;
    unsigned char mapping[256];
    for (int i = 0; i < nchannels; ++i){
        mapping[i] = i;
    }
    memset(mapping + nchannels, 255, 256 - nchannels);

    // TODO: don't reallocate if size hasn't changed)
    auto size = opus_multistream_decoder_get_size(nchannels, 0);
    auto state = (OpusMSDecoder *)aoo::allocate(size);
    if (!state){
        return kAooErrorOutOfMemory;
    }
    auto err = opus_multistream_decoder_init(
        state, fmt->header.sampleRate,
        nchannels, nchannels, 0, mapping);
    if (err != OPUS_OK){
        LOG_ERROR("Opus: opus_decoder_create() failed with error code " << err);
        aoo::deallocate(state, size);
        return kAooErrorBadArgument;
    }

    if (dec->state_) {
        aoo::deallocate(dec->state_, dec->size_);
    }

    dec->state_ = state;
    dec->size_ = size;
    dec->sampleRate_ = fmt->header.sampleRate;
    dec->applicationType_ = fmt->applicationType;
#if AOO_SAMPLE_SIZE == 64
    dec->numChannels_ = nchannels;
    dec->buffer_.resize(nchannels * fmt->header.blockSize);
#endif

    return kAooOk;
}

AooError Decoder_control(AooCodec *c, AooCtl ctl, void *ptr, AooSize size){
    auto d = static_cast<Decoder *>(c);
    switch (ctl){
    case kAooCodecCtlReset:
    {
        auto err = opus_multistream_decoder_ctl(
                    d->state_, OPUS_RESET_STATE);
        if (err != OPUS_OK) {
            return kAooErrorBadArgument;
        }
        break;
    }
    case kAooCodecCtlGetLatency:
        CHECKARG(AooInt32);
        as<AooInt32>(ptr) = 0;
        break;
    default:
        LOG_WARNING("Opus: unsupported codec ctl " << ctl);
        return kAooErrorNotImplemented;
    }
    return kAooOk;
}

AooError Decoder_decode(
        AooCodec *c, const AooByte *inData, AooInt32 size,
        AooSample *outSamples, AooInt32 *frameSize)
{
    auto d = static_cast<Decoder *>(c);
#if AOO_SAMPLE_SIZE == 64
    auto buffer = d->buffer_.data();
    auto result = opus_multistream_decode_float(
        d->state_, (const unsigned char *)inData, size, buffer, *frameSize, 0);
    auto nsamples = *frameSize * d->numChannels_;
    for (int32_t i = 0; i < nsamples; ++i) {
        outSamples[i] = buffer[i];
    }
#else
    auto result = opus_multistream_decode_float(
        d->state_, (const unsigned char *)inData, size, outSamples, *frameSize, 0);
#endif
    if (result > 0){
        *frameSize = result;
        return kAooOk;
    } else {
        LOG_INFO("Opus: opus_decode_float() failed with error code " << result);
        // LATER try to translate Opus error codes to AOO error codes?
        return kAooErrorCodec;
    }
}

//-------------------------- free functions ------------------------//

AooError serialize(const AooFormat *f, AooByte *buf, AooInt32 *size)
{
    if (!size) {
        return kAooErrorBadArgument;
    }
    if (!buf){
        *size = sizeof(AooInt32);
        return kAooOk;
    }
    if (*size < sizeof(AooInt32)) {
        LOG_ERROR("Opus: couldn't write settings - buffer too small!");
        return kAooErrorInsufficientBuffer;
    }
    if (!AOO_CHECK_FIELD(f, AooFormatOpus, applicationType)) {
        LOG_ERROR("Opus: bad format struct size");
        return kAooErrorBadArgument;
    }

    auto fmt = (const AooFormatOpus *)f;
    aoo::to_bytes<AooInt32>(fmt->applicationType, buf);
    *size = sizeof(AooInt32);

    return kAooOk;
}

AooError deserialize(
        const AooByte *buf, AooInt32 size, AooFormat *f, AooInt32 *fmtsize)
{
    if (!fmtsize) {
        return kAooErrorBadArgument;
    }
    if (!f) {
        *fmtsize = AOO_STRUCT_SIZE(AooFormatOpus, applicationType);
        return kAooOk;
    }
    if (size < sizeof(AooInt32)) {
        LOG_ERROR("Opus: couldn't read format - not enough data!");
        return kAooErrorBadArgument;
    }
    if (*fmtsize < AOO_STRUCT_SIZE(AooFormatOpus, applicationType)) {
        LOG_ERROR("Opus: output format storage too small");
        return kAooErrorBadArgument;
    }
    auto fmt = (AooFormatOpus *)f;
    fmt->applicationType = aoo::from_bytes<AooInt32>(buf);
    *fmtsize = AOO_STRUCT_SIZE(AooFormatOpus, applicationType);

    return kAooOk;
}

AooCodecInterface g_interface = {
    AOO_STRUCT_SIZE(AooCodecInterface, deserialize),
    kAooCodecOpus,
    // encoder
    Encoder_new,
    Encoder_free,
    Encoder_setup,
    Encoder_control,
    Encoder_encode,
    // decoder
    Decoder_new,
    Decoder_free,
    Decoder_setup,
    Decoder_control,
    Decoder_decode,
    // helper
    serialize,
    deserialize
};

Encoder::Encoder() {
    cls = &g_interface;
}

Encoder::~Encoder(){
    if (state_) {
        aoo::deallocate(state_, size_);
    }
}

Decoder::Decoder() {
    cls = &g_interface;
}

Decoder::~Decoder(){
    if (state_) {
        aoo::deallocate(state_, size_);
    }
}

} // namespace

void aoo_opusLoad(const AooCodecHostInterface *iface){
    iface->registerCodec(&g_interface);
    // the Opus codec is always statically linked, so we can simply use the
    // internal log function and allocator
}

void aoo_opusUnload() {}
