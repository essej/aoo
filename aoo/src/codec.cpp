#include "codec.hpp"

#include "common/utils.hpp"

#include <string>
#include <unordered_map>
#include <memory>

namespace aoo {

aoo_error encoder::set_format(aoo_format& fmt){
    auto result = codec_->encoder_setformat(obj_, &fmt);
    if (result == AOO_OK){
        // assign after validation!
        nchannels_ = fmt.nchannels;
        samplerate_ = fmt.samplerate;
        blocksize_ = fmt.blocksize;
    }
    return result;
}

aoo_error decoder::set_format(aoo_format& fmt){
    auto result = codec_->decoder_setformat(obj_, &fmt);
    if (result == AOO_OK){
        // assign after validation!
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

static std::unordered_map<std::string, std::unique_ptr<aoo::codec>> g_codec_dict;

const aoo::codec * find_codec(const char * name){
    auto it = g_codec_dict.find(name);
    if (it != g_codec_dict.end()){
        return it->second.get();
    } else {
        return nullptr;
    }
}

} // aoo

aoo_error aoo_register_codec(const char *name, const aoo_codec *codec){
    if (aoo::g_codec_dict.count(name) != 0){
        LOG_WARNING("aoo: codec " << name << " already registered!");
        return AOO_ERROR_UNSPECIFIED;
    }
    aoo::g_codec_dict[name] = std::make_unique<aoo::codec>(codec);
    LOG_VERBOSE("aoo: registered codec '" << name << "'");
    return AOO_OK;
}
