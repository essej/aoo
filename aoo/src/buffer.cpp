#include "buffer.hpp"

#include "common/utils.hpp"

#include <algorithm>
#include <cassert>

namespace aoo {

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
    #if AOO_DEBUG_JITTER_BUFFER
        DO_LOG("jitter buffer: copy last frame with " << n << " bytes");
    #endif
        std::copy(data, data + n, buffer_.end() - n);
    } else {
    #if AOO_DEBUG_JITTER_BUFFER
        DO_LOG("jitter buffer: copy frame " << which << " with " << n << " bytes");
    #endif
        std::copy(data, data + n, buffer_.data() + (which * n));
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
#if AOO_DEBUG_JITTER_BUFFER
    DO_LOG("jitter buffer: request block " << sequence);
#endif
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
    last_popped_ = last_pushed_ = -1;
}

void jitter_buffer::resize(int32_t n){
    data_.resize(n);
    clear();
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

    auto begin = data_.data();
    if (head_ > tail_){
        // [tail, head]
        return dofind(begin + tail_, begin + head_);
    } else {
        // [begin, head] + [tail, end]
        auto result = dofind(begin, begin + head_);
        if (!result){
            result = dofind(begin + tail_, begin + data_.capacity());
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
    last_pushed_ = seq;
    return &data_[old];
}

void jitter_buffer::pop_front(){
    assert(!empty());
    last_popped_ = data_[tail_].sequence;
    if (++tail_ == capacity()){
        tail_ = 0;
    }
    size_--;
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

} // aoo
