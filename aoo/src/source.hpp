/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_source.hpp"
#if USE_AOO_NET
# include "aoo/aoo_client.hpp"
#endif

#include "common/lockfree.hpp"
#include "common/net_utils.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "buffer.hpp"
#include "imp.hpp"
#include "resampler.hpp"
#include "timer.hpp"
#include "time_dll.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#include <list>

namespace aoo {

struct data_request {
    int32_t sequence;
    int32_t frame;
};

struct event {
    event() = default;

    event(AooEventType type) : type_(type){}

    // NOTE: can't use aoo::endpoint
    event(AooEventType type, const ip_address& addr, AooId id);

    event(const event& other);

    event& operator=(const event& other);

    union
    {
        AooEventType type_;
        AooEvent event_;
        AooEventEndpoint sink;
        AooEventInvite invite;
        AooEventPing ping;
        AooEventXRun xrun;
    };
private:
    char addr_[ip_address::max_length];
};

enum class request_type {
    none,
    stop
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
        } stop;
    };
};

namespace send_flag {
    const uint32_t start = 0x01;
    const uint32_t stop = 0x02;
}

struct sink_desc {
    sink_desc(const ip_address& addr, int32_t id, uint32_t flags)
        : ep(addr, id, flags) {
        // set in constructor to avoid race conditions
        stream_id = get_random_id();
    }
    sink_desc(const sink_desc& other) = delete;
    sink_desc& operator=(const sink_desc& other) = delete;

    // data
    const endpoint ep;
    std::atomic<int32_t> channel{0};
    AooId stream_id {kAooIdInvalid};

    void start(){
        stream_id = get_random_id();
        data_requests_.clear();
        notify(send_flag::start);
    }

    void stop(){
        notify(send_flag::stop);
    }

    void notify(uint32_t what){
        send_.fetch_or(what, std::memory_order_release);
    }

    uint32_t need_send() {
        return send_.exchange(0, std::memory_order_acquire);
    }

    void add_data_request(int32_t sequence, int32_t frame){
        data_requests_.push(sequence, frame);
    }

    bool get_data_request(data_request& r){
        return data_requests_.try_pop(r);
    }
private:
    std::atomic<uint32_t> send_{0};
    lockfree::unbounded_mpsc_queue<data_request, aoo::allocator<data_request>> data_requests_;
};

struct cached_sink_desc {
    cached_sink_desc(const sink_desc& s, uint32_t _send = 0)
        : ep(s.ep), stream_id(s.stream_id), channel(s.channel.load()),
          send(_send) {}

    endpoint ep;
    AooId stream_id;
    int32_t channel;
    uint32_t send;
};

using cached_sink_vector = std::vector<cached_sink_desc, aoo::allocator<cached_sink_desc>>;

class Source final : public AooSource {
 public:
    Source(AooId id, AooFlag flags, AooError *err);

    ~Source();

    AooId id() const { return id_.load(std::memory_order_relaxed); }

    AooError AOO_CALL setup(AooSampleRate sampleRate,
                            AooInt32 blockSize, AooInt32 numChannels) override;

    AooError AOO_CALL handleMessage(const AooByte *data, AooInt32 n,
                                    const void *address, AooAddrSize addrlen) override;

    AooError AOO_CALL send(AooSendFunc fn, void *user) override;

    AooError AOO_CALL process(const AooSample **data, AooInt32 n,
                              AooNtpTime t) override;

    AooError AOO_CALL setEventHandler(AooEventHandler fn, void *user,
                                      AooEventMode mode) override;

    AooBool AOO_CALL eventsAvailable() override;

    AooError AOO_CALL pollEvents() override;

    AooError AOO_CALL control(AooCtl ctl, AooIntPtr index,
                              void *ptr, AooSize size) override;
 private:
    using shared_lock = sync::shared_lock<sync::shared_mutex>;
    using unique_lock = sync::unique_lock<sync::shared_mutex>;
    using scoped_lock = sync::scoped_lock<sync::shared_mutex>;
    using scoped_shared_lock = sync::scoped_shared_lock<sync::shared_mutex>;

