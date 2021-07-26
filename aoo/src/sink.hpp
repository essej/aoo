/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_sink.hpp"
#if USE_AOO_NET
# include "aoo/aoo_client.hpp"
#endif

#include "common/lockfree.hpp"
#include "common/net_utils.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "buffer.hpp"
#include "codec.hpp"
#include "imp.hpp"
#include "resampler.hpp"
#include "timer.hpp"
#include "time_dll.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

namespace aoo {

struct stream_state {
    int32_t lost = 0;
    int32_t reordered = 0;
    int32_t resent = 0;
    int32_t dropped = 0;
};

class source_desc;

struct event
{
    event() = default;

    event(AooEventType type) : type_(type) {}

    event(AooEventType type, const source_desc& desc);

    union {
        AooEventType type_;
        AooEvent event_;
        AooEventEndpoint source;
        AooEventFormatChange format;
        AooEventFormatTimeout format_timeout;
        AooEventPing ping;
        AooEventStreamState source_state;
        AooEventBlockLost block_lost;
        AooEventBlockReordered block_reordered;
        AooEventBlockResent block_resent;
        AooEventBlockDropped block_dropped;
    };
};

struct sink_event {
    sink_event() = default;

    sink_event(AooEventType _type) : type(_type) {}
    sink_event(AooEventType _type, const source_desc& desc);

    AooEventType type;
    ip_address address;
    AooId id;
    int32_t count; // for xrun event
};

enum class request_type {
    unknown,
    format,
    ping_reply,
    invite,
    uninvite,
    uninvite_all
};

// used in 'source_desc'
struct request {
    request(request_type _type = request_type::unknown)
        : type(_type){}

    request_type type;
    union {
        struct {
            uint64_t tt1;
            uint64_t tt2;
        } ping;
    };
};

// used in 'sink'
struct source_request {
    source_request() = default;

    source_request(request_type _type)
        : type(_type) {}

    source_request(request_type _type, const ip_address& _addr, AooId _id)
        : type(_type), address(_addr), id(_id) {}

    request_type type;
    ip_address address;
    AooId id = -1;
};

class sink_imp;

enum class source_state {
    idle,
    stream,
    invite,
    uninvite
};

struct net_packet : data_packet {
    int32_t stream_id;
};

class source_desc {
public:
    source_desc(const ip_address& addr, AooId id,
                uint32_t flags, double time);

    source_desc(const source_desc& other) = delete;
    source_desc& operator=(const source_desc& other) = delete;

    ~source_desc();

    // getters
    AooId id() const { return id_; }

    const ip_address& address() const { return addr_; }

    bool match(const ip_address& addr, AooId id) const {
        return (addr_ == addr) && (id_ == id);
    }

    bool is_active(const sink_imp& s) const;

    bool is_inviting() const {
        return state_.load() == source_state::invite;
    }

    bool has_events() const {
        return !eventqueue_.empty();
    }

    int32_t poll_events(sink_imp& s, AooEventHandler fn, void *user);

    AooError get_format(AooFormat& format, size_t size);

    // methods
    void reset(const sink_imp& s);

    AooError handle_format(const sink_imp& s, int32_t stream_id, const AooFormat& f,
                           const AooByte *settings, int32_t size, uint32_t flags);

    AooError handle_data(const sink_imp& s, net_packet& d, bool binary);

    AooError handle_ping(const sink_imp& s, time_tag tt);

    void send(const sink_imp& s, const sendfn& fn);

    bool process(const sink_imp& s, AooSample **buffer, int32_t nsamples, time_tag tt);

    void invite(const sink_imp& s);

    void uninvite(const sink_imp& s);

    AooError request_format(const sink_imp& s, const AooFormat& f);

    float get_buffer_fill_ratio();

    void add_xrun(int32_t nsamples){
        xrunsamples_ += nsamples;
    }
private:
    using shared_lock = sync::shared_lock<sync::shared_mutex>;
    using unique_lock = sync::unique_lock<sync::shared_mutex>;
    using scoped_lock = sync::scoped_lock<sync::shared_mutex>;
    using scoped_shared_lock = sync::scoped_shared_lock<sync::shared_mutex>;

