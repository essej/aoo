#include "resampler.hpp"

#include <algorithm>

namespace aoo {

// extra space for samplerate fluctuations and non-pow-of-2 blocksizes.
// must be larger than 2!
#define AOO_RESAMPLER_SPACE 2.5

void dynamic_resampler::setup(int32_t nfrom, int32_t nto, int32_t srfrom, int32_t srto, int32_t nchannels){
    clear();
    nchannels_ = nchannels;
    ideal_ratio_ = (double)srto / (double)srfrom;
    int32_t blocksize;
    if (ideal_ratio_ < 1.0){
        // downsampling
        blocksize = std::max<int32_t>(nfrom, (double)nto / ideal_ratio_ + 0.5);
    } else {
        blocksize = std::max<int32_t>(nfrom, nto);
    }
    blocksize *= AOO_RESAMPLER_SPACE;
#if AOO_DEBUG_RESAMPLING
    DO_LOG("resampler setup: nfrom: " << nfrom << ", srfrom: " << srfrom << ", nto: " << nto
           << ", srto: " << srto << ", capacity: " << blocksize);
#endif
    buffer_.resize(blocksize * nchannels_);
    update(srfrom, srto);
}

void dynamic_resampler::clear(){
    ratio_ = 1;
    rdpos_ = 0;
    wrpos_ = 0;
    balance_ = 0;
}

void dynamic_resampler::update(double srfrom, double srto){
    if (srfrom == srto){
        ratio_ = 1;
    } else {
        ratio_ = srto / srfrom;
    }
#if AOO_DEBUG_RESAMPLING
    DO_LOG("srfrom: " << srfrom << ", srto: " << srto << ", ratio: " << ratio_);
    DO_LOG("balance: " << balance_ << ", capacity: " << buffer_.size());
#endif
}

bool dynamic_resampler::write(const aoo_sample *data, int32_t n){
    if (buffer_.size() - balance_ < n){
        return false;
    }
    auto size = (int32_t)buffer_.size();
    auto end = wrpos_ + n;
    int32_t split;
    if (end > size){
        split = size - wrpos_;
    } else {
        split = n;
    }
    std::copy(data, data + split, &buffer_[wrpos_]);
    std::copy(data + split, data + n, &buffer_[0]);
    wrpos_ += n;
    if (wrpos_ >= size){
        wrpos_ -= size;
    }
    balance_ += n;
    return true;
}

bool dynamic_resampler::read(aoo_sample *data, int32_t n){
    auto size = (int32_t)buffer_.size();
    auto limit = size / nchannels_;
    int32_t intpos = (int32_t)rdpos_;
    double advance = 1.0 / ratio_;
    int32_t intadvance = (int32_t)advance;
    if ((advance - intadvance) == 0.0 && (rdpos_ - intpos) == 0.0){
        // non-interpolating (faster) versions
        if ((int32_t)balance_ < n * intadvance){
            return false;
        }
        if (intadvance == 1){
            // just copy samples
            int32_t pos = intpos * nchannels_;
            int32_t end = pos + n;
            int n1, n2;
            if (end > size){
                n1 = size - pos;
                n2 = end - size;
            } else {
                n1 = n;
                n2 = 0;
            }
            std::copy(&buffer_[pos], &buffer_[pos + n1], data);
            std::copy(&buffer_[0], &buffer_[n2], data + n1);
            pos += n;
            if (pos >= size){
                pos -= size;
            }
            rdpos_ = pos / nchannels_;
            balance_ -= n;
        } else {
            // skip samples
            int32_t pos = rdpos_;
            for (int i = 0; i < n; i += nchannels_){
                for (int j = 0; j < nchannels_; ++j){
                    int32_t index = pos * nchannels_ + j;
                    data[i + j] = buffer_[index];
                }
                pos += intadvance;
                if (pos >= limit){
                    pos -= limit;
                }
            }
            rdpos_ = pos;
            balance_ -= n * intadvance;
        }
     } else {
        // interpolating version
        if (static_cast<int32_t>(balance_ * ratio_ / nchannels_) * nchannels_ <= n){
            return false;
        }
        double pos = rdpos_;
        for (int i = 0; i < n; i += nchannels_){
            int32_t index = (int32_t)pos;
            double fract = pos - (double)index;
            for (int j = 0; j < nchannels_; ++j){
                int32_t idx1 = index * nchannels_ + j;
                int32_t idx2 = (index + 1) * nchannels_ + j;
                if (idx2 >= size){
                    idx2 -= size;
                }
                double a = buffer_[idx1];
                double b = buffer_[idx2];
                data[i + j] = a + (b - a) * fract;
            }
            pos += advance;
            if (pos >= limit){
                pos -= limit;
            }
        }
        rdpos_ = pos;
        balance_ -= n * advance;
    }
    return true;
}

} // aoo