    // settings
    std::atomic<AooId> id_;
    int32_t nchannels_ = 0;
    int32_t blocksize_ = 0;
    int32_t samplerate_ = 0;
#if USE_AOO_NET
    AooClient *client_ = nullptr;
#endif
    // audio encoder
    std::unique_ptr<AooFormat, format_deleter> format_;
    std::unique_ptr<AooCodec, encoder_deleter> encoder_;
    AooId format_id_ {kAooIdInvalid};
    // state
    int32_t sequence_ = 0;
    std::atomic<float> xrun_{0};
    std::atomic<float> lastpingtime_{0};
    std::atomic<uint32_t> needsend_{0};
    enum class stream_state {
        stop,
        start,
        run,
        idle
    };
    std::atomic<stream_state> state_{stream_state::idle};
    // metadata
    AooCustomData *metadata_{nullptr};
    std::atomic<int32_t> metadata_size_{ AOO_STREAM_METADATA_SIZE };
    bool metadata_accepted_{false};
    sync::spinlock metadata_lock_;
    // timing
    std::atomic<double> realsr_{0};
    time_dll dll_;
    timer timer_;
    // buffers and queues
    std::vector<AooByte, aoo::allocator<AooByte>> sendbuffer_;
    dynamic_resampler resampler_;
    struct block_data {
        double sr;
        AooSample data[1];
    };
    lockfree::spsc_queue<char, aoo::allocator<char>> audioqueue_;
    history_buffer history_;
    // events
    lockfree::unbounded_mpsc_queue<event, aoo::allocator<event>> eventqueue_;
    AooEventHandler eventhandler_ = nullptr;
    void *eventcontext_ = nullptr;
    AooEventMode eventmode_ = kAooEventModeNone;
    // requests
    lockfree::unbounded_mpsc_queue<sink_request, aoo::allocator<sink_request>> requests_;
    // sinks
    using sink_list = lockfree::simple_list<sink_desc, aoo::allocator<sink_desc>>;
    using sink_lock = std::unique_lock<sink_list>;
    sink_list sinks_;
    cached_sink_vector cached_sinks_; // only for the send thread
    // memory
    memory_list memory_;
    // thread synchronization
    sync::shared_mutex update_mutex_;
    // options
#if __cplusplus >= 201703L
    static_assert(std::atomic<AooSeconds>::is_always_lock_free,
                  "AooSeconds is not lockfree!");
#endif

    std::atomic<AooSeconds> buffersize_{ AOO_SOURCE_BUFFER_SIZE };
    std::atomic<AooSeconds> resend_buffersize_{ AOO_RESEND_BUFFER_SIZE };
    std::atomic<int32_t> packetsize_{ AOO_PACKET_SIZE };
    std::atomic<int32_t> redundancy_{ AOO_SEND_REDUNDANCY };
    std::atomic<double> dll_bandwidth_{ AOO_DLL_BANDWIDTH };
    std::atomic<AooSeconds> ping_interval_{ AOO_PING_INTERVAL };
    std::atomic<bool> dynamic_resampling_{ AOO_DYNAMIC_RESAMPLING };
    std::atomic<bool> timer_check_{ AOO_TIMER_CHECK };
    std::atomic<bool> binary_{ AOO_BINARY_DATA_MSG };

    // helper methods
    AooError add_sink(const AooEndpoint& sink, AooFlag flags);

    AooError remove_sink(const AooEndpoint& sink);

    AooError remove_all_sinks();

    AooError set_format(AooFormat& fmt);

    AooError get_format(AooFormat& fmt);

    AooError codec_control(AooCtl ctl, void *data, AooSize size);

    sink_desc * find_sink(const ip_address& addr, AooId id);

    sink_desc *get_sink_arg(intptr_t index);

    void notify(uint32_t what);

    uint32_t need_send();

    void send_event(const event& e, AooThreadLevel level);

    void free_event(const event& e);

    bool need_resampling() const;

    AooError start_stream(const AooCustomData *md);

    void make_new_stream();

    void allocate_metadata(int32_t size);

    void add_xrun(float n);

    void update_audioqueue();

    void update_resampler();

    void update_historybuffer();

    void dispatch_requests(const sendfn& fn);

    void send_stream(const sendfn& fn);

    void send_data(const sendfn& fn);

    void resend_data(const sendfn& fn);

    void send_ping(const sendfn& fn);

    void handle_start_request(const osc::ReceivedMessage& msg,
                              const ip_address& addr);

    void handle_data_request(const osc::ReceivedMessage& msg,
                             const ip_address& addr);

    void handle_data_request(const AooByte * msg, int32_t n,
                             const ip_address& addr);

    void handle_ping(const osc::ReceivedMessage& msg,
                     const ip_address& addr);

    void handle_invite(const osc::ReceivedMessage& msg,
                       const ip_address& addr);

    void handle_uninvite(const osc::ReceivedMessage& msg,
                         const ip_address& addr);
};

} // aoo
