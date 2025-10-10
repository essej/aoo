/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoo_sink.hpp"

#include <vector>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

// for hardware buffer sizes up to 1024 @ 44.1 kHz
#define DEFAULT_LATENCY 25

// "fake" stream message types
const int kAooDataStreamTime = -3; // AooEventStreamTime
const int kAooDataStreamState = -2; // AooEventStreamState
// NB: kAooDataUnspecified = -1

/*///////////////////// aoo_receive~ ////////////////////*/

t_class *aoo_receive_class;

struct t_source
{
    aoo::ip_address s_address;
    AooId s_id;
    t_symbol *s_group;
    t_symbol *s_user;
};

struct t_stream_message
{
    t_stream_message(const AooStreamMessage& msg, const AooEndpoint& ep)
        : address((const sockaddr *)ep.address, ep.addrlen), id(ep.id),
          channel(msg.channel), type(msg.type), data(msg.data, msg.data + msg.size) {}
    aoo::ip_address address;
    AooId id;
    int32_t channel;
    AooDataType type;
    std::vector<AooByte> data;
};

struct t_aoo_receive
{
    t_aoo_receive(int argc, t_atom *argv);
    ~t_aoo_receive();

    t_object x_obj;

    t_float x_f = 0;
    AooSink::Ptr x_sink;
    int32_t x_samplerate = 0;
    int32_t x_blocksize = 0;
    int32_t x_nchannels = 0;
    int32_t x_port = 0;
    AooId x_id = 0;
    bool x_multi = false;
    std::unique_ptr<t_sample *[]> x_vec;
#if PD_FLOATSIZE != AOO_SAMPLE_SIZE
    std::vector<AooSample> x_buffer;
#endif
    // sources
    std::vector<t_source> x_sources;
    // node
    t_node *x_node = nullptr;
    // events
    t_outlet *x_msgout = nullptr;
    t_clock *x_clock = nullptr;
    t_clock *x_queue_clock = nullptr;

    t_priority_queue<t_stream_message> x_queue;

    bool get_source_arg(int argc, t_atom *argv,
                        aoo::ip_address& addr, AooId& id, bool check) const;

    bool check(const char *name) const;

    bool check(int argc, t_atom *argv, int minargs, const char *name) const;

    void dispatch_stream_message(const AooStreamMessage& msg, const aoo::ip_address& address, AooId id);
};

bool t_aoo_receive::get_source_arg(int argc, t_atom *argv,
                                   aoo::ip_address& addr, AooId& id, bool check) const
{
    if (!x_node) {
        pd_error(this, "%s: no socket!", classname(this));
        return false;
    }
    if (argc < 3){
        pd_error(this, "%s: too few arguments for source", classname(this));
        return false;
    }
    // first try peer (group|user)
    if (argv[1].a_type == A_SYMBOL) {
        t_symbol *group = atom_getsymbol(argv);
        t_symbol *user = atom_getsymbol(argv + 1);
        id = atom_getfloat(argv + 2);
        // first search source list, in case the client has been disconnected
        for (auto& s : x_sources) {
            if (s.s_group == group && s.s_user == user && s.s_id == id) {
                addr = s.s_address;
                return true;
            }
        }
        if (!check) {
            // not yet in list -> try to get from client
            if (x_node->find_peer(group, user, addr)) {
                return true;
            }
        }
        pd_error(this, "%s: couldn't find source %s|%s %d",
                 classname(this), group->s_name, user->s_name, id);
        return false;
    } else {
        // otherwise try host|port
        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        id = atom_getfloat(argv + 2);
        if (x_node->resolve(host, port, addr)) {
            if (check) {
                // try to find in list
                for (auto& s : x_sources) {
                    if (s.s_address == addr && s.s_id == id) {
                        return true;
                    }
                }
                pd_error(this, "%s: couldn't find source %s %d %d",
                         classname(this), host->s_name, port, id);
                return false;
            } else {
                return true;
            }
        } else {
            pd_error(this, "%s: couldn't resolve source hostname '%s'",
                     classname(this), host->s_name);
            return false;
        }
    }
}

