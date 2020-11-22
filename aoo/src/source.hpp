/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo.hpp"

#include "common/lockfree.hpp"
#include "common/net_utils.hpp"
#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include "codec.hpp"
#include "buffer.hpp"
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
    endpoint(const ip_address& _address, int32_t _id)
        : address(_address), id(_id){}

    // data
    ip_address address;
    aoo_id id = 0;

    // methods
    void send_data(const source& s, aoo_id src, int32_t salt, const data_packet& data) const;

    void send_format(const source& s, aoo_id src, int32_t salt, const aoo_format& f,
                     const char *options, int32_t size) const;

    void send_ping(const source& s, aoo_id src, time_tag t) const;

    void send(const source& s, const char *data, int32_t n) const;
};

using format_request = endpoint;

struct data_request : endpoint {
    data_request() = default;
    data_request(const ip_address& _addr, int32_t _id,
                 int32_t _salt, int32_t _sequence, int32_t _frame)
        : endpoint(_addr, _id),
          salt(_salt), sequence(_sequence), frame(_frame){}
    int32_t salt = 0;
    int32_t sequence = 0;
    int32_t frame = 0;
};

struct invite_request : endpoint {
    enum type {
        INVITE,
        UNINVITE
    };

    invite_request() = default;
    invite_request(const ip_address& _addr, int32_t _id, int32_t _type)
        : endpoint(_addr, _id), type(_type){}
    int32_t type = 0;
};

struct sink_desc : endpoint {
    sink_desc(const ip_address& _addr, int32_t _id)
        : endpoint(_addr, _id), channel(0), format_changed(true) {}
    sink_desc(const sink_desc& other)
        : endpoint(other.address, other.id),
          channel(other.channel.load()),
          format_changed(other.format_changed.load()){}
    sink_desc& operator=(const sink_desc& other){
        address = other.address;
        id = other.id;
        channel = other.channel.load();
        format_changed = other.format_changed.load();
        return *this;
    }

    // data
    std::atomic<int16_t> channel;
    std::atomic<bool> format_changed;
};

class source final : public isource {
 public:
    struct event {
        event() = default;

        event(aoo_event_type type, const ip_address& addr, aoo_id id){
            memcpy(&addr_, addr.address(), addr.length());
            sink.type = type;
            sink.address = &addr_;
            sink.addrlen = addr.length();
            sink.id = id;
        }

        event(const event& other){
            memcpy(this, &other, sizeof(event)); // ugh
            sink.address = addr_;
        }

        event& operator=(const event& other){
            memcpy(this, &other, sizeof(event)); // ugh
            sink.address = addr_;
            return *this;
        }

        union
        {
            aoo_event_type type_;
            aoo_event event_;
            aoo_sink_event sink;
            aoo_ping_event ping;
        };
    private:
        char addr_[ip_address::max_length];
    };

    source(aoo_id id, aoo_replyfn replyfn, void *user);
    ~source();

    aoo_id id() const { return id_.load(std::memory_order_relaxed); }

    int32_t setup(int32_t samplerate, int32_t blocksize, int32_t nchannels) override;

    int32_t add_sink(const void *address, int32_t addrlen, aoo_id id) override;

    int32_t remove_sink(const void *address, int32_t addrlen, aoo_id id) override;

    void remove_all() override;

    int32_t handle_message(const char *data, int32_t n,
                           const void *address, int32_t addrlen) override;

    int32_t send() override;

    int32_t process(const aoo_sample **data, int32_t n, uint64_t t) override;

    int32_t events_available() override;

    int32_t poll_events(aoo_eventhandler fn, void *user) override;

    int32_t set_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t get_option(int32_t opt, void *ptr, int32_t size) override;

    int32_t set_sinkoption(const void *address, int32_t addrlen, aoo_id id,
                           int32_t opt, void *ptr, int32_t size) override;

    int32_t get_sinkoption(const void *address, int32_t addrlen, aoo_id id,
                           int32_t opt, void *ptr, int32_t size) override;

    int32_t do_send(const char *data, int32_t size, const ip_address& addr) const {
        return replyfn_(user_, data, size, addr.address(), addr.length());
    }
 private:
    // settings
    std::atomic<aoo_id> id_;
    aoo_replyfn replyfn_;
    void *user_;
    int32_t salt_ = 0;
    int32_t nchannels_ = 0;
    int32_t blocksize_ = 0;
    int32_t samplerate_ = 0;
    // audio encoder
    std::unique_ptr<encoder> encoder_;
    // state
    int32_t sequence_ = 0;
    std::atomic<int32_t> dropped_{0};
    std::atomic<float> lastpingtime_{0};
    std::atomic<bool> format_changed_{false};
    enum class stream_state {
        stop,
        start,
        play
    };
    std::atomic<stream_state> state_{stream_state::stop};
    // timing
    time_dll dll_;
    timer timer_;
    // buffers and queues
    std::vector<char> sendbuffer_;
    dynamic_resampler resampler_;
    lockfree::spsc_queue<aoo_sample> audioqueue_;
    lockfree::spsc_queue<double> srqueue_;
    lockfree::unbounded_mpsc_queue<event> eventqueue_;
    lockfree::unbounded_mpsc_queue<format_request> formatrequestqueue_;
    lockfree::unbounded_mpsc_queue<data_request> datarequestqueue_;
    history_buffer history_;
    // sinks
    std::list<sink_desc> sinks_; // don't move in memory!
    // thread synchronization
    aoo::shared_mutex update_mutex_;
    aoo::shared_mutex sink_mutex_;
    // options
    std::atomic<int32_t> buffersize_{ AOO_SOURCE_BUFSIZE };
    std::atomic<int32_t> packetsize_{ AOO_PACKETSIZE };
    std::atomic<int32_t> resend_buffersize_{ AOO_RESEND_BUFSIZE };
    std::atomic<int32_t> redundancy_{ AOO_SEND_REDUNDANCY };
    std::atomic<float> bandwidth_{ AOO_TIMEFILTER_BANDWIDTH };
    std::atomic<float> ping_interval_{ AOO_PING_INTERVAL * 0.001 };

    // helper methods
    int32_t set_format(aoo_format& f);

    sink_desc * find_sink(const ip_address& addr, aoo_id id);

    int32_t make_salt();

    bool need_resampling() const;

    void start_new_stream();

    void update_timer();

    void update_audioqueue();

    void update_resampler();

    void update_historybuffer();

    bool send_format();

    bool send_data();

    bool resend_data();

    bool send_ping();

    void handle_format_request(const osc::ReceivedMessage& msg,
                               const ip_address& addr);

    void handle_data_request(const osc::ReceivedMessage& msg,
                             const ip_address& addr);

    void handle_ping(const osc::ReceivedMessage& msg,
                     const ip_address& addr);

    void handle_invite(const osc::ReceivedMessage& msg,
                       const ip_address& addr);

    void handle_uninvite(const osc::ReceivedMessage& msg,
                         const ip_address& addr);
};

} // aoo
