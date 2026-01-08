/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo_sink.hpp"
#if AOO_NET
# include "aoo_client.hpp"
#endif

#include "common/lockfree.hpp"
#include "common/net_utils.hpp"
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

namespace aoo {

struct stream_stats {
    int32_t dropped = 0;
    int32_t resent = 0;
    int32_t xrun = 0;
};

enum class request_type {
    none,
    start,
    stop,
    pong,
    invite,
    uninvite,
    uninvite_all
};

// used in 'source_desc'
struct request {
    request(request_type _type = request_type::none)
        : type(_type){}

    request_type type;
    union {
        struct {
            AooNtpTime tt1;
            AooNtpTime tt2;
        } pong;
        struct {
            AooId token;
        } uninvite;
        struct {
            AooId stream;
        } stop;
    };
};

struct data_request {
    int32_t sequence;
    int16_t offset;
    uint16_t bitset;
};

// used in 'sink'
struct source_request {
    source_request() = default;

    source_request(request_type _type)
        : type(_type) {}

    // NOTE: can't use aoo::endpoint here
    source_request(request_type _type, const ip_address& _addr, AooId _id)
        : type(_type), id(_id), address(_addr) {}

    request_type type;
    AooId id = kAooIdInvalid;
    ip_address address;
    union {
        struct {
            AooId token;
            AooData *metadata;
        } invite;
    };
};

enum class source_state {
    idle,
    run,
    stop,
    start,
    invite,
    uninvite,
    timeout
};

enum class stream_state : uint8_t {
    active,
    inactive,
    buffering
};

struct net_packet : data_packet {
    int32_t stream_id;
};

struct stream_message_header {
    stream_message_header *next;
    double time; // in samples
    int16_t channel;
    int16_t type; // must be signed because of kAooDataStreamTime
    uint32_t size;
};

struct flat_stream_message {
    stream_message_header header;
    char data[1];
};

constexpr AooDataType kAooDataStreamTime = -1;

struct stream_time_message {
    stream_message_header header;
    AooNtpTime tt;
};

class Sink;

class source_desc {
public:
#if AOO_NET
    source_desc(const ip_address& addr, const ip_address& relay,
                AooId id, bool binary, double time);
#else
    source_desc(const ip_address& addr, AooId id, bool binary, double time);
#endif

    source_desc(const source_desc& other) = delete;
    source_desc& operator=(const source_desc& other) = delete;

    ~source_desc();

    bool match(const ip_address& addr, AooId id) const {
        return (ep.address == addr) && (ep.id == id);
    }

    bool check_active(const Sink& s);

    AooError get_format(AooFormat& format, size_t size);

    AooError codec_control(const AooChar *codec, AooCtl ctl,
                           void *data, AooSize size);

    // methods
    void reset(const Sink& s);

    AooError handle_start(const Sink& s, int32_t stream_id, int32_t seq_start,
                          int32_t format_id, const AooFormat& f,
                          const AooByte *ext_data, int32_t ext_size,
                          aoo::time_tag tt, int32_t latency, int32_t codec_delay,
                          const std::optional<AooData>& md, int32_t offset);

    AooError handle_stop(const Sink& s, int32_t stream,
                         int32_t last_seq, int32_t offset);

    AooError handle_decline(const Sink& s, int32_t token);

    AooError handle_data(const Sink& s, net_packet& d, bool binary);

    AooError handle_ping(const Sink& s, time_tag tt);

    AooError handle_pong(const Sink& s, time_tag tt1, time_tag tt2, time_tag tt3);

    void send(const Sink& s, const sendfn& fn);

    bool process(const Sink& s, AooSample **buffer, int32_t nsamples,
                 time_tag tt, AooStreamMessageHandler handler, void *user);

    void invite(const Sink& s, AooId token, AooData *metadata);

    void uninvite(const Sink& s);

    float get_buffer_fill_ratio();

    void add_xrun(double nblocks);
private:
    using shared_lock = sync::shared_lock<sync::shared_mutex>;
    using unique_lock = sync::unique_lock<sync::shared_mutex>;
    using scoped_lock = sync::scoped_lock<sync::shared_mutex>;
    using scoped_shared_lock = sync::scoped_shared_lock<sync::shared_mutex>;

    void update(const Sink& s);

    void check_latency(const Sink& s);

    void on_underrun(const Sink& s);

    void handle_underrun(const Sink& s);

    void handle_overrun(const Sink& s);

    bool add_packet(const Sink& s, const net_packet& d,
                    stream_stats& stats);

    bool try_decode_block(const Sink& s, AooSample* buffer, stream_stats& stats);

    void check_missing_blocks(const Sink& s);

    void sched_stream_message(stream_message_header *msg);

    void dispatch_stream_messages(const Sink& s, int nsamples,
                                  AooStreamMessageHandler fn, void *user);

    void flush_packet_queue();