bool t_aoo_receive::check(const char *name) const
{
    if (x_node){
        return true;
    } else {
        pd_error(this, "%s: '%s' failed: no socket!", classname(this), name);
        return false;
    }
}

bool t_aoo_receive::check(int argc, t_atom *argv, int minargs, const char *name) const
{
    if (!check(name)) return false;

    if (argc < minargs){
        pd_error(this, "%s: too few arguments for '%s' message", classname(this), name);
        return false;
    }

    return true;
}


static void aoo_receive_invite(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check(argc, argv, 3, "invite")) return;

    aoo::ip_address addr;
    AooId id = 0;
    if (x->get_source_arg(argc, argv, addr, id, false)) {
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };

        if (argc > 3) {
            // with metadata
            AooData metadata;
            if (!atom_to_datatype(argv[3], metadata.type, x)) {
                return;
            }
            argc -= 4; argv += 4;
            if (!argc) {
                pd_error(x, "%s: metadata must not be empty", classname(x));
                return;
            }
            auto size = argc * datatype_element_size(metadata.type);
            auto data = (AooByte *)alloca(size);
            atoms_to_data(metadata.type, argc, argv, data, size);
            metadata.size = size;
            metadata.data = data;

            x->x_sink->inviteSource(ep, &metadata);
        } else {
            x->x_sink->inviteSource(ep, nullptr);
        }
        // notify send thread
        x->x_node->notify();
    }
}

static void aoo_receive_uninvite(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check("uninvite")) return;

    if (!argc){
        x->x_sink->uninviteAll();
        return;
    }

    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'uninvite' message", classname(x));
        return;
    }

    aoo::ip_address addr;
    AooId id = 0;
    if (x->get_source_arg(argc, argv, addr, id, true)){
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        x->x_sink->uninviteSource(ep);
        // notify send thread
        x->x_node->notify();
    }
}

static void aoo_receive_latency(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setLatency(f * 0.001);
}

static void aoo_receive_buffersize(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setBufferSize(f * 0.001);
}

// <ip> <port> <id> <codec> <option> [<value>]
static void aoo_receive_codec_set(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv){
    if (!x->check(argc, argv, 6, "codec_set")) return;
#if 0
    aoo::ip_address addr;
    AooId id = 0;
    if (!x->get_source_arg(argc, argv, addr, id, true)) {
        return;
    }
    AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
#endif
    auto codec = atom_getsymbol(argv + 3);
    auto opt = atom_getsymbol(argv + 4);
    // no codec options yet
    pd_error(x,"%s: unknown parameter '%s' for codec '%s'",
             classname(x), opt->s_name, codec->s_name);
}

// <ip> <port> <id> <codec> <option>
// replies with <ip> <port> <id> <codec> <option> <value>
static void aoo_receive_codec_get(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv) {
    if (!x->check(argc, argv, 5, "codec_get")) return;
#if 0
    aoo::ip_address addr;
    AooId id = 0;
    if (!x->get_source_arg(argc, argv, addr, id, true)) {
        return;
    }
    AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
#endif
    auto codec = atom_getsymbol(argv + 3);
    auto opt = atom_getsymbol(argv + 4);
#if 0
    t_atom msg[6];
    std::copy(argv, argv + 5, msg);
#endif
    // no codec options yet
    pd_error(x, "%s: unknown parameter '%s' for codec '%s'",
             classname(x), opt->s_name, codec->s_name);
    return;
}

