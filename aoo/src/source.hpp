/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo_source.hpp"
#if AOO_NET
# include "aoo_client.hpp"
#endif

#include "common/lockfree.hpp"
#include "common/net_utils.hpp"
#include "common/priority_queue.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "binmsg.hpp"
#include "packet_buffer.hpp"
#include "detail.hpp"
#include "events.hpp"
#include "resampler.hpp"
#include "time_dll.hpp"

#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"

#include <list>

namespace aoo {

class Source;

struct data_request {
    int32_t sequence;
    int16_t offset;
    uint16_t bitset;
};

enum class request_type {
    none,
    stop,
    decline,
    pong
};

struct sink_request {
    sink_request() = default;

    sink_request(request_type _type)
        : type(_type) {}

    sink_request(request_type _type, const endpoint& _ep)
        : type(_type), ep(_ep) {}

    request_type type;
    endpoint ep;
    union {
        struct {
            int32_t stream;
            int32_t offset;
        } stop;
        struct {
            int32_t token;
        } decline;
        struct {
            AooNtpTime tt1;
            AooNtpTime tt2;
        } pong;
    };
};

// NOTE: the stream ID can change anytime, it only
// has to be synchronized with any format change.
struct sink_desc {
#if AOO_NET
    sink_desc(const ip_address& addr, const ip_address& relay,
              int32_t id, bool binary, AooId stream_id)
        : ep(addr, relay, id, binary), stream_id_(stream_id) {}
#else
    sink_desc(const ip_address& addr, int32_t id, bool binary, AooId stream_id)
        : ep(addr, id, binary), stream_id_(stream_id) {}
#endif // USE_AOO_NEt
    sink_desc(const sink_desc& other) = delete;
    sink_desc& operator=(const sink_desc& other) = delete;

    const endpoint ep;

    AooId stream_id() const {
        return stream_id_.load(std::memory_order_acquire);
    }

    void set_channel(int32_t chn){
        channel_.store(chn, std::memory_order_relaxed);
    }

    int32_t channel() const {
        return channel_.load(std::memory_order_relaxed);
    }

    // a new stream has been started by the user;
    // called while (try-)locked
    void start();

    // the stream has been stopped by the user
    void stop(Source& s, int32_t offset);

    // (de)activate a source; if the stream is running,
    // this will also queue a /start resp. /stop message.
    void activate(Source& s, bool b);

    bool is_active() const {
        return stream_id_.load(std::memory_order_acquire) != kAooIdInvalid;
    }

    bool need_invite(AooId token);

    void handle_invite(Source& s, AooId token, bool accept);

    bool need_uninvite(AooId stream_id);

    void handle_uninvite(Source& s, AooId token, bool accept);

    // tell that we need to send a /start message
    void notify_start() {
        needstart_.exchange(true, std::memory_order_release);
    }

    // check if we need to send a /start message
    bool need_start() {
        return needstart_.exchange(false, std::memory_order_acquire);
    }

    void push_data_request(const data_request& r){
        data_requests_.push(r);
    }

    bool get_data_request(data_request& r){
        return data_requests_.pop(r);
    }
private:
    std::atomic<int32_t> channel_{0};
    std::atomic<int32_t> stream_id_ {kAooIdInvalid};
    int32_t invite_token_{kAooIdInvalid};
    int32_t uninvite_token_{kAooIdInvalid};
    std::atomic<bool> needstart_{false};
    aoo::concurrent_queue<data_request, false> data_requests_;
};

struct cached_sink {
    cached_sink(const sink_desc& s)
        : ep(s.ep), stream_id(s.stream_id()), channel(s.channel()) {}

    endpoint ep;
    AooId stream_id;
    int32_t channel;
};

template<typename Alloc>
struct stream_message : Alloc {
    stream_message() = default;
    stream_message(uint64_t _time, int32_t _channel,
                   AooDataType _type, const char *_data, int32_t _size)
        : time(_time), channel(_channel), type(_type), size(_size) {
        data = (char *)Alloc::allocate(size);
        memcpy(data, _data, size);
    }

    ~stream_message() {
        if (data) Alloc::deallocate(data, size);
    }

    stream_message(stream_message&& other) noexcept
        : time(other.time), channel(other.channel),
          type(other.type), data(other.data), size(other.size) {
        other.data = nullptr;
        other.size = 0;
    }

    stream_message& operator=(stream_message&& other) noexcept {
        time = other.time;
        channel = other.channel;
        type = other.type;
        data = other.data;
        size = other.size;
        other.data = nullptr;
        other.size = 0;
        return *this;
    }