    // send messages
    void send_ping(const Sink&s, const sendfn& fn);

    void send_pong(const Sink& s, time_tag tt1, time_tag tt2, const sendfn& fn);

    void send_start_request(const Sink& s, const sendfn& fn);

    void send_stop_request(const Sink& s, int32_t stream, const sendfn& fn);

    void send_data_requests(const Sink& s, const sendfn& fn);

    void send_invitations(const Sink& s, const sendfn& fn);
public:
    // data
    const endpoint ep;
private:
    AooId stream_id_ = kAooIdInvalid;
    AooId format_id_ = kAooIdInvalid;

    int16_t channel_ = 0; // recent channel onset
    stream_state stream_state_ = stream_state::inactive;
    bool did_update_{false};
    bool underrun_{false};
    bool stopped_{false};
    double xrunblocks_ = 0;
    aoo::time_tag stream_tt_;
    aoo::time_tag local_tt_;
    int32_t sample_offset_ = 0;
    int32_t stream_start_ = 0;
    int32_t source_latency_ = 0;
    int32_t sink_latency_ = 0;
    int32_t source_codec_delay_ = 0;
    int32_t sink_codec_delay_ = 0;
    int32_t buffer_latency_ = 0;

    std::atomic<source_state> state_{source_state::idle};
    rt_metadata_ptr metadata_;
    rt_metadata_ptr invite_metadata_;
    std::atomic<int32_t> invite_token_{kAooIdInvalid};

    // timing
    std::atomic<float> invite_start_time_{0};
    std::atomic<float> last_invite_time_{0};
    std::atomic<float> last_packet_time_{0};
    std::atomic<float> last_ping_time_{0};
    float last_stop_time_{0};
    // statistics
    std::atomic<int32_t> dropped_blocks_{0};
    time_tag last_ping_reply_time_;
    // audio decoder
    std::unique_ptr<AooFormat, format_deleter> format_;
    std::unique_ptr<AooCodec, decoder_deleter> decoder_;
    // resampler
    dynamic_resampler resampler_;
    // packet queue and jitter buffer
    // NB: frame_allocator_ must come *before* jitter_buffer_!
    data_frame_allocator frame_allocator_;
    aoo::concurrent_queue<net_packet, false> packet_queue_;
    jitter_buffer jitter_buffer_{frame_allocator_};
    int32_t latency_blocks_ = 0;
    int32_t latency_samples_ = 0;
    // stream messages
    stream_message_header *stream_messages_ = nullptr;
    double stream_samples_ = 0;
    int64_t process_samples_ = 0;
    void reset_stream();
    // requests
    aoo::concurrent_queue<request, true> request_queue_;
    void push_request(const request& r){
        request_queue_.push(r);
    }
    aoo::concurrent_queue<data_request, false> data_requests_;
    void push_data_request(const data_request& r) {
    #if AOO_DEBUG_RESEND && 0
        LOG_DEBUG("AooSink: push data request (" << r.sequence
                  << " " << r.offset << " " << r.bitset << ")");
    #endif
        data_requests_.push(r);
    }
    // events
    using event_buffer = std::vector<event_ptr, aoo::allocator<event_ptr>>;
    event_buffer event_buffer_;
    void queue_event(event_ptr e);
    void flush_events(const Sink& s);
    // thread synchronization
    sync::shared_mutex mutex_; // LATER replace with a spinlock?
};

class Sink final : public AooSink, rt_memory_pool_client {
public:
    Sink(AooId id);

    ~Sink();

    AooError AOO_CALL setup(AooInt32 nchannels, AooSampleRate samplerate,
                            AooInt32 blocksize, AooFlag flags) override;

    AooError AOO_CALL handleMessage(const AooByte *data, AooInt32 n,
                                    const void *address, AooAddrSize addrlen) override;

    AooError AOO_CALL send(AooSendFunc fn, void *user) override;

    AooError AOO_CALL process(AooSample **data, AooInt32 nsamples, AooNtpTime t,
                              AooStreamMessageHandler messageHandler, void *user) override;

    AooError AOO_CALL setEventHandler(AooEventHandler fn, void *user,
                                      AooEventMode mode) override;

    AooBool AOO_CALL eventsAvailable() override;

    AooError AOO_CALL pollEvents() override;

    AooError AOO_CALL inviteSource(
            const AooEndpoint& source, const AooData *metadata) override;

    AooError AOO_CALL uninviteSource(const AooEndpoint& source) override;

    AooError AOO_CALL uninviteAll() override;

    AooError AOO_CALL control(AooCtl ctl, AooIntPtr index,
                              void *ptr, AooSize size) override;

    AooError AOO_CALL codecControl(const AooChar *codec, AooCtl ctl,
            AooIntPtr index, void *data, AooSize size) override;

    // getters
    AooId id() const { return id_.load(); }

    int32_t nchannels() const { return nchannels_.load(std::memory_order_relaxed); }

