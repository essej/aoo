#include "aoo/aoo.hpp"
#include "aoo/aoo_utils.hpp"
#include "aoo/aoo_pcm.h"
#include "aoo/aoo_opus.h"
#include "aoo_imp.hpp"

#include <unordered_map>
#include <chrono>
#include <algorithm>

namespace aoo {

static std::unordered_map<std::string, std::unique_ptr<aoo::codec>> codec_dict;

void register_codec(const char *name, const aoo_codec *codec){
    LOG_VERBOSE("aoo: registered codec '" << name << "'");
    codec_dict[name] = std::make_unique<aoo::codec>(codec);
}

const aoo::codec * find_codec(const std::string& name){
    auto it = codec_dict.find(name);
    if (it != codec_dict.end()){
        return it->second.get();
    } else {
        return nullptr;
    }
}

bool is_pow2(int32_t i){
    return (i & (i - 1)) == 0;
}

} // aoo

/*//////////////////// OSC ////////////////////////////*/

int32_t aoo_parsepattern(const char *msg, int32_t n, int32_t *id){
    int32_t offset = sizeof(AOO_DOMAIN) - 1;
    if (n < (offset + 2)){
        return 0;
    }
    if (!memcmp(msg, AOO_DOMAIN, offset)){
        if (!memcmp(msg + offset, "/*", 2)){
            *id = AOO_ID_WILDCARD; // wildcard
            return offset + 2;
        }
        int32_t skip = 0;
        if (sscanf(&msg[offset], "/%d%n", id, &skip) > 0){
            return offset + skip;
        }
    }
    return 0;
}

// OSC time stamp (NTP time)
uint64_t aoo_osctime_get(void){
    // use system clock (1970 epoch)
    auto epoch = std::chrono::system_clock::now().time_since_epoch();
    auto s = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    auto ns = epoch - s;
    // add number of seconds between 1900 and 1970 (including leap years!)
    auto seconds = s.count() + 2208988800UL;
    // fractional part in nanoseconds mapped to the range of uint32_t
    auto nanos = (double)ns.count() * 4.294967296; // 2^32 / 1e9
    // seconds in the higher 4 bytes, nanos in the lower 4 bytes
    aoo::time_tag tt;
    tt.seconds = seconds;
    tt.nanos = nanos;
    return tt.to_uint64();
}

double aoo_osctime_toseconds(uint64_t t){
    return aoo::time_tag(t).to_double();
}

uint64_t aoo_osctime_fromseconds(double s){
    return aoo::time_tag(s).to_uint64();
}

uint64_t aoo_osctime_addseconds(uint64_t t, double s){
    // LATER do operator overloading for aoo::time_tag
    // split osctime
    uint64_t th = t >> 32;
    uint64_t tl = (t & 0xFFFFFFFF);
    // split seconds
    uint64_t sh = (uint64_t)s;
    double fract = s - (double)sh;
    uint64_t sl = fract * 4294967296.0;
    // combine and reassemble
    uint64_t rh = th + sh;
    uint64_t rl = tl + sl;
    // handle overflowing nanoseconds
    rh += (rl >> 32); // add carry
    rl &= 0xFFFFFFFF; // mask carry
    return (rh << 32) + rl;
}