    uint64_t time = 0;
    int32_t channel = 0;
    AooDataType type = 0;
    char *data = nullptr;
    int32_t size = 0;
};

using rt_stream_message = stream_message<rt_allocator<char>>;
using nrt_stream_message = stream_message<aoo::allocator<char>>;

class stream_message_comp {
public:
    template<typename T>
    bool operator()(const T& a, const T& b) const {
        return a.time > b.time;
    }
};


class Source final : public AooSource, rt_memory_pool_client {
 public:
    Source(AooId id);

    ~Source();

    //--------------------- interface methods --------------------------//

    AooError AOO_CALL setup(AooInt32 numChannels, AooSampleRate sampleRate,
                            AooInt32 blockSize, AooFlag flags) override;

    AooError AOO_CALL handleMessage(const AooByte *data, AooInt32 n,
                                    const void *address, AooAddrSize addrlen) override;

    AooError AOO_CALL send(AooSendFunc fn, void *user) override;

    AooError AOO_CALL addStreamMessage(const AooStreamMessage& message) override;

    AooError AOO_CALL process(AooSample **data, AooInt32 n, AooNtpTime t) override;

    AooError AOO_CALL setEventHandler(AooEventHandler fn, void *user, AooEventMode mode) override;

    AooBool AOO_CALL eventsAvailable() override;

    AooError AOO_CALL pollEvents() override;

    AooError AOO_CALL startStream(AooInt32 sampleOffset, const AooData *metadata) override;

    AooError AOO_CALL stopStream(AooInt32 sampleOffset) override;

    AooError AOO_CALL addSink(const AooEndpoint& sink, AooBool active) override;

    AooError AOO_CALL removeSink(const AooEndpoint& sink) override;

    AooError AOO_CALL removeAllSinks() override;

    AooError AOO_CALL handleInvite(const AooEndpoint& sink, AooId token, AooBool accept) override;

    AooError AOO_CALL handleUninvite(const AooEndpoint& sink, AooId token, AooBool accept) override;

    AooError AOO_CALL control(AooCtl ctl, AooIntPtr index,
                              void *ptr, AooSize size) override;

    AooError AOO_CALL codecControl(const AooChar *codec, AooCtl ctl, AooIntPtr index,
                                   void *ptr, AooSize size) override;

    //----------------------- semi-public methods -------------------//

    AooId id() const { return id_.load(); }

    bool is_running() const {
        auto state = stream_state_.load(std::memory_order_acquire) & stream_state_mask;
        return state == stream_state::run;
    }

    void notify_start();

    void push_request(const sink_request& r);
 private:
    using shared_lock = sync::shared_lock<sync::shared_mutex>;
    using unique_lock = sync::unique_lock<sync::shared_mutex>;
    using scoped_lock = sync::scoped_lock<sync::shared_mutex>;
    using scoped_shared_lock = sync::scoped_shared_lock<sync::shared_mutex>;
    using scoped_spinlock = sync::scoped_lock<sync::spinlock>;

    static constexpr int32_t invalid_stream = kAooIdInvalid;