static void aoo_receive_resample_method(t_aoo_receive *x, t_symbol *s)
{
    AooResampleMethod method;
    std::string_view name = s->s_name;
    if (name == "hold") {
        method = kAooResampleHold;
    } else if (name == "linear") {
        method = kAooResampleLinear;
    } else if (name == "cubic") {
        method = kAooResampleCubic;
    } else {
        pd_error(x, "%s: bad resample method '%s'",
                 classname(x), name.data());
        return;
    }
    if (x->x_sink->setResampleMethod(method) != kAooOk) {
        pd_error(x, "%s: resample method '%s' not supported",
                 classname(x), name.data());
    }
}

static void aoo_receive_dynamic_resampling(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setDynamicResampling(f);
}

static void aoo_receive_dll_bandwidth(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setDllBandwidth(f);
}

static void aoo_receive_real_samplerate(t_aoo_receive *x)
{
    AooSampleRate sr;
    x->x_sink->getRealSampleRate(sr);
    t_atom msg;
    SETFLOAT(&msg, sr);
    outlet_anything(x->x_msgout, gensym("real_samplerate"), 1, &msg);
}

static void aoo_receive_packetsize(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setPacketSize(f);
}

static void aoo_receive_ping(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setPingInterval(f * 0.001);
}

static void aoo_receive_reset(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc){
        // reset specific source
        aoo::ip_address addr;
        AooId id = 0;
        if (x->get_source_arg(argc, argv, addr, id, true)) {
            AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
            x->x_sink->resetSource(ep);
        }
    } else {
        // reset all sources
        x->x_sink->reset();
    }
}

static void aoo_receive_fill_ratio(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv){
    aoo::ip_address addr;
    AooId id = 0;
    if (argc > 0) {
        if (!x->get_source_arg(argc, argv, addr, id, true)) {
            return;
        }
    } else {
        // just get the first source (if not empty)
        if (!x->x_sources.empty()) {
            auto& src = x->x_sources.front();
            addr = src.s_address;
            id = src.s_id;
        } else {
            pd_error(x, "%s: no sources", classname(x));
        }
    }

    double ratio = 0;
    AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
    x->x_sink->getBufferFillRatio(ep, ratio);

    t_atom msg[4];
    if (x->x_node->serialize_endpoint(addr, id, 3, msg)){
        SETFLOAT(msg + 3, ratio);
        outlet_anything(x->x_msgout, gensym("fill_ratio"), 4, msg);
    }
}

static void aoo_receive_resend(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setResendData(f != 0);
}

static void aoo_receive_resend_limit(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setResendLimit(f);
}

static void aoo_receive_resend_interval(t_aoo_receive *x, t_floatarg f)
{
    x->x_sink->setResendInterval(f * 0.001);
}

static void aoo_receive_source_list(t_aoo_receive *x)
{
    if (!x->check("source_list")) return;

    for (auto& src : x->x_sources)
    {
        t_atom msg[3];
        if (x->x_node->serialize_endpoint(src.s_address, src.s_id, 3, msg)) {
            outlet_anything(x->x_msgout, gensym("source"), 3, msg);
        } else {
            bug("aoo_receive_source_list: serialize_endpoint");
        }
    }
}

static void aoo_receive_handle_stream_message(t_aoo_receive *x, const AooStreamMessage *msg, const AooEndpoint *ep);

