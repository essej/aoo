#pragma once

#include "aoo/aoo_codec.h"
#include "imp.hpp"

#include <memory>

namespace aoo {

class encoder;
class decoder;

class codec {
public:
    codec(const char *name, const AooCodecInterface *c)
        : name_(name), codec_(c){}

    const char *name() const {
        return name_;
    }

    std::unique_ptr<encoder> create_encoder(AooError *err) const;

    std::unique_ptr<decoder> create_decoder(AooError *err) const;

    AooError serialize(const AooFormat& format,
                       AooByte *buffer, AooInt32& size) const {
        return codec_->serialize(&format, buffer, &size);
    }

    AooError deserialize(const AooFormat& header,
                          const AooByte *data, AooInt32 size,
                          AooFormat& format, AooInt32 fmtsize) const {
        return codec_->deserialize(&header, data, size, &format, fmtsize);
    }
protected:
    const char *name_;
    const AooCodecInterface *codec_;
};

class base_codec : public codec {
public:
    base_codec(const char *name, const AooCodecInterface *c, void *obj)
        : codec(name, c), obj_(obj){}

    int32_t nchannels() const { return nchannels_; }

    int32_t samplerate() const { return samplerate_; }

    int32_t blocksize() const { return blocksize_; }
protected:
    void *obj_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;

    void save_format(const AooFormat& f){
        nchannels_ = f.numChannels;
        samplerate_ = f.sampleRate;
        blocksize_ = f.blockSize;
    }
};

class encoder : public base_codec {
public:
    using base_codec::base_codec;

    ~encoder(){
        codec_->encoderFree(obj_);
    }

    AooError set_format(AooFormat& fmt){
        auto result = codec_->encoderControl(obj_,
            kAooCodecCtlSetFormat, &fmt, sizeof(AooFormat));
        if (result == kAooOk){
            save_format(fmt); // after validation!
        }
        return result;
    }

    AooError get_format(AooFormat& fmt, size_t size) const {
        return codec_->encoderControl(obj_, kAooCodecCtlGetFormat,
                                      &fmt, size);
    }

    bool compare(const AooFormat& fmt) const {
        return codec_->encoderControl(obj_, kAooCodecCtlFormatEqual,
                                      (void *)&fmt, fmt.size);
    }

    AooError reset() {
        return codec_->encoderControl(obj_, kAooCodecCtlReset, nullptr, 0);
    }

    AooError encode(const AooSample *s, AooInt32 n, AooByte *buf, AooInt32 &size){
        return codec_->encoderEncode(obj_, s, n, buf, &size);
    }
};

inline std::unique_ptr<encoder> codec::create_encoder(AooError *err) const {
    return std::make_unique<encoder>(name_, codec_, codec_->encoderNew(err));
}

class decoder : public base_codec {
public:
    using base_codec::base_codec;
    ~decoder(){
        codec_->decoderFree(obj_);
    }

    AooError set_format(AooFormat& fmt){
        auto result = codec_->decoderControl(obj_,
            kAooCodecCtlSetFormat, &fmt, sizeof(AooFormat));
        if (result == kAooOk){
            save_format(fmt); // after validation!
        }
        return result;
    }

    AooError get_format(AooFormat& fmt, size_t size) const {
        return codec_->decoderControl(obj_, kAooCodecCtlGetFormat,
                                      &fmt, size);
    }

    bool compare(const AooFormat& fmt) const {
        return codec_->decoderControl(obj_, kAooCodecCtlFormatEqual,
                                      (void *)&fmt, fmt.size);
    }

    AooError reset() {
        return codec_->decoderControl(obj_, kAooCodecCtlReset, nullptr, 0);
    }

    AooError decode(const AooByte *buf, AooInt32 size, AooSample *s, AooInt32 &n){
        return codec_->decoderDecode(obj_, buf, size, s, &n);
    }
};

inline std::unique_ptr<decoder> codec::create_decoder(AooError *err) const {
    return std::make_unique<decoder>(name_, codec_, codec_->decoderNew(err));
}

const codec * find_codec(const char * name);

} // aoo