    void update(const sink_imp& s);

    void add_lost(stream_state& state, int32_t n);

    void handle_underrun(const sink_imp& s);

    bool add_packet(const sink_imp& s, const net_packet& d,
                    stream_state& state);

    void process_blocks(const sink_imp& s, stream_state& state);

    void skip_blocks(const sink_imp& s);

    void check_missing_blocks(const sink_imp& s);

    // send messages
    void send_ping_reply(const sink_imp& s, const sendfn& fn,
                         const request& r);

    void send_format_request(const sink_imp& s, const sendfn& fn,
                             bool format = false);

    void send_data_requests(const sink_imp& s, const sendfn& fn);

    void send_invitation(const sink_imp& s, const sendfn& fn);

    void send_uninvitation(const sink_imp& s, const sendfn& fn);

    // data
    const ip_address addr_;
    const AooId id_;
    uint32_t flags_;
    int32_t stream_id_ = -1; // start with invalid stream ID!

    AooStreamState streamstate_;
    bool underrun_{false};
    bool didupdate_{false};
    std::atomic<bool> binary_{false};

    std::atomic<source_state> state_{source_state::idle};

    std::unique_ptr<AooFormat, format_deleter> format_request_;
    double format_time_ = 0;

    std::atomic<double> state_time_{0.0};
    std::atomic<double> last_packet_time_{0};
    std::atomic<int32_t> lost_since_ping_{0};
    // audio decoder
    std::unique_ptr<aoo::decoder> decoder_;
    // state
    int32_t channel_ = 0; // recent channel onset
    int32_t skipblocks_ = 0;
    float xrun_ = 0;
    int32_t xrunsamples_ = 0;
    // resampler
    dynamic_resampler resampler_;
    // queues and buffers
    struct block_data {
        struct {
            double samplerate;
            int32_t channel;
            int32_t padding;
        } header;
        AooSample data[1];
    };
    lockfree::spsc_queue<char, aoo::allocator<char>> audioqueue_;
    int32_t minblocks_ = 0;
    // packet queue and jitter buffer
    lockfree::unbounded_mpsc_queue<net_packet, aoo::allocator<net_packet>> packetqueue_;
    jitter_buffer jitterbuffer_;
    // requests
    lockfree::unbounded_mpsc_queue<request, aoo::allocator<request>> requestqueue_;
    void push_request(const request& r){
        requestqueue_.push(r);
    }
    struct data_request {
        int32_t sequence;
        int32_t frame;
    };
    lockfree::unbounded_mpsc_queue<data_request, aoo::allocator<data_request>> datarequestqueue_;
    void push_data_request(const data_request& r){
        datarequestqueue_.push(r);
    }
    // events
    lockfree::unbounded_mpsc_queue<event, aoo::allocator<event>> eventqueue_;
    void send_event(const sink_imp& s, const event& e, AooThreadLevel level);
    // memory
    aoo::memory_list memory_;
    // thread synchronization
    sync::shared_mutex mutex_; // LATER replace with a spinlock?
};

class sink_imp final : public AooSink {
public:
    sink_imp(AooId id, AooFlag flags, AooError *err);

    ~sink_imp();

    AooError AOO_CALL setup(AooSampleRate samplerate,
                            AooInt32 blocksize, AooInt32 nchannels) override;

    AooError AOO_CALL handleMessage(const AooByte *data, AooInt32 n,
                                    const void *address, AooAddrSize addrlen) override;

    AooError AOO_CALL send(AooSendFunc fn, void *user) override;

    AooError AOO_CALL process(AooSample **data, AooInt32 nsamples,
                              AooNtpTime t) override;

    AooError AOO_CALL setEventHandler(AooEventHandler fn, void *user,
                                      AooEventMode mode) override;

    AooBool AOO_CALL eventsAvailable() override;

    AooError AOO_CALL pollEvents() override;

    AooError AOO_CALL control(AooCtl ctl, AooIntPtr index,
                              void *ptr, AooSize size) override;

    // getters
    AooId id() const { return id_.load(std::memory_order_relaxed); }