static void aoo_receive_handle_event(t_aoo_receive *x, const AooEvent *event, int32_t)
{
    switch (event->type){
    case kAooEventSourceAdd:
    case kAooEventSourceRemove:
    case kAooEventInviteDecline:
    case kAooEventInviteTimeout:
    case kAooEventUninviteTimeout:
    case kAooEventBufferOverrun:
    case kAooEventBufferUnderrun:
    case kAooEventFormatChange:
    case kAooEventStreamStart:
    case kAooEventStreamStop:
    case kAooEventStreamState:
    case kAooEventStreamLatency:
    case kAooEventStreamTime:
    case kAooEventBlockDrop:
    case kAooEventBlockResend:
    case kAooEventBlockXRun:
    case kAooEventSourcePing:
    {
        // common endpoint header
        auto& ep = event->endpoint.endpoint;
        aoo::ip_address addr((const sockaddr *)ep.address, ep.addrlen);
        const int maxsize = 32;
        t_atom msg[maxsize];
        if (!x->x_node->serialize_endpoint(addr, ep.id, 3, msg)) {
            bug("aoo_receive_handle_event: serialize_endpoint");
            return;
        }
        // event data
        switch (event->type){
        case kAooEventSourceAdd:
        {
            // first add to source list; try to find peer name!
            t_symbol *group = nullptr;
            t_symbol *user = nullptr;
            x->x_node->find_peer(addr, group, user);
            x->x_sources.push_back({ addr, ep.id, group, user });

            outlet_anything(x->x_msgout, gensym("add"), 3, msg);
            break;
        }
        case kAooEventSourceRemove:
        {
            // first remove from source list
            auto& sources = x->x_sources;
            for (auto it = sources.begin(); it != sources.end(); ++it){
                if ((it->s_address == addr) && (it->s_id == ep.id)){
                    x->x_sources.erase(it);
                    break;
                }
            }

            outlet_anything(x->x_msgout, gensym("remove"), 3, msg);
            break;
        }
        case kAooEventInviteDecline:
        {
            outlet_anything(x->x_msgout, gensym("invite_decline"), 3, msg);
            break;
        }
        case kAooEventInviteTimeout:
        {
            outlet_anything(x->x_msgout, gensym("invite_timeout"), 3, msg);
            break;
        }
        case kAooEventUninviteTimeout:
        {
            outlet_anything(x->x_msgout, gensym("uninvite_timeout"), 3, msg);
            break;
        }
        case kAooEventSourcePing:
        {
            auto& e = event->sourcePing;

            double delta1 = aoo_ntpTimeDuration(e.t1, e.t2) * 1000.0;
            double delta2 = aoo_ntpTimeDuration(e.t3, e.t4) * 1000.0;
            double total_rtt = aoo_ntpTimeDuration(e.t1, e.t4) * 1000.0;
            double network_rtt = total_rtt - aoo_ntpTimeDuration(e.t2, e.t3) * 1000;

            SETFLOAT(msg + 3, delta1);
            SETFLOAT(msg + 4, delta2);
            SETFLOAT(msg + 5, network_rtt);
            SETFLOAT(msg + 6, total_rtt);

            outlet_anything(x->x_msgout, gensym("ping"), 7, msg);

            break;
        }
        case kAooEventBufferOverrun:
        {
            outlet_anything(x->x_msgout, gensym("overrun"), 3, msg);
            break;
        }
        case kAooEventBufferUnderrun:
        {
            outlet_anything(x->x_msgout, gensym("underrun"), 3, msg);
            break;
        }
        case kAooEventFormatChange:
        {
            // skip first 3 atoms
            int n = format_to_atoms(*event->formatChange.format, maxsize - 3, msg + 3);
            outlet_anything(x->x_msgout, gensym("format"), n + 3, msg);
            break;
        }
        case kAooEventStreamStart:
        {
            auto& e = event->streamStart;
            if (e.metadata){
                // <ip> <port> <id> <type> <data...>
                auto count = 4 + (e.metadata->size / datatype_element_size(e.metadata->type));
                auto vec = (t_atom *)alloca(count * sizeof(t_atom));
                // copy endpoint
                memcpy(vec, msg, 3 * sizeof(t_atom));
                // copy data
                data_to_atoms(*e.metadata, count - 3, vec + 3);

                outlet_anything(x->x_msgout, gensym("start"), count, vec);
            } else {
                outlet_anything(x->x_msgout, gensym("start"), 3, msg);
            }
            break;
        }
        case kAooEventStreamStop:
        {
            outlet_anything(x->x_msgout, gensym("stop"), 3, msg);
            break;
        }
        case kAooEventStreamState:
        {
            auto state = event->streamState.state;
            auto offset = event->streamState.sampleOffset;
            if (offset > 0) {
                // HACK: schedule as fake stream message
                AooStreamMessage msg;
                msg.type = kAooDataStreamState;
                msg.sampleOffset = offset;
                msg.size = sizeof(state);
                msg.data = (AooByte *)&state;
                aoo_receive_handle_stream_message(x, &msg, &ep);
            } else {
                SETFLOAT(msg + 3, state);
                outlet_anything(x->x_msgout, gensym("state"), 4, msg);
            }
            break;
        }
        case kAooEventStreamLatency:
        {
            SETFLOAT(msg + 3, event->streamLatency.sourceLatency * 1000);
            SETFLOAT(msg + 4, event->streamLatency.sinkLatency * 1000);
            SETFLOAT(msg + 5, event->streamLatency.bufferLatency * 1000);
            outlet_anything(x->x_msgout, gensym("latency"), 6, msg);
            break;
        }
        case kAooEventStreamTime:
        {
            AooNtpTime tt[2];
            tt[0] = event->streamTime.sourceTime;
            tt[1] = event->streamTime.sinkTime;
            auto offset = event->streamTime.sampleOffset;
            if (offset > 0) {
                // HACK: schedule as fake stream message
                AooStreamMessage msg;
                msg.type = kAooDataStreamTime;
                msg.sampleOffset = offset;
                msg.size = sizeof(tt);
                msg.data = (AooByte *)tt;
                aoo_receive_handle_stream_message(x, &msg, &ep);
            } else {
                SETFLOAT(msg + 3, get_elapsed_ms(tt[0]));
                SETFLOAT(msg + 4, get_elapsed_ms(tt[1]));
                outlet_anything(x->x_msgout, gensym("time"), 5, msg);
            }
            break;
        }
        case kAooEventBlockDrop:
        {
            SETFLOAT(msg + 3, event->blockDrop.count);
            outlet_anything(x->x_msgout, gensym("block_dropped"), 4, msg);
            break;
        }
        case kAooEventBlockResend:
        {
            SETFLOAT(msg + 3, event->blockResend.count);
            outlet_anything(x->x_msgout, gensym("block_resent"), 4, msg);
            break;
        }
        case kAooEventBlockXRun:
        {
            SETFLOAT(msg + 3, event->blockXRun.count);
            outlet_anything(x->x_msgout, gensym("block_xrun"), 4, msg);
            break;
        }
        default:
            bug("aoo_receive_handle_event: bad case label!");
            break;
        }

        break; // !
    }
    default:
        logpost(x, PD_VERBOSE, "%s: unknown event type (%d)",
                classname(x), event->type);
        break;
    }
}

