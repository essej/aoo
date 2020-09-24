#pragma once

#include "aoo/aoo.h"

#include <memory>

namespace aoo {

class base_codec {
public:
    base_codec(const aoo_codec *codec, void *obj)
        : codec_(codec), obj_(obj){}
    base_codec(const aoo_codec&) = delete;

    const char *name() const { return codec_->name; }
    int32_t nchannels() const { return nchannels_; }
    int32_t samplerate() const { return samplerate_; }
    int32_t blocksize() const { return blocksize_; }
protected:
    const aoo_codec *codec_;
    void *obj_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
};

class encoder : public base_codec {
public:
    using base_codec::base_codec;
    ~encoder(){
        codec_->encoder_free(obj_);
    }

    bool set_format(aoo_format& fmt);
    bool get_format(aoo_format_storage& fmt) const {
        return codec_->encoder_getformat(obj_, &fmt) > 0;
    }
    int32_t write_format(aoo_format& fmt, char *buf, int32_t size){
        return codec_->encoder_writeformat(obj_, &fmt, buf, size);
    }
    int32_t encode(const aoo_sample *s, int32_t n, char *buf, int32_t size){
        return codec_->encoder_encode(obj_, s, n, buf, size);
    }
};

class decoder : public base_codec {
public:
    using base_codec::base_codec;
    ~decoder(){
        codec_->decoder_free(obj_);
    }

    bool set_format(aoo_format& fmt);
    bool get_format(aoo_format_storage& f) const {
        return codec_->decoder_getformat(obj_, &f) > 0;
    }
    int32_t read_format(const aoo_format& fmt, const char *opt, int32_t size);
    int32_t decode(const char *buf, int32_t size, aoo_sample *s, int32_t n){
        return codec_->decoder_decode(obj_, buf, size, s, n);
    }
};

class codec {
public:
    codec(const aoo_codec *c)
        : codec_(c){}
    const char *name() const {
        return codec_->name;
    }
    std::unique_ptr<encoder> create_encoder() const;
    std::unique_ptr<decoder> create_decoder() const;
private:
    const aoo_codec *codec_;
};

const codec * find_codec(const char * name);

} // aoo