    int32_t nchannels() const { return nchannels_; }

    int32_t samplerate() const { return samplerate_; }

    AooSampleRate real_samplerate() const { return realsr_.load(std::memory_order_relaxed); }

    bool dynamic_resampling() const { return dynamic_resampling_.load(std::memory_order_relaxed);}

    int32_t blocksize() const { return blocksize_; }

    AooSeconds buffersize() const { return buffersize_.load(std::memory_order_relaxed); }

    int32_t packetsize() const { return packetsize_.load(std::memory_order_relaxed); }

    bool resend_enabled() const { return resend_.load(std::memory_order_relaxed); }

    AooSeconds resend_interval() const { return resend_interval_.load(std::memory_order_relaxed); }

    int32_t resend_limit() const { return resend_limit_.load(std::memory_order_relaxed); }

    AooSeconds source_timeout() const { return source_timeout_.load(std::memory_order_relaxed); }

    AooSeconds elapsed_time() const { return timer_.get_elapsed(); }

    time_tag absolute_time() const { return timer_.get_absolute(); }

    AooEventMode event_mode() const { return eventmode_; }

    void call_event(const event& e, AooThreadLevel level) const;
private:
    // settings
    std::atomic<AooId> id_;
    int32_t nchannels_ = 0;
    int32_t samplerate_ = 0;
    int32_t blocksize_ = 0;
#if USE_AOO_NET
    AooClient *client_ = nullptr;
#endif
    // the sources
    using source_list = lockfree::simple_list<source_desc, aoo::allocator<source_desc>>;
    using source_lock = std::unique_lock<source_list>;
    source_list sources_;
    sync::mutex source_mutex_;
    // timing
    std::atomic<AooSampleRate> realsr_{0};
    time_dll dll_;
    timer timer_;
    // options
#if __cplusplus >= 201703L
    static_assert(std::atomic<AooSeconds>::is_always_lock_free,
                  "AooSeconds is not lockfree!");
#endif

    std::atomic<AooSeconds> buffersize_{ AOO_SINK_BUFFER_SIZE };
    std::atomic<AooSeconds> resend_interval_{ AOO_RESEND_INTERVAL };
    std::atomic<int32_t> packetsize_{ AOO_PACKET_SIZE };
    std::atomic<int32_t> resend_limit_{ AOO_RESEND_LIMIT };
    std::atomic<AooSeconds> source_timeout_{ AOO_SOURCE_TIMEOUT };
    std::atomic<double> dll_bandwidth_{ AOO_DLL_BANDWIDTH };
    std::atomic<bool> resend_{AOO_RESEND_DATA};
    std::atomic<bool> dynamic_resampling_{ AOO_DYNAMIC_RESAMPLING };
    std::atomic<bool> timer_check_{ AOO_TIMER_CHECK };
    // events
    lockfree::unbounded_mpsc_queue<sink_event, aoo::allocator<sink_event>> eventqueue_;
    void send_event(const sink_event& e, AooThreadLevel level);
    AooEventHandler eventhandler_ = nullptr;
    void *eventcontext_ = nullptr;
    AooEventMode eventmode_ = kAooEventModeNone;
    // requests
    lockfree::unbounded_mpsc_queue<source_request, aoo::allocator<source_request>> requestqueue_;
    void push_request(const source_request& r){
        requestqueue_.push(r);
    }
    // helper method

    source_desc *find_source(const ip_address& addr, AooId id);

    source_desc *get_source_arg(intptr_t index);

    source_desc *add_source(const ip_address& addr, AooId id);

    void reset_sources();

    AooError handle_format_message(const osc::ReceivedMessage& msg,
                                   const ip_address& addr);

    AooError handle_data_message(const osc::ReceivedMessage& msg,
                                 const ip_address& addr);

    AooError handle_data_message(const AooByte *msg, int32_t n,
                                 const ip_address& addr);

    AooError handle_data_packet(net_packet& d, bool binary,
                                const ip_address& addr, AooId id);

    AooError handle_ping_message(const osc::ReceivedMessage& msg,
                                 const ip_address& addr);
};

} // aoo
