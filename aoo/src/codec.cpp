#include "codec.hpp"

#include "common/utils.hpp"

#include <string>
#include <unordered_map>
#include <memory>

namespace aoo {

bool encoder::set_format(aoo_format& fmt){
    auto result = codec_->encoder_setformat(obj_, &fmt);
    if (result > 0){
        // assign after validation!
        nchannels_ = fmt.nchannels;
        samplerate_ = fmt.samplerate;
        blocksize_ = fmt.blocksize;
        return true;
    } else {
        return false;
    }
}

bool decoder::set_format(aoo_format& fmt){
    auto result = codec_->decoder_setformat(obj_, &fmt);
    if (result > 0){
        // assign after validation!
        nchannels_ = fmt.nchannels;
        samplerate_ = fmt.samplerate;
        blocksize_ = fmt.blocksize;
        return true;
    } else {
        return false;
    }
}

int32_t decoder::read_format(const aoo_format& fmt, const char *opt, int32_t size){
    auto result = codec_->decoder_readformat(obj_, &fmt, opt, size);
    if (result >= 0){
        nchannels_ = fmt.nchannels;
        samplerate_ = fmt.samplerate;
        blocksize_ = fmt.blocksize;
    }
    return result;
}

std::unique_ptr<encoder> codec::create_encoder() const {
    auto obj = codec_->encoder_new();
    if (obj){
        return std::make_unique<encoder>(codec_, obj);
    } else {
        return nullptr;
    }
}
std::unique_ptr<decoder> codec::create_decoder() const {
    auto obj = codec_->decoder_new();
    if (obj){
        return std::make_unique<decoder>(codec_, obj);
    } else {
        return nullptr;
    }
}

static std::unordered_map<std::string, std::unique_ptr<aoo::codec>> codec_dict;

const aoo::codec * find_codec(const char * name){
    auto it = codec_dict.find(name);
    if (it != codec_dict.end()){
        return it->second.get();
    } else {
        return nullptr;
    }
}

} // aoo

int32_t aoo_register_codec(const char *name, const aoo_codec *codec){
    if (aoo::codec_dict.count(name) != 0){
        LOG_WARNING("aoo: codec " << name << " already registered!");
        return 0;
    }
    aoo::codec_dict[name] = std::make_unique<aoo::codec>(codec);
    LOG_VERBOSE("aoo: registered codec '" << name << "'");
    return 1;
}