static void aoo_receive_tick(t_aoo_receive *x)
{
    x->x_sink->pollEvents();
}

static void aoo_receive_queue_tick(t_aoo_receive *x)
{
    auto& queue = x->x_queue;
    auto now = clock_getlogicaltime();

    while (!queue.empty()){
        if (queue.top().time <= now) {
            auto& m = queue.top().data;
            AooStreamMessage msg { 0, m.channel, m.type,
                                 (int32_t)m.data.size(), m.data.data() };
            x->dispatch_stream_message(msg, m.address, m.id);
            queue.pop();
        } else {
            break;
        }
    }
    // reschedule
    if (!queue.empty()){
        clock_set(x->x_queue_clock, queue.top().time);
    }
}

void t_aoo_receive::dispatch_stream_message(const AooStreamMessage& msg,
                                            const aoo::ip_address& address, AooId id) {
    // 5 extra atoms for endpoint (host, port, ID) + message (channel, type)
    // NB: in case of "fake" stream messages, we just over-allocate.
    auto size = 5 + (msg.size / datatype_element_size(msg.type));
    auto vec = (t_atom *)alloca(sizeof(t_atom) * size);
    if (!x_node->serialize_endpoint(address, id, 3, vec)) {
        bug("dispatch_stream_message: serialize_endpoint");
        return;
    }
    if (msg.type == kAooDataStreamState) {
        AooStreamState state;
        assert(msg.size == sizeof(state)); // see aoo_receive_handle_event()
        memcpy(&state, msg.data, sizeof(state));
        SETFLOAT(vec + 3, state);

        outlet_anything(x_msgout, gensym("state"), 4, vec);
    } else if (msg.type == kAooDataStreamTime) {
        AooNtpTime tt[2];
        assert(msg.size == sizeof(tt)); // see aoo_receive_handle_event()
        memcpy(tt, msg.data, sizeof(tt));
        SETFLOAT(vec + 3, get_elapsed_ms(tt[0]));
        SETFLOAT(vec + 4, get_elapsed_ms(tt[1]));

        outlet_anything(x_msgout, gensym("time"), 5, vec);
    } else {
        // message
        stream_message_to_atoms(msg, size - 3, vec + 3);

        outlet_anything(x_msgout, gensym("msg"), size, vec);
    }
}