    int32_t samplerate() const { return samplerate_.load(std::memory_order_relaxed); }

    int32_t blocksize() const { return blocksize_.load(std::memory_order_relaxed); }

    bool fixed_blocksize() const { return flags_.load(std::memory_order_relaxed) & kAooFixedBlockSize; }

    float elapsed_time() const { return elapsed_time_.load(std::memory_order_relaxed); }

    AooSampleRate real_samplerate() const { return realsr_.load(); }

    bool dynamic_resampling() const { return dynamic_resampling_.load(); }

    AooResampleMethod resample_method() const { return resample_method_.load(); }

    AooSeconds latency() const { return latency_.load(); }

    AooSeconds buffersize() const { return buffersize_.load(); }

    int32_t packet_size() const { return packet_size_.load(); }

    bool resend_enabled() const { return resend_.load(); }

    AooSeconds resend_interval() const { return resend_interval_.load(); }

    AooSeconds ping_interval() const { return ping_interval_.load(); }

    int32_t resend_limit() const { return resend_limit_.load(); }

    AooSeconds source_timeout() const { return source_timeout_.load(); }

    AooSeconds invite_timeout() const { return invite_timeout_.load(); }

    AooEventMode event_mode() const { return event_mode_; }

    void send_event(event_ptr e, AooThreadLevel level) const;
private:
    // settings
    parameter<AooId> id_;
    std::atomic<uint32_t> flags_{0};
    std::atomic<int16_t> nchannels_{0};
    std::atomic<int16_t> blocksize_{0};
    std::atomic<int32_t> samplerate_{0};
#if AOO_NET
    AooClient *client_ = nullptr;
#endif
    // the sources
    using source_list = aoo::concurrent_list<source_desc>;
    using source_lock = std::unique_lock<source_list>;
    source_list sources_;
    sync::mutex source_mutex_;
    // timing
    time_dll dll_;
    parameter<AooSampleRate> realsr_{0};
    AooNtpTime start_tt_{0};
    std::atomic<float> elapsed_time_ = 0;
    std::atomic<bool> need_reset_timer_{false};
    void reset_timer() {
        need_reset_timer_.store(true);
    }
    // options
    parameter<float> latency_{ AOO_SINK_LATENCY };
    parameter<float> buffersize_{ 0 };
    parameter<float> resend_interval_{ AOO_RESEND_INTERVAL };
    parameter<float> ping_interval_{ AOO_PING_INTERVAL };
    parameter<int32_t> packet_size_{ AOO_PACKET_SIZE };
    parameter<int32_t> resend_limit_{ AOO_RESEND_LIMIT };
    parameter<float> source_timeout_{ AOO_SOURCE_TIMEOUT };
    parameter<float> invite_timeout_{ AOO_INVITE_TIMEOUT };
    parameter<float> dll_bandwidth_{ AOO_DLL_BANDWIDTH };
    parameter<bool> resend_{ AOO_RESEND_DATA };
    parameter<bool> dynamic_resampling_{ AOO_DYNAMIC_RESAMPLING };
    parameter<bool> binary_{ AOO_BINARY_FORMAT };
    parameter<char> resample_method_{ AOO_RESAMPLE_MODE };

    // events
    using event_queue = lockfree::concurrent_queue<event_ptr, true, aoo::rt_allocator<event_ptr>>;
    mutable event_queue event_queue_;
    AooEventHandler event_handler_ = nullptr;
    void *event_context_ = nullptr;
    AooEventMode event_mode_ = kAooEventModeNone;
    // requests
    aoo::concurrent_queue<source_request, true> request_queue_;
    void push_request(const source_request& r) {
        request_queue_.push(r);
    }
    void dispatch_requests();

    // helper method

    source_desc *find_source(const ip_address& addr, AooId id);

    source_desc *get_source_arg(intptr_t index);

    source_desc *add_source(const ip_address& addr, AooId id);

    void reset_sources();

    void handle_xrun(int32_t nsamples);

    AooError handle_start_message(const osc::ReceivedMessage& msg,
                                  const ip_address& addr);

    AooError handle_stop_message(const osc::ReceivedMessage& msg,
                                 const ip_address& addr);

    AooError handle_decline_message(const osc::ReceivedMessage& msg,
                                    const ip_address& addr);

    AooError handle_data_message(const osc::ReceivedMessage& msg,
                                 const ip_address& addr);

    AooError handle_data_message(const AooByte *msg, int32_t n,
                                 AooId id, const ip_address& addr);

    AooError handle_data_packet(net_packet& d, bool binary,
                                const ip_address& addr, AooId id);

    AooError handle_ping_message(const osc::ReceivedMessage& msg,
                                 const ip_address& addr);

    AooError handle_pong_message(const osc::ReceivedMessage& msg,
                                 const ip_address& addr);
};

} // aoo
