/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo/aoo_utils.hpp"
#include "aoo/aoo_pcm.h"
#include "common.hpp"
#if USE_CODEC_OPUS
#include "aoo/aoo_opus.h"
#endif

#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <cstring>

/*/////////////// version ////////////////////*/

namespace aoo {

bool check_version(uint32_t version){
    auto major = (version >> 24) & 255;
    auto minor = (version >> 16) & 255;
    auto bugfix = (version >> 8) & 255;

    if (major != AOO_VERSION_MAJOR){
        return false;
    }

    return true;
}

uint32_t make_version(){
    // make version: major, minor, bugfix, [protocol]
    return ((uint32_t)AOO_VERSION_MAJOR << 24) | ((uint32_t)AOO_VERSION_MINOR << 16)
            | ((uint32_t)AOO_VERSION_PATCH << 8);
}

}

/*////////////// codec plugins ///////////////*/

namespace aoo {

static std::unordered_map<std::string, std::unique_ptr<aoo::codec>> codec_dict;

const aoo::codec * find_codec(const std::string& name){
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

/*//////////////////// OSC ////////////////////////////*/

int32_t aoo_parse_pattern(const char *msg, int32_t n,
                         int32_t *type, int32_t *id)
{
    int32_t offset = 0;
    if (n >= AOO_MSG_DOMAIN_LEN
        && !memcmp(msg, AOO_MSG_DOMAIN, AOO_MSG_DOMAIN_LEN))
    {
        offset += AOO_MSG_DOMAIN_LEN;
        if (n >= (offset + AOO_MSG_SOURCE_LEN)
            && !memcmp(msg + offset, AOO_MSG_SOURCE, AOO_MSG_SOURCE_LEN))
        {
            *type = AOO_TYPE_SOURCE;
            offset += AOO_MSG_SOURCE_LEN;
        } else if (n >= (offset + AOO_MSG_SINK_LEN)
            && !memcmp(msg + offset, AOO_MSG_SINK, AOO_MSG_SINK_LEN))
        {
            *type = AOO_TYPE_SINK;
            offset += AOO_MSG_SINK_LEN;
        } else {
            return 0;
        }

        if (!memcmp(msg + offset, "/*", 2)){
            *id = AOO_ID_WILDCARD; // wildcard
            return offset + 2;
        }
        int32_t skip = 0;
        if (sscanf(msg + offset, "/%d%n", id, &skip) > 0){
            return offset + skip;
        } else {
            // TODO only print relevant part of OSC address string
            LOG_ERROR("aoo_parsepattern: bad ID " << msg + offset);
            return 0;
        }
    } else {
        return 0; // not an AoO message
    }
}

// OSC time stamp (NTP time)
uint64_t aoo_osctime_get(void){
    return aoo::time_tag::now().to_uint64();
}

double aoo_osctime_toseconds(uint64_t t){
    return aoo::time_tag(t).to_double();
}

uint64_t aoo_osctime_fromseconds(double s){
    return aoo::time_tag(s).to_uint64();
}

double aoo_osctime_duration(uint64_t t1, uint64_t t2){
    return aoo::time_tag::duration(t1, t2);
}

namespace aoo {

/*////////////////////////// codec /////////////////////////////*/

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

/*////////////////////////// block /////////////////////////////*/

void block::set(int32_t seq, double sr, int32_t chn,
                const char *data, int32_t nbytes,
                int32_t nframes, int32_t framesize)
{
    sequence = seq;
    samplerate = sr;
    channel = chn;
    numframes_ = nframes;
    framesize_ = framesize;
    buffer_.assign(data, data + nbytes);
}

int32_t block::get_frame(int32_t which, char *data, int32_t n){
    assert(framesize_ > 0 && numframes_ > 0);
    if (which >= 0 && which < numframes_){
        auto onset = which * framesize_;
        auto minsize = (which == numframes_ - 1) ? size() - onset : framesize_;
        if (n >= minsize){
            int32_t nbytes;
            if (which == numframes_ - 1){ // last frame
                nbytes = size() - onset;
            } else {
                nbytes = framesize_;
            }
            auto ptr = buffer_.data() + onset;
            std::copy(ptr, ptr + n, data);
            return nbytes;
        } else {
            LOG_ERROR("buffer too small! got " << n << ", need " << minsize);
        }
    } else {
        LOG_ERROR("frame number " << which << " out of range!");
    }
    return 0;
}

int32_t block::frame_size(int32_t which) const {
    assert(which < numframes_);
    if (which == numframes_ - 1){ // last frame
        return size() - which * framesize_;
    } else {
        return framesize_;
    }
}

/*////////////////////// received_block //////////////////////*/

void received_block::init(int32_t seq, double sr, int32_t chn,
             int32_t nbytes, int32_t nframes)
{
    assert(nbytes > 0);
    assert(nframes <= (int32_t)frames_.size());
    // keep timestamp and numtries if we're actually reiniting
    if (seq != sequence){
        timestamp_ = 0;
        numtries_ = 0;
    }
    sequence = seq;
    samplerate = sr;
    channel = chn;
    buffer_.resize(nbytes);
    numframes_ = nframes;
    framesize_ = 0;
    dropped_ = false;
    frames_.reset();
    for (int i = 0; i < nframes; ++i){
        frames_[i] = true;
    }
}

void received_block::init(int32_t seq, bool dropped)
{
    sequence = seq;
    samplerate = 0;
    channel = 0;
    buffer_.clear();
    numframes_ = 0;
    framesize_ = 0;
    timestamp_ = 0;
    numtries_ = 0;
    dropped_ = dropped;
    if (dropped){
        frames_.reset(); // complete
    } else {
        frames_.set(); // has_frame() always returns false
    }
}

bool received_block::dropped() const {
    return dropped_;
}

bool received_block::complete() const {
    return frames_.none();
}

int32_t received_block::count_frames() const {
    return std::max<int32_t>(0, numframes_ - frames_.count());
}

int32_t received_block::resend_count() const {
    return numtries_;
}

void received_block::add_frame(int32_t which, const char *data, int32_t n){
    assert(!buffer_.empty());
    assert(which < numframes_);
    if (which == numframes_ - 1){
        LOG_DEBUG("copy last frame with " << n << " bytes");
        std::copy(data, data + n, buffer_.end() - n);
    } else {
        LOG_DEBUG("copy frame " << which << " with " << n << " bytes");
        std::copy(data, data + n, buffer_.begin() + which * n);
        framesize_ = n; // LATER allow varying framesizes
    }
    frames_[which] = false;
}

bool received_block::has_frame(int32_t which) const {
    return !frames_[which];
}

bool received_block::update(double time, double interval){
    if (timestamp_ > 0 && (time - timestamp_) < interval){
        return false;
    }
    timestamp_ = time;
    numtries_++;
    LOG_DEBUG("request block " << sequence);
    return true;
}

/*////////////////////////// history_buffer ///////////////////////////*/

void history_buffer::clear(){
    head_ = 0;
    oldest_ = -1;
    for (auto& block : buffer_){
        block.sequence = -1;
    }
}

int32_t history_buffer::capacity() const {
    return buffer_.size();
}

void history_buffer::resize(int32_t n){
    buffer_.resize(n);
    clear();
}

block * history_buffer::find(int32_t seq){
    if (seq >= oldest_){
    #if 0
        // linear search
        for (auto& block : buffer_){
            if (block.sequence == seq){
                return &block;
            }
        }
    #else
        // binary search
        // blocks are always pushed in chronological order,
        // so the ranges [begin, head] and [head, end] will always be sorted.
        auto dofind = [&](auto begin, auto end) -> block * {
            auto result = std::lower_bound(begin, end, seq, [](auto& a, auto& b){
                return a.sequence < b;
            });
            if (result != end && result->sequence == seq){
                return &*result;
            } else {
                return nullptr;
            }
        };
        auto result = dofind(buffer_.begin() + head_, buffer_.end());
        if (!result){
            result = dofind(buffer_.begin(), buffer_.begin() + head_);
        }
        return result;
    #endif
    } else {
        LOG_VERBOSE("couldn't find block " << seq << " - too old");
    }
    return nullptr;
}

block * history_buffer::push()
{
    assert(!buffer_.empty());
    auto old = head_;
    // check if we're going to overwrite an existing block
    if (buffer_[old].sequence >= 0){
        oldest_ = buffer_[old].sequence;
    }
    if (++head_ >= (int32_t)buffer_.size()){
        head_ = 0;
    }
    return &buffer_[old];
}

/*////////////////////////// jitter_buffer /////////////////////////////*/

void jitter_buffer::clear(){
    head_ = tail_ = size_ = 0;
    oldest_ = newest_ = -1;
}

void jitter_buffer::resize(int32_t n){
    data_.resize(n);
    clear();
}

bool jitter_buffer::empty() const {
    return size_ == 0;
}

bool jitter_buffer::full() const {
    return size_ == capacity();
}

int32_t jitter_buffer::size() const {
    return size_;
}

int32_t jitter_buffer::capacity() const {
    return data_.size();
}

received_block* jitter_buffer::find(int32_t seq){
    // first try the end, as we most likely have to complete the most recent block
    if (empty()){
        return nullptr;
    } else if (back().sequence == seq){
        return &back();
    }
#if 0
    // linear search
    if (head_ > tail_){
        for (int32_t i = tail_; i < head_; ++i){
            if (data_[i].sequence == seq){
                return &data_[i];
            }
        }
    } else {
        for (int32_t i = 0; i < head_; ++i){
            if (data_[i].sequence == seq){
                return &data_[i];
            }
        }
        for (int32_t i = tail_; i < capacity(); ++i){
            if (data_[i].sequence == seq){
                return &data_[i];
            }
        }
    }
    return nullptr;
#else
    // binary search
    // (blocks are always pushed in chronological order)
    auto dofind = [&](auto begin, auto end) -> received_block * {
        auto result = std::lower_bound(begin, end, seq, [](auto& a, auto& b){
            return a.sequence < b;
        });
        if (result != end && result->sequence == seq){
            return &(*result);
        } else {
            return nullptr;
        }
    };

    if (head_ > tail_){
        // [tail, head]
        return dofind(&data_[tail_], &data_[head_]);
    } else {
        // [begin, head] + [tail, end]
        auto result = dofind(&data_[0], &data_[head_]);
        if (!result){
            result = dofind(&data_[tail_], &data_[capacity()]);
        }
        return result;
    }
#endif
}

received_block* jitter_buffer::push_back(int32_t seq){
    assert(!full());
    auto old = head_;
    if (++head_ == capacity()){
        head_ = 0;
    }
    size_++;
    newest_ = seq;
    if (oldest_ < 0){
        oldest_ = seq;
    }
    return &data_[old];
}

void jitter_buffer::pop_front(){
    assert(!empty());
    if (++tail_ == capacity()){
        tail_ = 0;
    }
    size_--;
    oldest_++;
}

received_block& jitter_buffer::front(){
    assert(!empty());
    return data_[tail_];
}

const received_block& jitter_buffer::front() const {
    assert(!empty());
    return data_[tail_];
}

received_block& jitter_buffer::back(){
    assert(!empty());
    auto index = head_ - 1;
    if (index < 0){
        index = capacity() - 1;
    }
    return data_[index];
}

const received_block& jitter_buffer::back() const {
    assert(!empty());
    auto index = head_ - 1;
    if (index < 0){
        index = capacity() - 1;
    }
    return data_[index];
}

jitter_buffer::iterator jitter_buffer::begin(){
    if (empty()){
        return end();
    } else {
        return iterator(this, &data_[tail_]);
    }
}

jitter_buffer::const_iterator jitter_buffer::begin() const {
    if (empty()){
        return end();
    } else {
        return const_iterator(this, &data_[tail_]);
    }
}

jitter_buffer::iterator jitter_buffer::end(){
    return iterator(this);
}

jitter_buffer::const_iterator jitter_buffer::end() const {
    return const_iterator(this);
}

std::ostream& operator<<(std::ostream& os, const jitter_buffer& jb){
    os << "jitterbuffer (" << jb.size() << " / " << jb.capacity() << "): ";
    for (auto& b : jb){
        os << b.sequence << " " << "(" << b.count_frames() << "/" << b.num_frames() << ") ";
    }
    return os;
}

/*////////////////////////// dynamic_resampler /////////////////////////////*/

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

/*//////////////////////// timer //////////////////////*/

timer::timer(const timer& other){
    last_ = other.last_.load();
    elapsed_ = other.elapsed_.load();
#if AOO_TIMEFILTER_CHECK
    static_assert(is_pow2(buffersize_), "buffer size must be power of 2!");
    delta_ = other.delta_;
    sum_ = other.sum_;
    buffer_ = other.buffer_;
    head_ = other.head_;
#endif
}

timer& timer::operator=(const timer& other){
    last_ = other.last_.load();
    elapsed_ = other.elapsed_.load();
#if AOO_TIMEFILTER_CHECK
    static_assert(is_pow2(buffersize_), "buffer size must be power of 2!");
    delta_ = other.delta_;
    sum_ = other.sum_;
    buffer_ = other.buffer_;
    head_ = other.head_;
#endif
    return *this;
}

void timer::setup(int32_t sr, int32_t blocksize){
#if AOO_TIMEFILTER_CHECK
    delta_ = (double)blocksize / (double)sr; // shouldn't tear
#endif
    reset();
}

void timer::reset(){
    scoped_lock<spinlock> l(lock_);
    last_ = 0;
    elapsed_ = 0;
#if AOO_TIMEFILTER_CHECK
    // fill ringbuffer with nominal delta
    std::fill(buffer_.begin(), buffer_.end(), delta_);
    sum_ = delta_ * buffer_.size(); // initial sum
    head_ = 0;
#endif
}

double timer::get_elapsed() const {
    return elapsed_.load();
}

time_tag timer::get_absolute() const {
    return last_.load();
}

timer::state timer::update(time_tag t, double& error){
    std::unique_lock<spinlock> l(lock_);
    time_tag last = last_.load();
    if (!last.empty()){
        last_ = t.to_uint64(); // first!

        auto delta = time_tag::duration(last, t);
        elapsed_ = elapsed_ + delta;

    #if AOO_TIMEFILTER_CHECK
        // check delta and return error

        // if we're in a callback scheduler,
        // there shouldn't be any delta larger than
        // the nominal delta +- tolerance

        // If we're in a ringbuffer scheduler and we have a
        // DSP blocksize of N and a hardware buffer size of M,
        // there will be M / N blocks calculated in a row, so we
        // usually see one large delta and (M / N) - 1 short deltas.
        // The arithmetic mean should still be the nominal delta +- tolerance.
        // If it is larger than that, we assume that one or more DSP ticks
        // took too long, so we reset the timer and output the error.
        // Note that this also happens when we start the timer
        // in the middle of the ringbuffer scheduling sequence
        // (i.e. we didn't get all short deltas before the long delta),
        // so resetting the timer makes sure that the next time we start
        // at the beginning.
        // Since the relation between hardware buffersize and DSP blocksize
        // is a power of 2, our ringbuffer size also has to be a power of 2!

        // recursive moving average filter
        head_ = (head_ + 1) & (buffer_.size() - 1);
        sum_ += delta - buffer_[head_];
        buffer_[head_] = delta;

        auto average = sum_ / buffer_.size();
        auto average_error = average - delta_;
        auto last_error = delta - delta_;

        l.unlock();

        if (average_error > delta_ * AOO_TIMEFILTER_TOLERANCE){
            LOG_WARNING("DSP tick(s) took too long!");
            LOG_VERBOSE("last period: " << (delta * 1000.0)
                        << " ms, average period: " << (average * 1000.0)
                        << " ms, error: " << (last_error * 1000.0)
                        << " ms, average error: " << (average_error * 1000.0) << " ms");
            error = std::max<double>(0, delta - delta_);
            return state::error;
        } else {
        #if AOO_DEBUG_TIMEFILTER
            DO_LOG("delta : " << (delta * 1000.0)
                      << ", average delta: " << (average * 1000.0)
                      << ", error: " << (last_error * 1000.0)
                      << ", average error: " << (average_error * 1000.0));
        #endif
        }
    #endif

        return state::ok;
    } else {
        last_ = t.to_uint64();
        return state::reset;
    }
}

} // aoo

void aoo_initialize(){
    static bool initialized = false;
    if (!initialized){
        // register codecs
        aoo_codec_pcm_setup(aoo_register_codec);

    #if USE_CODEC_OPUS
        aoo_codec_opus_setup(aoo_register_codec);
    #endif

        initialized = true;
    }
}

void aoo_terminate() {}