static void aoo_receive_handle_stream_message(t_aoo_receive *x, const AooStreamMessage *msg, const AooEndpoint *ep)
{
    auto delay = (double)msg->sampleOffset / (double)x->x_samplerate * 1000.0;
    if (delay > 0) {
        // put on queue and schedule on clock (using logical time)
        auto abstime = clock_getsystimeafter(delay);
        // reschedule if we are the next due element
        if (x->x_queue.empty() || abstime < x->x_queue.top().time) {
            clock_set(x->x_queue_clock, abstime);
        }
        x->x_queue.emplace(t_stream_message(*msg, *ep), abstime);
    } else {
        // dispatch immediately
        aoo::ip_address addr((const sockaddr *)ep->address, ep->addrlen);
        x->dispatch_stream_message(*msg, addr, ep->id);
    }

}

static t_int * aoo_receive_perform(t_int *w)
{
    t_aoo_receive *x = (t_aoo_receive *)(w[1]);
    int n = (int)(w[2]);

    if (x->x_node) {
#if PD_FLOATSIZE != AOO_SAMPLE_SIZE
        // write sink output to buffer
        AooSample** buf = nullptr;
        auto nchannels = x->x_nchannels;
        if (nchannels > 0) {
            buf = (AooSample**)alloca(nchannels * sizeof(AooSample*));
            for (int i = 0; i < nchannels; ++i) {
                buf[i] = &x->x_buffer[i * n];
            }
        }
#else
        auto buf = x->x_vec.get();
#endif
        auto err = x->x_sink->process(buf, n, get_osctime(),
                                      (AooStreamMessageHandler)aoo_receive_handle_stream_message, x);
#if PD_FLOATSIZE != AOO_SAMPLE_SIZE
        // copy buffer to signal outlets
        for (int i = 0; i < nchannels; ++i) {
            auto src = buf[i];
            auto dst = x->x_vec[i];
            for (int k = 0; k < n; ++k) {
                dst[k] = src[k];
            }
        }
#endif
        if (err != kAooErrorIdle) {
            x->x_node->notify();
        }

        // handle events
        if (x->x_sink->eventsAvailable()) {
            clock_delay(x->x_clock, 0);
        }
    } else {
        // zero outputs
        for (int i = 0; i < x->x_nchannels; ++i) {
            std::fill(x->x_vec[i], x->x_vec[i] + n, 0);
        }
    }

    return w + 3;
}

static void aoo_receive_channels(t_aoo_receive *x, t_floatarg f) {
    if (x->x_multi) {
        int nchannels = std::max<int>(f, 1);
        if (nchannels != x->x_nchannels) {
            x->x_nchannels = nchannels;
            x->x_vec = nullptr; // sentinel, see "dsp" method
            canvas_update_dsp();
        }
    } else {
        pd_error(x, "%s: 'channels' message requires multi-channel mode", classname(x));
    }
}