    // settings
    parameter<AooId> id_;
    uint32_t flags_ = 0;
    int16_t nchannels_ = 0;
    int16_t blocksize_ = 0;
    int32_t samplerate_ = 0;
#if AOO_NET
    AooClient *client_ = nullptr;
#endif
    // audio encoder
    std::unique_ptr<AooFormat, format_deleter> format_;
    std::unique_ptr<AooCodec, encoder_deleter> encoder_;
    AooId format_id_ = kAooIdInvalid;
    int32_t sequence_ = invalid_stream;
    // metadata
    // state_ actually is a aoo::flat_metadata pointer,
    // but the lowest 4 bits contain the state.
    using stream_state_type = uintptr_t;
    static constexpr stream_state_type stream_state_mask = 15;
    static constexpr size_t stream_state_bits = 4;
    static constexpr stream_state_type metadata_mask = ~15;
    enum stream_state : stream_state_type {
        stop,
        start,
        run,
        idle
    };
    stream_state_type stream_state() const {
        return stream_state_.load(std::memory_order_relaxed) & stream_state_mask;
    }
    std::atomic<stream_state_type> stream_state_{stream_state::idle};
    rt_metadata_ptr metadata_;
    // timing
    uint64_t process_samples_ = 0;
    double stream_samples_ = 0;
    aoo::time_tag stream_tt_;
    aoo::time_tag start_tt_;
    std::atomic<float> xrunblocks_{0};
    std::atomic<float> last_ping_time_{0};
    std::atomic<float> elapsed_time_ = 0;
    std::atomic<bool> need_start_{false};
    std::atomic<bool> need_reset_timer_{false};
    void reset_timer() {
        need_reset_timer_.store(true);
    }
    // timing
    time_dll dll_;
    parameter<AooSampleRate> realsr_{0};
    // buffers and queues
    aoo::vector<AooByte> sendbuffer_;
    dynamic_resampler resampler_;
    struct block_data {
        static constexpr size_t header_size = 8;
        double sr;
        AooSample data[1];
    };
    aoo::spsc_queue<char> audio_queue_;
    history_buffer history_;
    using message_queue = lockfree::concurrent_queue<rt_stream_message, false, aoo::rt_allocator<rt_stream_message>>;
    message_queue message_queue_;
    using message_prio_queue = priority_queue<nrt_stream_message, stream_message_comp, aoo::allocator<nrt_stream_message>>;
    message_prio_queue message_prio_queue_;
    // events
    using event_queue = lockfree::concurrent_queue<event_ptr, true, aoo::rt_allocator<event_ptr>>;
    event_queue event_queue_;
    AooEventHandler event_handler_ = nullptr;
    void *event_context_ = nullptr;
    AooEventMode event_mode_ = kAooEventModeNone;
    // requests
    aoo::concurrent_queue<sink_request, true> requests_;
    // sinks
    using sink_list = aoo::concurrent_list<sink_desc>;
    using sink_lock = std::unique_lock<sink_list>;
    sink_list sinks_;
    sync::mutex sink_mutex_;
    aoo::vector<cached_sink> cached_sinks_; // only for the send thread
    // thread synchronization
    sync::shared_mutex update_mutex_;
    // options
    parameter<float> buffersize_{ AOO_SOURCE_BUFFER_SIZE };
    parameter<float> resend_buffersize_{ AOO_RESEND_BUFFER_SIZE };
    parameter<int32_t> packet_size_{ AOO_PACKET_SIZE };
    parameter<int32_t> redundancy_{ AOO_SEND_REDUNDANCY };
    parameter<float> ping_interval_{ AOO_PING_INTERVAL };
    parameter<float> dll_bandwidth_{ AOO_DLL_BANDWIDTH };
    parameter<float> tt_interval_{ AOO_STREAM_TIME_SEND_INTERVAL };
    parameter<bool> dynamic_resampling_{ AOO_DYNAMIC_RESAMPLING };
    parameter<bool> binary_{ AOO_BINARY_FORMAT };
    parameter<char> resample_method_{ AOO_RESAMPLE_MODE };

    // helper methods
    static void free_metadata(stream_state_type state);

    sink_desc * do_add_sink(const ip_address& addr, AooId id, AooId stream_id);

    bool do_remove_sink(const ip_address& addr, AooId id);

    AooError set_format(AooFormat& fmt);

    AooError get_format(AooFormat& fmt, size_t size);

    sink_desc * find_sink(const ip_address& addr, AooId id);

    sink_desc *get_sink_arg(intptr_t index);

    void send_event(event_ptr event, AooThreadLevel level);

    bool need_resampling() const;

    void restart_stream();

    void make_new_stream(aoo::time_tag tt, AooData *md);

    void add_xrun(int32_t nsamples);

    void handle_xrun(int32_t nsamples);

    void update_audio_queue();

    void update_resampler();

    void update_historybuffer();

    void dispatch_requests(const sendfn& fn);

    void send_start(const sendfn& fn);

    void send_xruns(const sendfn& fn);

    void send_data(const sendfn& fn);

    void resend_data(const sendfn& fn);

    void send_ping(const sendfn& fn);

    void handle_start_request(const osc::ReceivedMessage& msg,
                              const ip_address& addr);

    void handle_stop_request(const osc::ReceivedMessage& msg,
                              const ip_address& addr);

    void handle_data_request(const osc::ReceivedMessage& msg,
                             const ip_address& addr);

    void handle_data_request(const AooByte * msg, int32_t n,
                             AooId id, const ip_address& addr);

    void handle_ping(const osc::ReceivedMessage& msg,
                     const ip_address& addr);

    void handle_pong(const osc::ReceivedMessage& msg,
                     const ip_address& addr);

    void handle_invite(const osc::ReceivedMessage& msg,
                       const ip_address& addr);

    void handle_uninvite(const osc::ReceivedMessage& msg,
                         const ip_address& addr);
};

} // aoo