namespace aoo {

/*////////////////////////// block /////////////////////////////*/

void block::set(int32_t seq, double sr, int32_t chn,
             int32_t nbytes, int32_t nframes)
{
    sequence = seq;
    samplerate = sr;
    channel = chn;
    numframes_ = nframes;
    framesize_ = 0;
    assert(nbytes > 0);
    buffer_.resize(nbytes);
    // set missing frame bits to 1
    frames_ = 0;
    for (int i = 0; i < nframes; ++i){
        frames_ |= ((uint64_t)1 << i);
    }
    // LOG_DEBUG("initial frames: " << (unsigned)frames);
}

void block::set(int32_t seq, double sr, int32_t chn,
                const char *data, int32_t nbytes,
                int32_t nframes, int32_t framesize)
{
    sequence = seq;
    samplerate = sr;
    channel = chn;
    numframes_ = nframes;
    framesize_ = framesize;
    frames_ = 0; // no frames missing
    buffer_.assign(data, data + nbytes);
}

bool block::complete() const {
    if (buffer_.data() == nullptr){
        LOG_ERROR("buffer is 0!");
    }
    assert(buffer_.data() != nullptr);
    assert(sequence >= 0);
    return frames_ == 0;
}

void block::add_frame(int32_t which, const char *data, int32_t n){
    assert(data != nullptr);
    assert(buffer_.data() != nullptr);
    if (which == numframes_ - 1){
        LOG_DEBUG("copy last frame with " << n << " bytes");
        std::copy(data, data + n, buffer_.end() - n);
    } else {
        LOG_DEBUG("copy frame " << which << " with " << n << " bytes");
        std::copy(data, data + n, buffer_.begin() + which * n);
        framesize_ = n; // LATER allow varying framesizes
    }
    frames_ &= ~((uint64_t)1 << which);
    // LOG_DEBUG("frames: " << frames_);
}

void block::get_frame(int32_t which, const char *&data, int32_t &n){
    assert(framesize_ > 0 && numframes_ > 0);
    if (which >= 0 && which < numframes_){
        auto onset = which * framesize_;
        data = buffer_.data() + onset;
        if (which == numframes_ - 1){ // last frame
            n = size() - onset;
        } else {
            n = framesize_;
        }
    } else {
        LOG_ERROR("frame number " << which << " out of range!");
        data = nullptr;
        n = 0;
    }
}

bool block::has_frame(int32_t which) const {
    assert(which < numframes_);
    return ((frames_ >> which) & 1) == 0;
}

/*////////////////////////// block_ack /////////////////////////////*/

block_ack::block_ack()
    : sequence(-1), count_(0), timestamp_(-1e009){}

block_ack::block_ack(int32_t seq, int32_t limit)
    : sequence(seq) {
    count_ = limit;
    timestamp_ = -1e009;
}

bool block_ack::check(double time, double interval){
    if (count_ > 0){
        auto diff = time - timestamp_;
        if (diff >= interval){
            timestamp_ = time;
            count_--;
            LOG_DEBUG("request block " << sequence);
            return true;
        } else {
//            LOG_DEBUG("don't resend block "
//                        << sequence << ": need to wait");
        }
    } else {
//        LOG_DEBUG("don't resend block "
//                    << sequence << ": tried too many times");
    }
    return false;
}

/*////////////////////////// block_ack_list ///////////////////////////*/

#if BLOCK_ACK_LIST_HASHTABLE

block_ack_list::block_ack_list(){
    data_.resize(initial_size_);
    mask_ = data_.size() - 1;
    oldest_ = INT32_MAX;
}

void block_ack_list::setup(int32_t limit){
    limit_ = limit;
}

void block_ack_list::clear(){
    for (auto& d : data_){
        d.sequence = -1;
    }
    size_ = 0;
    oldest_ = INT32_MAX;
}

int32_t block_ack_list::size() const {
    return size_;
}

bool block_ack_list::empty() const {
    return size_ == 0;
}

block_ack * block_ack_list::find(int32_t seq){
    auto index = seq & mask_;
    while (data_[index].sequence != seq){
        if (data_[index].sequence < 0){
            return nullptr;
        }
        index = (index + increment_) & mask_;
    }
    assert(data_[index].sequence >= 0);
    assert(seq >= oldest_);
    return &data_[index];
}

block_ack& block_ack_list::get(int32_t seq){
    // try to find item
    auto index = seq & mask_;
    while (data_[index].sequence != seq){
        if (data_[index].sequence < 0){
            // hit empty item -> insert item
            data_[index] = block_ack { seq, limit_ };
            if (seq < oldest_){
                oldest_ = seq;
            }
            // rehash if the table is more than 50% full
            if (++size_ > (int32_t)(data_.size() >> 1)){
                rehash();
                auto b = find(seq);
                assert(b != nullptr);
                return *b;
            }
            break;
        }
        index = (index + increment_) & mask_;
    }
    assert(data_[index].sequence >= 0);
    return data_[index];
}

bool block_ack_list::remove(int32_t seq){
    auto b = find(seq);
    if (b){
        b->sequence = -1;
        size_--;
        if (empty()){
            oldest_ = INT32_MAX;
        }
        assert(size_ >= 0);
        return true;
    } else {
        return false;
    }
}

int32_t block_ack_list::remove_before(int32_t seq){
    if (empty() || seq <= oldest_){
        return 0;
    }
    int count = 0;
    for (auto& d : data_){
        if (d.sequence >= 0 && d.sequence < seq){
            d.sequence = -1;
            count++;
        }
    }
    size_ -= count;
    assert(size_ >= 0);
    oldest_ = seq;
    return count;
}

void block_ack_list::rehash(){
    auto newsize = data_.size() << 1; // double the size
    auto newmask = newsize - 1;
    std::vector<block_ack> temp(newsize);
#if LOGLEVEL >= 3
    LOG_DEBUG("before rehash:");
    std::cerr << *this << std::endl;
#endif
    // transfer items
    for (auto& b : data_){
        if (b.sequence >= 0){
            auto index = b.sequence & newmask;
            while (temp[index].sequence >= 0){
                index = (index + increment_) & newmask;
            }
            // insert item
            temp[index] = block_ack { b.sequence, limit_ };
        }
    }
    data_ = std::move(temp);
    mask_ = newmask;
#if LOGLEVEL >= 3
    LOG_DEBUG("after rehash:");
    std::cerr << *this << std::endl;
#endif
}

std::ostream& operator<<(std::ostream& os, const block_ack_list& b){
    os << "acklist (" << b.size() << " / " << b.data_.size() << "): ";
    for (auto& d : b.data_){
        if (d.sequence >= 0){
            os << d.sequence << " ";
        }
    }
    return os;
}

#else

block_ack_list::block_ack_list(){}

void block_ack_list::setup(int32_t limit){
    limit_ = limit;
}

void block_ack_list::clear(){
    data_.clear();
}

int32_t block_ack_list::size() const {
    return data_.size();
}

bool block_ack_list::empty() const {
    return data_.empty();
}

block_ack * block_ack_list::find(int32_t seq){
#if BLOCK_ACK_LIST_SORTED
    // binary search
    auto it = lower_bound(seq);
    if (it != data_.end() && it->sequence == seq){
        return &*it;
    }
#else
    // linear search
    for (auto& b : data_){
        if (b.sequence == seq){
            return &b;
        }
    }
#endif
    return nullptr;
}

block_ack& block_ack_list::get(int32_t seq){
#if BLOCK_ACK_LIST_SORTED
    auto it = lower_bound(seq);
    // insert if needed
    if (it == data_.end() || it->sequence != seq){
        it = data_.emplace(it, seq, limit_);
    }
    return *it;
#else
    auto b = find(seq);
    if (b){
        return *b;
    } else {
        data_.emplace_back(seq, limit_);
        return data_.back();
    }
#endif
}

bool block_ack_list::remove(int32_t seq){
#if BLOCK_ACK_LIST_SORTED
    auto it = lower_bound(seq);
    if (it != data_.end() && it->sequence == seq){
        data_.erase(it);
        return true;
    }
#else
    for (auto it = data_.begin(); it != data_.end(); ++it){
        if (it->sequence == seq){
            data_.erase(it);
            return true;
        }
    }
#endif
    return false;
}

int32_t block_ack_list::remove_before(int32_t seq){
    if (empty()){
        return 0;
    }
    int count = 0;
#if BLOCK_ACK_LIST_SORTED
    auto begin = data_.begin();
    auto end = lower_bound(seq);
    count = end - begin;
    data_.erase(begin, end);
#else
    for (auto it = data_.begin(); it != data_.end(); ){
        if (it->sequence < seq){
            it = data_.erase(it);
            count++;
        } else {
            ++it;
        }
    }
#endif
    return count;
}

#if BLOCK_ACK_LIST_SORTED
std::vector<block_ack>::iterator block_ack_list::lower_bound(int32_t seq){
    return std::lower_bound(data_.begin(), data_.end(), seq, [](auto& a, auto& b){
        return a.sequence < b;
    });
}
#endif

std::ostream& operator<<(std::ostream& os, const block_ack_list& b){
    os << "acklist (" << b.size() << "): ";
    for (auto& d : b.data_){
        os << d.sequence << " ";
    }
    return os;
}

#endif

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

void history_buffer::push(int32_t seq, double sr,
                          const char *data, int32_t nbytes,
                          int32_t nframes, int32_t framesize)
{
    if (buffer_.empty()){
        return;
    }
    assert(data != nullptr && nbytes > 0);
    // check if we're going to overwrite an existing block
    if (buffer_[head_].sequence >= 0){
        oldest_ = buffer_[head_].sequence;
    }
    buffer_[head_].set(seq, sr, 0, data, nbytes, nframes, framesize);
    if (++head_ >= (int32_t)buffer_.size()){
        head_ = 0;
    }
}

/*////////////////////////// block_queue /////////////////////////////*/

void block_queue::clear(){
    size_ = 0;
}

void block_queue::resize(int32_t n){
    blocks_.resize(n);
    size_ = 0;
}

bool block_queue::empty() const {
    return size_ == 0;
}

bool block_queue::full() const {
    return size_ == capacity();
}

int32_t block_queue::size() const {
    return size_;
}

int32_t block_queue::capacity() const {
    return blocks_.size();
}

block* block_queue::insert(int32_t seq, double sr, int32_t chn,
              int32_t nbytes, int32_t nframes){
    assert(capacity() > 0);
    // find pos to insert
    block * it;
    // first try the end, as it is the most likely position
    // (blocks usually arrive in sequential order)
    if (empty() || seq > back().sequence){
        it = end();
    } else {
    #if 0
        // linear search
        it = begin();
        for (; it != end(); ++it){
            assert(it->sequence != seq);
            if (it->sequence > seq){
                break;
            }
        }
    #else
        // binary search
        it = std::lower_bound(begin(), end(), seq, [](auto& a, auto& b){
            return a.sequence < b;
        });
        assert(!(it != end() && it->sequence == seq));
    #endif
    }
    // move items if needed
    if (full()){
        if (it > begin()){
            LOG_DEBUG("insert block at pos " << (it - begin()) << " and pop old block");
            // save first block
            block temp = std::move(front());
            // move blocks before 'it' to the left
            std::move(begin() + 1, it, begin());
            // adjust iterator and re-insert removed block
            *(--it) = std::move(temp);
        } else {
            // simply replace first block
            LOG_DEBUG("replace oldest block");
        }
    } else {
        if (it != end()){
            LOG_DEBUG("insert block at pos " << (it - begin()));
            // save block past the end
            block temp = std::move(*end());
            // move blocks to the right
            std::move_backward(it, end(), end() + 1);
            // re-insert removed block at free slot (first moved item)
            *it = std::move(temp);
        } else {
            // simply replace block past the end
            LOG_DEBUG("append block");
        }
        size_++;
    }
    // replace data
    it->set(seq, sr, chn, nbytes, nframes);
    return it;
}

block* block_queue::find(int32_t seq){
    // first try the end, as we most likely have to complete the most recent block
    if (empty()){
        return nullptr;
    } else if (back().sequence == seq){
        return &back();
    }
#if 0
    // linear search
    for (int32_t i = 0; i < size_; ++i){
        if (blocks_[i].sequence == seq){
            return &blocks_[i];
        }
    }
#else
    // binary search
    auto result = std::lower_bound(begin(), end(), seq, [](auto& a, auto& b){
        return a.sequence < b;
    });
    if (result != end() && result->sequence == seq){
        return result;
    }
#endif
    return nullptr;
}

void block_queue::pop_front(){
    assert(!empty());
    if (size_ > 1){
        // temporarily remove first block
        block temp = std::move(front());
        // move remaining blocks to the left
        std::move(begin() + 1, end(), begin());
        // re-insert removed block at free slot
        back() = std::move(temp);
    }
    size_--;
}

void block_queue::pop_back(){
    assert(!empty());
    size_--;
}

block& block_queue::front(){
    assert(!empty());
    return blocks_.front();
}

block& block_queue::back(){
    assert(!empty());
    return blocks_[size_ - 1];
}

block* block_queue::begin(){
    return blocks_.data();
}

block* block_queue::end(){
    return blocks_.data() + size_;
}

block& block_queue::operator[](int32_t i){
    return blocks_[i];
}

std::ostream& operator<<(std::ostream& os, const block_queue& b){
    os << "blockqueue (" << b.size() << " / " << b.capacity() << "): ";
    for (int i = 0; i < b.size(); ++i){
        os << b.blocks_[i].sequence << " ";
    }
    return os;
}

/*////////////////////////// dynamic_resampler /////////////////////////////*/

#define AOO_RESAMPLER_SPACE 3

void dynamic_resampler::setup(int32_t nfrom, int32_t nto, int32_t srfrom, int32_t srto, int32_t nchannels){
    nchannels_ = nchannels;
    auto blocksize = std::max<int32_t>(nfrom, nto);
#if 0
    // this doesn't work as expected...
    auto ratio = srfrom > srto ? (double)srfrom / (double)srto : (double)srto / (double)srfrom;
    buffer_.resize(blocksize * nchannels_ * ratio * AOO_RESAMPLER_SPACE); // extra space for fluctuations
#else
    buffer_.resize(blocksize * nchannels_ * AOO_RESAMPLER_SPACE); // extra space for fluctuations
#endif
    clear();
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
    if (debug_counter == 100){
        DO_LOG("srfrom: " << srfrom << ", srto: " << srto);
        DO_LOG("resample factor: " << ratio_);
        DO_LOG("balance: " << balance_ << ", size: " << buffer_.size());
        debug_counter = 0;
    } else {
        debug_counter++;
    }
#endif
}

int32_t dynamic_resampler::write_available(){
    return (double)buffer_.size() - balance_ + 0.5; // !
}

void dynamic_resampler::write(const aoo_sample *data, int32_t n){
    auto size = (int32_t)buffer_.size();
    auto end = wrpos_ + n;
    int32_t n1, n2;
    if (end > size){
        n1 = size - wrpos_;
        n2 = end - size;
    } else {
        n1 = n;
        n2 = 0;
    }
    std::copy(data, data + n1, &buffer_[wrpos_]);
    std::copy(data + n1, data + n, &buffer_[0]);
    wrpos_ += n;
    if (wrpos_ >= size){
        wrpos_ -= size;
    }
    balance_ += n;
}

int32_t dynamic_resampler::read_available(){
    return balance_ * ratio_;
}

void dynamic_resampler::read(aoo_sample *data, int32_t n){
    auto size = (int32_t)buffer_.size();
    auto limit = size / nchannels_;
    int32_t intpos = (int32_t)rdpos_;
    if (ratio_ != 1.0 || (rdpos_ - intpos) != 0.0){
        // interpolating version
        double incr = 1. / ratio_;
        assert(incr > 0);
        for (int i = 0; i < n; i += nchannels_){
            int32_t index = (int32_t)rdpos_;
            double fract = rdpos_ - (double)index;
            for (int j = 0; j < nchannels_; ++j){
                double a = buffer_[index * nchannels_ + j];
                double b = buffer_[((index + 1) * nchannels_ + j) % size];
                data[i + j] = a + (b - a) * fract;
            }
            rdpos_ += incr;
            if (rdpos_ >= limit){
                rdpos_ -= limit;
            }
        }
        balance_ -= n * incr;
    } else {
        // non-interpolating (faster) version
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
        rdpos_ += n / nchannels_;
        if (rdpos_ >= limit){
            rdpos_ -= limit;
        }
        balance_ -= n;
    }
}

} // aoo

void aoo_setup(){
    aoo_codec_pcm_setup(aoo::register_codec);
    aoo_codec_opus_setup(aoo::register_codec);
}

void aoo_close() {}
