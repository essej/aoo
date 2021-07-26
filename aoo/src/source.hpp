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

#include "codec.hpp"
#include "buffer.hpp"
#include "imp.hpp"
#include "resampler.hpp"
#include "timer.hpp"
#include "time_dll.hpp"

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscReceivedElements.h"

#include <list>

namespace aoo {

class source;

struct endpoint {
    endpoint() = default;
    endpoint(const ip_address& _address, int32_t _id, uint32_t _flags)
        : address(_address), id(_id), flags(_flags) {}

    // data
    ip_address address;
    AooId id = 0;
    uint32_t flags = 0;
};

struct data_request {
    int32_t sequence;
    int32_t frame;
};

struct sink_desc : endpoint {
    sink_desc(const ip_address& _addr, int32_t _id, uint32_t _flags)
        : endpoint(_addr, _id, _flags), channel(0) {}
    sink_desc(const sink_desc& other)
        : endpoint(other.address, other.id, other.flags),
          channel(other.channel.load()), needformat_(other.needformat_.load()) {}
    sink_desc& operator=(const sink_desc& other){
        address = other.address;
        id = other.id;
        flags = other.flags;
        channel = other.channel.load();
        needformat_ = other.needformat_.load();
        return *this;
    }

    // data
    std::atomic<int16_t> channel;
    lockfree::unbounded_mpsc_queue<data_request, aoo::allocator<data_request>> data_requests;

    void request_format(){
        needformat_.store(true, std::memory_order_release);
    }

    bool need_format() {
        return needformat_.exchange(false, std::memory_order_acquire);
    }

    void reset(){
        data_requests.clear();
    }
private:
    std::atomic<bool> needformat_{true}; // !
};

class source_imp final : public AooSource {
 public:
    struct event {
        event() = default;

        event(AooEventType type) : type_(type){}

        event(AooEventType type, const ip_address& addr, AooId id){
            memcpy(&addr_, addr.address(), addr.length());
            sink.type = type;
            sink.endpoint.address = &addr_;
            sink.endpoint.addrlen = addr.length();
            sink.endpoint.id = id;
        }

        event(const event& other){
            memcpy(this, &other, sizeof(event)); // ugh
            if (type_ != kAooEventXRun){
                sink.endpoint.address = &addr_;
            }
        }

        event& operator=(const event& other){
            memcpy(this, &other, sizeof(event)); // ugh
            if (type_ != kAooEventXRun){
                sink.endpoint.address = &addr_;
            }
            return *this;
        }

        union
        {
            AooEventType type_;
            AooEvent event_;
            AooEventEndpoint sink;
            AooEventPing ping;
            AooEventXRun xrun;
            AooEventFormat format;
        };
    private:
        char addr_[ip_address::max_length];
    };

    source_imp(AooId id, AooFlag flags, AooError *err);

    ~source_imp();

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
    AooId stream_id_ = kAooIdInvalid;
    int32_t nchannels_ = 0;
    int32_t blocksize_ = 0;
    int32_t samplerate_ = 0;
#if USE_AOO_NET
    AooClient *client_ = nullptr;
#endif
    // audio encoder
    std::unique_ptr<encoder> encoder_;
    // state
    int32_t sequence_ = 0;
    std::atomic<float> xrun_{0};
    std::atomic<float> lastpingtime_{0};
    std::atomic<bool> needformat_{false};
    enum class stream_state {
        stop,
        start,
        play
    };
    std::atomic<stream_state> state_{stream_state::stop};
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
    // sinks
    using sink_list = lockfree::simple_list<sink_desc, aoo::allocator<sink_desc>>;
    using sink_lock = std::unique_lock<sink_list>;
    sink_list sinks_;
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

    AooError set_format(AooFormat& fmt);

    sink_desc * find_sink(const ip_address& addr, AooId id);

    sink_desc *get_sink_arg(intptr_t index);

    static int32_t make_stream_id();

    void send_event(const event& e, AooThreadLevel level);

    bool need_resampling() const;

    void start_new_stream();

    void add_xrun(float n);

    void update_audioqueue();

    void update_resampler();

    void update_historybuffer();

    void send_format(const sendfn& fn);

    void send_data(const sendfn& fn);

    void resend_data(const sendfn& fn);

    void send_packet(const sendfn& fn, int32_t stream_id,
                     data_packet& d, bool binary);

    void send_packet_osc(const sendfn& fn, const endpoint& ep,
                         int32_t stream_id, const data_packet& d) const;

    void send_packet_bin(const sendfn& fn, const endpoint& ep,
                         int32_t stream_id, const data_packet& d) const;

    void write_bin_data(const endpoint* ep, int32_t stream_id,
                        const data_packet& d, AooByte *buf, int32_t& size) const;

    void send_ping(const sendfn& fn);

    void handle_format_request(const osc::ReceivedMessage& msg,
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