static void aoo_receive_dsp(t_aoo_receive *x, t_signal **sp)
{
    int32_t blocksize = sp[0]->s_n;
    int32_t samplerate = sp[0]->s_sr;
    int32_t nchannels = x->x_nchannels;

    // NB: aoo_receive~ is a multi-channel class, so we need to
    // call signal_setmultiout - even in single-channel mode!
    if (g_signal_setmultiout) {
        if (x->x_multi) {
            g_signal_setmultiout(sp, nchannels);
        } else {
            for (int i = 0; i < nchannels; ++i) {
                g_signal_setmultiout(&sp[i], 1);
            }
        }
    }

    bool channels_changed = false;
    if (x->x_multi) {
        // channel count changed?
        if (!x->x_vec) {
            x->x_vec = std::make_unique<t_sample *[]>(nchannels);
            channels_changed = true;
        }
        for (int i = 0; i < nchannels; ++i) {
            x->x_vec[i] = &sp[0]->s_vec[i * blocksize];
        }
    } else {
        for (int i = 0; i < nchannels; ++i) {
            x->x_vec[i] = sp[i]->s_vec;
        }
    }
#if PD_FLOATSIZE != AOO_SAMPLE_SIZE
    x->x_buffer.resize(nchannels * blocksize);
#endif

    if (blocksize != x->x_blocksize || samplerate != x->x_samplerate
            || channels_changed) {
        x->x_sink->setup(nchannels, samplerate, blocksize, kAooFixedBlockSize);
        x->x_blocksize = blocksize;
        x->x_samplerate = samplerate;
    }

    dsp_add(aoo_receive_perform, 2, (t_int)x, (t_int)x->x_blocksize);
}

static void aoo_receive_set(t_aoo_receive *x, t_floatarg f1, t_floatarg f2)
{
    int port = f1;
    AooId id = f2;

    if (id == x->x_id && port == x->x_port) {
        return;
    }

    if (id < 0) {
        pd_error(x, "%s: bad id %d", classname(x), id);
        return;
    }

    if (port < 0) {
        // NB: 0 is allowed (= don't listen)!
        pd_error(x, "%s: bad port %d", classname(x), id);
        return;
    }

    // always release node!
    if (x->x_node) {
        x->x_node->release((t_pd *)x, x->x_sink.get());
    }

    if (id != x->x_id) {
        x->x_sink->setId(id);
        x->x_id = id;
    }

    if (port) {
        x->x_node = t_node::get((t_pd *)x, port, x->x_sink.get(), id);
    } else {
        x->x_node = nullptr;
    }
    x->x_port = port;
}

static void aoo_receive_port(t_aoo_receive *x, t_floatarg f)
{
    aoo_receive_set(x, f, x->x_id);
}

static void aoo_receive_id(t_aoo_receive *x, t_floatarg f)
{
    aoo_receive_set(x, x->x_port, f);
}

static void * aoo_receive_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_receive_class);
    new (x) t_aoo_receive(argc, argv);
    return x;
}

t_aoo_receive::t_aoo_receive(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_receive_tick);
    x_queue_clock = clock_new(this, (t_method)aoo_receive_queue_tick);

    // flags
    while (argc && argv->a_type == A_SYMBOL) {
        auto flag = argv->a_w.w_symbol->s_name;
        if (*flag == '-') {
            if (!strcmp(flag, "-m")) {
                if (g_signal_setmultiout) {
                    x_multi = true;
                } else {
                    pd_error(this, "%s: no multi-channel support, ignoring '-m' flag", classname(this));
                }
            } else {
                pd_error(this, "%s: ignore unknown flag '%s",
                         classname(this), flag);
            }
            argc--; argv++;
        } else {
            break;
        }
    }

    // arg #1: channels
    int noutlets;
    if (x_multi) {
        noutlets = 1;
        // NB: the channel count cannot be zero!
        x_nchannels = std::max<int>(atom_getfloatarg(0, argc, argv), 1);
    } else {
        // NB: users may explicitly specify 0 channels for pure message streams!
        x_nchannels = argc > 0 ? atom_getfloat(argv) : 1;
        if (x_nchannels < 0) {
            x_nchannels = 0;
        } else if (x_nchannels > AOO_MAX_NUM_CHANNELS) {
            // see comment above AOO_MAX_NUM_CHANNELS
            pd_error(this, "%s: channel count (%d) out of range",
                     classname(this), x_nchannels);
            x_nchannels = 0;
        }
        // however, we need at least one signal outlet for the "dsp" method...
        noutlets = std::max<int>(x_nchannels, 1);
    }

    // arg #2 (optional): port number
    // NB: 0 means "don't listen"
    int port = atom_getfloatarg(1, argc, argv);

    // arg #3 (optional): ID
    AooId id = atom_getfloatarg(2, argc, argv);
    if (id < 0) {
        pd_error(this, "%s: bad id % d, setting to 0", classname(this), id);
        id = 0;
    }
    x_id = id;

    // arg #4 (optional): latency (ms)
    float latency = argc > 3 ? atom_getfloat(argv + 3) : DEFAULT_LATENCY;

    // make signal outlets
    for (int i = 0; i < noutlets; ++i){
        outlet_new(&x_obj, &s_signal);
    }
    // channel vector
    if (x_nchannels > 0) {
        x_vec = std::make_unique<t_sample *[]>(x_nchannels);
    }
    // event outlet
    x_msgout = outlet_new(&x_obj, 0);

    // create and initialize AooSink object
    x_sink = AooSink::create(x_id);

    // set event handler
    x_sink->setEventHandler((AooEventHandler)aoo_receive_handle_event,
                             this, kAooEventModePoll);

    x_sink->setLatency(latency * 0.001);

    // finally we're ready to receive messages
    aoo_receive_port(this, port);
}

static void aoo_receive_free(t_aoo_receive *x)
{
    x->~t_aoo_receive();
}

t_aoo_receive::~t_aoo_receive()
{
    if (x_node){
        x_node->release((t_pd *)this, x_sink.get());
    }

    clock_free(x_clock);
    clock_free(x_queue_clock);
}

void aoo_receive_tilde_setup(void)
{
    aoo_receive_class = class_new(gensym("aoo_receive~"), (t_newmethod)(void *)aoo_receive_new,
        (t_method)aoo_receive_free, sizeof(t_aoo_receive), CLASS_MULTICHANNEL, A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_dsp,
                    gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_port,
                    gensym("port"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_id,
                    gensym("id"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_set,
                    gensym("set"), A_DEFFLOAT, A_DEFFLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_invite,
                    gensym("invite"), A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_uninvite,
                    gensym("uninvite"), A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_latency,
                    gensym("latency"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_buffersize,
                    gensym("buffersize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_codec_set,
                    gensym("codec_set"), A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_codec_get,
                    gensym("codec_get"), A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_resample_method,
                    gensym("resample_method"), A_SYMBOL, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_dynamic_resampling,
                    gensym("dynamic_resampling"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_dll_bandwidth,
                    gensym("dll_bandwidth"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_real_samplerate,
                    gensym("real_samplerate"), A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_packetsize,
                    gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_ping,
                    gensym("ping"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_resend,
                    gensym("resend"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_resend_limit,
                    gensym("resend_limit"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_resend_interval,
                    gensym("resend_interval"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_source_list,
                    gensym("source_list"), A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_reset,
                    gensym("reset"), A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_fill_ratio,
                    gensym("fill_ratio"), A_GIMME, A_NULL);
    if (g_signal_setmultiout) {
        class_addmethod(aoo_receive_class, (t_method)aoo_receive_channels,
            gensym("channels"), A_FLOAT, A_NULL);
    }
}
