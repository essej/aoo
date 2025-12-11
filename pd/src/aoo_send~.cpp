/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoo_source.hpp"

#if AOO_USE_OPUS
# include "codec/aoo_opus.h"
#endif

#include <vector>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

#ifdef _WIN32
# include <malloc.h> // MSVC or mingw on windows
# ifdef _MSC_VER
#  define alloca _alloca
# endif
#elif defined(__linux__) || defined(__APPLE__)
# include <alloca.h> // linux, mac, mingw, cygwin
#else
# include <stdlib.h> // BSDs for example
#endif

// for hardware buffer sizes up to 1024 @ 44.1 kHz
#define DEFBUFSIZE 25

t_class *aoo_send_class;

struct t_sink
{
    aoo::ip_address s_address;
    AooId s_id;
    t_symbol *s_group;
    t_symbol *s_user;
};

struct t_aoo_send
{
    t_aoo_send(int argc, t_atom *argv);
    ~t_aoo_send();

    t_object x_obj;

    t_float x_f = 0;
    AooSource::Ptr x_source;
    std::vector<t_atom> x_format;
    int32_t x_samplerate = 0;
    int32_t x_blocksize = 0;
    int32_t x_nchannels = 0;
    int32_t x_port = 0;
    AooId x_id = 0;
    double x_logicaltime = 0;
    std::unique_ptr<t_sample *[]> x_vec;
#if PD_FLOATSIZE != AOO_SAMPLE_SIZE
    std::vector<AooSample> x_buffer;
#endif
    // sinks
    std::vector<t_sink> x_sinks;
    // node
    t_node *x_node = nullptr;
    // events
    t_clock *x_clock = nullptr;
    t_outlet *x_msgout = nullptr;
    // (un)invite
    AooId x_invite_token = kAooIdInvalid;
    bool x_running = false;
    bool x_auto_invite = true; // on by default
    bool x_multi = false;

    void update_format();

    bool get_sink_arg(int argc, t_atom *argv,
                      aoo::ip_address& addr, AooId& id, bool check) const;

    bool check(const char *name) const;

    bool check(int argc, t_atom *argv, int minargs, const char *name) const;

    void add_sink(const aoo::ip_address& addr, AooId id);

    void remove_all();

    void remove_sink(const aoo::ip_address& addr, AooId id);

    const t_sink *find_sink(const aoo::ip_address& addr, AooId id) const;
};

void t_aoo_send::update_format()
{
    AooFormatStorage fmt;
    format_parse((t_pd*)this, fmt, x_format.size(), x_format.data(),
                 x_nchannels, x_samplerate, x_blocksize);
    x_source->setFormat(fmt.header);
}

bool t_aoo_send::get_sink_arg(int argc, t_atom *argv,
                              aoo::ip_address& addr, AooId& id, bool check) const
{
    if (!x_node) {
        pd_error(this, "%s: no socket!", classname(this));
        return false;
    }
    if (argc < 3){
        pd_error(this, "%s: too few arguments for sink", classname(this));
        return false;
    }
    // first try peer (group|user)
    if (argv[1].a_type == A_SYMBOL) {
        t_symbol *group = atom_getsymbol(argv);
        t_symbol *user = atom_getsymbol(argv + 1);
        id = atom_getfloat(argv + 2);
        // first search sink list, in case the client has been disconnected
        for (auto& s : x_sinks) {
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
        pd_error(this, "%s: couldn't find sink %s|%s %d",
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
                for (auto& s : x_sinks) {
                    if (s.s_address == addr && s.s_id == id) {
                        return true;
                    }
                }
                pd_error(this, "%s: couldn't find sink %s %d %d",
                         classname(this), host->s_name, port, id);
                return false;
            } else {
                return true;
            }
        } else {
            pd_error(this, "%s: couldn't resolve sink hostname '%s'",
                     classname(this), host->s_name);
            return false;
        }
    }
}

bool t_aoo_send::check(const char *name) const
{
    if (x_node){
        return true;
    } else {
        pd_error(this, "%s: '%s' failed: no socket!", classname(this), name);
        return false;
    }
}

bool t_aoo_send::check(int argc, t_atom *argv, int minargs, const char *name) const
{
    if (!check(name)) return false;

    if (argc < minargs){
        pd_error(this, "%s: too few arguments for '%s' message", classname(this), name);
        return false;
    }

    return true;
}

void t_aoo_send::add_sink(const aoo::ip_address& addr, AooId id)
{
    // add sink to list; try to find peer name!
    t_symbol *group = nullptr;
    t_symbol *user = nullptr;
    x_node->find_peer(addr, group, user);
    x_sinks.push_back({addr, id, group, user});

    // output message
    t_atom msg[3];
    if (x_node->serialize_endpoint(addr, id, 3, msg)){
        outlet_anything(x_msgout, gensym("add"), 3, msg);
    } else {
        bug("t_aoo_send::add_sink: serialize_endpoint");
    }
}

void t_aoo_send::remove_all()
{
    x_source->removeAllSinks();

    int numsinks = x_sinks.size();
    if (!numsinks){
        return;
    }

    // temporary copies (for reentrancy)
    t_sink *sinks = (t_sink *)alloca(sizeof(t_sink) * numsinks);
    std::copy(x_sinks.begin(), x_sinks.end(), sinks);

    x_sinks.clear();

    // output messages
    for (int i = 0; i < numsinks; ++i){
        t_atom msg[3];
        if (x_node->serialize_endpoint(sinks[i].s_address, sinks[i].s_id, 3, msg)){
            outlet_anything(x_msgout, gensym("remove"), 3, msg);
        } else {
            bug("aoo_send_doremoveall: serialize_endpoint");
        }
    }
}

void t_aoo_send::remove_sink(const aoo::ip_address& addr, AooId id)
{
    // remove the sink matching endpoint and id
    for (auto it = x_sinks.begin(); it != x_sinks.end(); ++it){
        if (it->s_address == addr && it->s_id == id){
            x_sinks.erase(it);

            // output message
            t_atom msg[3];
            if (x_node->serialize_endpoint(addr, id, 3, msg)){
                outlet_anything(x_msgout, gensym("remove"), 3, msg);
            } else {
                bug("aoo_send_doremovesink: serialize_endpoint");
            }
            return;
        }
    }
    bug("t_aoo_send::remove_sink");
}

const t_sink *t_aoo_send::find_sink(const aoo::ip_address& addr, AooId id) const
{
    for (auto& sink : x_sinks){
        if (sink.s_address == addr && sink.s_id == id){
            return &sink;
        }
    }
    return nullptr;
}

#if AOO_USE_OPUS

static bool get_opus_bitrate(t_aoo_send *x, t_atom *a) {
    opus_int32 value;
    auto err = AooSource_getOpusBitrate(x->x_source.get(), 0, &value);
    if (err != kAooOk){
        pd_error(x, "%s: could not get bitrate: %s", classname(x), aoo_strerror(err));
        return false;
    }
    // NOTE: because of a bug in opus_multistream_encoder (as of opus v1.3.2)
    // OPUS_GET_BITRATE always returns OPUS_AUTO
    switch (value){
    case OPUS_AUTO:
        SETSYMBOL(a, gensym("auto"));
        break;
    case OPUS_BITRATE_MAX:
        SETSYMBOL(a, gensym("max"));
        break;
    default:
        SETFLOAT(a, value * 0.001); // bit -> kBit
        break;
    }
    return true;
}

static void set_opus_bitrate(t_aoo_send *x, const t_atom *a) {
    // "auto", "max" or number
    opus_int32 value;
    if (a->a_type == A_SYMBOL){
        t_symbol *sym = a->a_w.w_symbol;
        if (sym == gensym("auto")){
            value = OPUS_AUTO;
        } else if (sym == gensym("max")){
            value = OPUS_BITRATE_MAX;
        } else {
            pd_error(x, "%s: bad bitrate argument '%s'",
                     classname(x), sym->s_name);
            return;
        }
    } else {
        opus_int32 bitrate = atom_getfloat(a) * 1000.0; // kBit -> bit
        if (bitrate > 0){
            value = bitrate;
        } else {
            pd_error(x, "%s: bitrate argument %d out of range",
                     classname(x), bitrate);
            return;
        }
    }
    auto err = AooSource_setOpusBitrate(x->x_source.get(), 0, value);
    if (err != kAooOk){
        pd_error(x, "%s: could not set bitrate: %s",
                 classname(x), aoo_strerror(err));
    }
}

static bool get_opus_complexity(t_aoo_send *x, t_atom *a){
    opus_int32 value;
    auto err = AooSource_getOpusComplexity(x->x_source.get(), 0, &value);
    if (err != kAooOk){
        pd_error(x, "%s: could not get complexity: %s",
                 classname(x), aoo_strerror(err));
        return false;
    }
    SETFLOAT(a, value);
    return true;
}

static void set_opus_complexity(t_aoo_send *x, const t_atom *a){
    // 0-10
    opus_int32 value = atom_getfloat(a);
    if (value < 0 || value > 10){
        pd_error(x, "%s: complexity value %d out of range",
                 classname(x), value);
        return;
    }
    auto err = AooSource_setOpusComplexity(x->x_source.get(), 0, value);
    if (err != kAooOk){
        pd_error(x, "%s: could not set complexity: %s",
                 classname(x), aoo_strerror(err));
    }
}

static bool get_opus_signal(t_aoo_send *x, t_atom *a){
    opus_int32 value;
    auto err = AooSource_getOpusSignalType(x->x_source.get(), 0, &value);
    if (err != kAooOk){
        pd_error(x, "%s: could not get signal type: %s",
                 classname(x), aoo_strerror(err));
        return false;
    }
    t_symbol *type;
    switch (value){
    case OPUS_SIGNAL_MUSIC:
        type = gensym("music");
        break;
    case OPUS_SIGNAL_VOICE:
        type = gensym("voice");
        break;
    default:
        type = gensym("auto");
        break;
    }
    SETSYMBOL(a, type);
    return true;
}

static void set_opus_signal(t_aoo_send *x, const t_atom *a){
    // "auto", "music", "voice"
    opus_int32 value;
    t_symbol *type = atom_getsymbol(a);
    if (type == gensym("auto")){
        value = OPUS_AUTO;
    } else if (type == gensym("music")){
        value = OPUS_SIGNAL_MUSIC;
    } else if (type == gensym("voice")){
        value = OPUS_SIGNAL_VOICE;
    } else {
        pd_error(x,"%s: unsupported signal type '%s'",
                 classname(x), type->s_name);
        return;
    }
    auto err = AooSource_setOpusSignalType(x->x_source.get(), 0, value);
    if (err != kAooOk){
        pd_error(x, "%s: could not set signal type: %s",
                 classname(x), aoo_strerror(err));
    }
}

#endif

static void aoo_send_codec_set(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv){
    if (!x->check(argc, argv, 2, "codec_set")) return;

    auto name = atom_getsymbol(argv);
    t_symbol *codec = atom_getsymbolarg(0, x->x_format.size(), x->x_format.data());
#if AOO_USE_OPUS
    if (codec == gensym("opus")){
        opus_int32 value;
        if (name == gensym("bitrate")){
            set_opus_bitrate(x, argv + 1);
            return;
        } else if (name == gensym("complexity")){
            set_opus_complexity(x, argv + 1);
            return;
        } else if (name == gensym("signal")){
            set_opus_signal(x, argv + 1);
            return;
        }
    }
#endif
    pd_error(x,"%s: unknown parameter '%s' for codec '%s'",
             classname(x), name->s_name, codec->s_name);
}

static void aoo_send_codec_get(t_aoo_send *x, t_symbol *s){
    if (!x->check("codec_get")) return;

    t_atom msg[2];
    SETSYMBOL(msg, s);

    t_symbol *codec = atom_getsymbolarg(0, x->x_format.size(), x->x_format.data());
#if AOO_USE_OPUS
    if (codec == gensym("opus")){
        if (s == gensym("bitrate")){
            if (get_opus_bitrate(x, msg + 1)){
                goto codec_sendit;
            } else {
                return;
            }
        } else if (s == gensym("complexity")){
            if (get_opus_complexity(x, msg + 1)){
                goto codec_sendit;
            } else {
                return;
            }
        } else if (s == gensym("signal")){
            if (get_opus_signal(x, msg + 1)){
                goto codec_sendit;
            } else {
                return;
            }
        }
    }
#endif
    pd_error(x,"%s: unknown parameter '%s' for codec '%s'",
             classname(x), s->s_name, codec->s_name);
    return;

codec_sendit:
    // send message
    outlet_anything(x->x_msgout, gensym("codec_get"), 2, msg);
}

static void aoo_send_handle_event(t_aoo_send *x, const AooEvent *event, int32_t)
{
    switch (event->type){
    case kAooEventSinkPing:
    case kAooEventInvite:
    case kAooEventUninvite:
    case kAooEventSinkAdd:
    case kAooEventSinkRemove:
    case kAooEventFrameResend:
    {
        // common endpoint header
        auto& ep = event->endpoint.endpoint;
        aoo::ip_address addr((const sockaddr *)ep.address, ep.addrlen);
        t_atom msg[12];
        if (!x->x_node->serialize_endpoint(addr, ep.id, 3, msg)) {
            bug("aoo_send_handle_event: serialize_endpoint");
            return;
        }
        // event data
        switch (event->type){
        case kAooEventInvite:
        {
            auto& e = event->invite;

            x->x_invite_token = e.token;
            if (x->x_auto_invite) {
                // accept by default if streaming; decline otherwise
                x->x_source->handleInvite(ep, e.token, x->x_running);
                if (!x->x_running) {
                    return; // don't send "invite" event!
                }
            }
            // Send "invite" event.
            // In manual mode, the user is supposed to handle the invitation;
            // in auto mode, we want to tell the user that the sink has been
            // activated (together with the metadata).
            if (e.metadata) {
                // <ip> <port> <id> <type> <data...>
                auto count = 4 + (e.metadata->size / datatype_element_size(e.metadata->type));
                t_atom *vec = (t_atom *)alloca(count * sizeof(t_atom));
                // copy endpoint
                memcpy(vec, msg, 3 * sizeof(t_atom));
                // copy data
                data_to_atoms(*e.metadata, count - 3, vec + 3);

                outlet_anything(x->x_msgout, gensym("invite"), count, vec);
            } else {
                outlet_anything(x->x_msgout, gensym("invite"), 3, msg);
            }
            break;
        }
        case kAooEventUninvite:
        {
            x->x_invite_token = event->uninvite.token;
            if (x->x_auto_invite) {
                // accept by default
                x->x_source->handleUninvite(ep, event->uninvite.token, true);
            }
            // Send "uninvite" event.
            // In manual mode, the user is supposed to handle the uninvitation;
            // in auto mode, we want to tell the user that the sink has been
            // deactivated.
            outlet_anything(x->x_msgout, gensym("uninvite"), 3, msg);

            break;
        }
        case kAooEventSinkAdd:
        {
            if (!x->find_sink(addr, ep.id)){
                x->add_sink(addr, ep.id);
            } else {
                // the sink might have been added concurrently by the user (very unlikely)
                logpost(x, PD_DEBUG, "%s: sink %s %d %d already added",
                        classname(x), addr.name(), addr.port(), ep.id);
            }
            break;
        }
        case kAooEventSinkRemove:
        {
            if (x->find_sink(addr, ep.id)){
                x->remove_sink(addr, ep.id);
            } else {
                // the sink might have been removed concurrently by the user (very unlikely)
                logpost(x, PD_DEBUG, "%s: sink %s %d %d already removed",
                        classname(x), addr.name(), addr.port(), ep.id);
            }
            break;
        }
        //--------------------- sink events -----------------------//
        case kAooEventSinkPing:
        {
            auto& e = event->sinkPing;

            double delta1 = aoo_ntpTimeDuration(e.t1, e.t2) * 1000.0;
            double delta2 = aoo_ntpTimeDuration(e.t3, e.t4) * 1000.0;
            double total_rtt = aoo_ntpTimeDuration(e.t1, e.t4) * 1000.0;
            double network_rtt = total_rtt - aoo_ntpTimeDuration(e.t2, e.t3) * 1000;

            SETFLOAT(msg + 3, delta1);
            SETFLOAT(msg + 4, delta2);
            SETFLOAT(msg + 5, network_rtt);
            SETFLOAT(msg + 6, total_rtt);
            SETFLOAT(msg + 7, e.packetLoss);

            outlet_anything(x->x_msgout, gensym("ping"), 8, msg);

            break;
        }
        case kAooEventFrameResend:
        {
            SETFLOAT(msg + 3, event->frameResend.count);
            outlet_anything(x->x_msgout, gensym("frame_resent"), 4, msg);
            break;
        }
        default:
            bug("aoo_send_handle_event: bad case label!");
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

static void aoo_send_tick(t_aoo_send *x)
{
    x->x_source->pollEvents();
}

static void aoo_send_format(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    AooFormatStorage f;
    if (format_parse((t_pd *)x, f, argc, argv, x->x_nchannels,
                     x->x_samplerate, x->x_blocksize)) {
        // Prevent user from accidentally creating huge number of channels.
        // This also helps to catch an issue with old patches (before 2.0-pre3),
        // which would pass the block size as the channel count because the
        // "channels" argument hasn't been added yet.
        // NB: don't do this in multi-channel mode because the actual channel
        // count may change after the fact!
        // NB: neither do this for the "null" codec (= pure message streams)
        // where we typically have no signal inlets.
        if (!x->x_multi && (strcmp(f.header.codecName, "null") != 0)) {
            if (f.header.numChannels > x->x_nchannels){
                if (x->x_nchannels > 0) {
                    pd_error(x, "%s: 'channel' argument (%d) in 'format' message out of range!",
                             classname(x), f.header.numChannels);
                    f.header.numChannels = x->x_nchannels;
                } else {
                    // if we have no inputs, silently bash format to single channel
                    f.header.numChannels = 1;
                }
            }
        }

        auto err = x->x_source->setFormat(f.header);
        if (err == kAooOk) {
            // store original format for updates, see "dsp" method
            x->x_format.assign(argv, argv + argc);

            // output actual format
            t_atom msg[16];
            int n = format_to_atoms(f.header, 16, msg);
            if (n > 0){
                outlet_anything(x->x_msgout, gensym("format"), n, msg);
            }
        } else {
            pd_error(x, "%s: could not set format: %s",
                     classname(x), aoo_strerror(err));
        }
    }
}

static void aoo_send_auto_invite(t_aoo_send *x, t_floatarg f) {
    x->x_auto_invite = f != 0;
}

static void aoo_send_invite(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check(argc, argv, 3, "invite")) return;

    aoo::ip_address addr;
    AooId id;
    if (x->get_sink_arg(argc, argv, addr, id, true)) {
        bool accept = argc > 3 ? atom_getfloat(argv + 3) : true; // default: true
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        x->x_source->handleInvite(ep, x->x_invite_token, accept);
    }
}

static void aoo_send_uninvite(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check(argc, argv, 3, "uninvite")) return;

    aoo::ip_address addr;
    AooId id;
    if (x->get_sink_arg(argc, argv, addr, id, true)){
        bool accept = argc > 3 ? atom_getfloat(argv + 3) : true; // default: true
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        x->x_source->handleUninvite(ep, x->x_invite_token, accept);
    }
}

static void aoo_send_sink_channel(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check(argc, argv, 4, "sink_channel")) return;

    aoo::ip_address addr;
    AooId id;
    if (x->get_sink_arg(argc, argv, addr, id, true)){
        int32_t chn = atom_getfloat(argv + 3);
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        x->x_source->setSinkChannelOffset(ep, chn);
    }
}

static void aoo_send_packetsize(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setPacketSize(f);
}

static void aoo_send_ping(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setPingInterval(f * 0.001);
}

static void aoo_send_stream_time(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setStreamTimeSendInterval(f * 0.001);
}

static void aoo_send_resend(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setResendBufferSize(f * 0.001);
}

static void aoo_send_redundancy(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setRedundancy(f);
}

static void aoo_send_resample_method(t_aoo_send *x, t_symbol *s)
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
    if (x->x_source->setResampleMethod(method) != kAooOk) {
        pd_error(x, "%s: resample method '%s' not supported",
                 classname(x), name.data());
    }
}

static void aoo_send_dynamic_resampling(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setDynamicResampling(f);
}

static void aoo_send_dll_bandwidth(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setDllBandwidth(f);
}

static void aoo_send_real_samplerate(t_aoo_send *x)
{
    AooSampleRate sr;
    x->x_source->getRealSampleRate(sr);
    t_atom msg;
    SETFLOAT(&msg, sr);
    outlet_anything(x->x_msgout, gensym("real_samplerate"), 1, &msg);
}

static void aoo_send_binary(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setBinaryFormat(f);
}

static void aoo_send_buffersize(t_aoo_send *x, t_floatarg f)
{
    x->x_source->setBufferSize(f * 0.001);
}

static void aoo_send_add(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check(argc, argv, 3, "add")) return;

    aoo::ip_address addr;
    AooId id;
    if (x->get_sink_arg(argc, argv, addr, id, false)){
        // check if sink exists
        if (x->find_sink(addr, id)) {
            if (argv[1].a_type == A_SYMBOL){
                // group + user
                auto group = atom_getsymbol(argv)->s_name;
                auto user = atom_getsymbol(argv + 1)->s_name;
                pd_error(x, "%s: sink %s|%s %d already added!",
                         classname(x), group, user, id);
            } else {
                // host + port
                auto host = atom_getsymbol(argv)->s_name;
                pd_error(x, "%s: sink %s %d %d already added!",
                         classname(x), host, addr.port(), id);
            }
            return;
        }

        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        bool active = argc > 3 ? atom_getfloat(argv + 3) : true;
        x->x_source->addSink(ep, active);

        // not yet implemented
        if (argc > 4) {
            int channel = atom_getfloat(argv + 4);
            x->x_source->setSinkChannelOffset(ep, channel);
        }

        x->add_sink(addr, id);

        // print message (use actual IP address)
        logpost(x, PD_DEBUG, "%s: added sink %s %d %d",
                classname(x), addr.name(), addr.port(), id);
    }
}

static void aoo_send_remove(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check("remove")) return;

    if (!argc){
        x->remove_all();
        return;
    }

    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'remove' message", classname(x));
        return;
    }

    aoo::ip_address addr;
    AooId id;
    if (x->get_sink_arg(argc, argv, addr, id, true)) {
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        x->x_source->removeSink(ep);

        x->remove_sink(addr, id);

        logpost(x, PD_DEBUG, "%s: removed sink %s %d %d",
                classname(x), addr.name(), addr.port(), id);
    }
}

static void aoo_send_start(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    int32_t offset = clock_gettimesince(x->x_logicaltime) * 0.001 * x->x_samplerate;
    // in case "start" is sent while DSP is off...
    if (offset >= x->x_blocksize) {
        offset = 0;
    }
    if (argc > 0) {
        // with metadata
        AooData metadata;
        if (!atom_to_datatype(*argv, metadata.type, x)) {
            return;
        }
        argc--; argv++;
        if (!argc) {
            pd_error(x, "%s: metadata must not be empty", classname(x));
            return;
        }
        auto size = argc * datatype_element_size(metadata.type);
        auto data = (AooByte *)alloca(size);
        atoms_to_data(metadata.type, argc, argv, data, size);
        metadata.size = size;
        metadata.data = data;

        x->x_source->startStream(offset, &metadata);
    } else {
        x->x_source->startStream(offset, nullptr);
    }
    x->x_running = true;
}

static void aoo_send_stop(t_aoo_send *x)
{
    int32_t offset = clock_gettimesince(x->x_logicaltime) * 0.001 * x->x_samplerate;
    // in case "stop" is sent while DSP is off...
    if (offset >= x->x_blocksize) {
        offset = 0;
    }
    x->x_source->stopStream(offset);
    x->x_running = false;
}

static void aoo_send_reset(t_aoo_send *x)
{
    x->x_source->reset();
}

static void aoo_send_active(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check(argc, argv, 4, "active")) return;

    aoo::ip_address addr;
    AooId id;
    if (x->get_sink_arg(argc, argv, addr, id, true)){
        AooEndpoint ep { addr.address(), (AooAddrSize)addr.length(), id };
        bool active = atom_getfloat(argv + 3);
        x->x_source->activate(ep, active);
    }
}

static void aoo_send_sink_list(t_aoo_send *x)
{
    if (!x->check("sink_list")) return;

    for (auto& sink : x->x_sinks){
        t_atom msg[3];
        if (x->x_node->serialize_endpoint(sink.s_address, sink.s_id, 3, msg)){
            outlet_anything(x->x_msgout, gensym("sink"), 3, msg);
        } else {
            bug("t_node::serialize_endpoint");
        }
    }
}

static void aoo_send_list(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->check("list")) return;

    if (argc < 2) {
        return;
    }

    AooStreamMessage msg;
    auto delta = clock_gettimesince(x->x_logicaltime) * 0.001;
    msg.sampleOffset = delta * x->x_samplerate;
    msg.channel = std::max<int32_t>(atom_getfloat(argv), 0);
    if (!atom_to_datatype(argv[1], msg.type, x)) {
        return;
    }
    argc -= 2; argv += 2;
    if (!argc) {
        pd_error(x, "%s: data must not be empty", classname(x));
        return;
    }
    auto size = argc * datatype_element_size(msg.type);
    auto buf = (AooByte *)alloca(size);
    atoms_to_data(msg.type, argc, argv, buf, size);
    msg.data = buf;
    msg.size = size;

    if (auto err = x->x_source->addStreamMessage(msg); err != kAooOk) {
        pd_error(x, "%s: could not add stream message: %s",
                 classname(x), aoo_strerror(err));
    }
}

static t_int * aoo_send_perform(t_int *w)
{
    t_aoo_send *x = (t_aoo_send *)(w[1]);
    int n = (int)(w[2]);

    if (x->x_node) {
#if PD_FLOATSIZE != AOO_SAMPLE_SIZE
        // buffer input signals and convert to AooSample
        AooSample** buf = nullptr;
        if (auto nchannels = x->x_nchannels; nchannels > 0) {
            buf = (AooSample**)alloca(nchannels * sizeof(AooSample*));
            for (int i = 0; i < nchannels; ++i) {
                auto src = x->x_vec[i];
                auto dst = buf[i] = &x->x_buffer[i * n];
                for (int k = 0; k < n; ++k) {
                    dst[k] = src[k];
                }
            }
        }
#else
        auto buf = x->x_vec.get();
#endif
        auto err = x->x_source->process(buf, n, get_osctime());

        if (err == kAooErrorOverflow) {
            pd_error(x, "%s: send buffer overflow. Try to manually increase "
                     "the send buffer size with the 'buffersize' method or "
                     "lower your hardware buffer size.", classname(x));
        }

        if (err != kAooErrorIdle) {
            x->x_node->notify();
        }

        if (x->x_source->eventsAvailable()) {
            clock_delay(x->x_clock, 0);
        }
    }

    x->x_logicaltime = clock_getlogicaltime();

    return w + 3;
}

static void aoo_send_dsp(t_aoo_send *x, t_signal **sp)
{
    int32_t blocksize = sp[0]->s_n;
    int32_t samplerate = sp[0]->s_sr;
    int32_t nchannels;
#ifdef PD_HAVE_MULTICHANNEL
    if (x->x_multi) {
        // NB: 's_nchans' only exists in Pd 0.54+
        nchannels = sp[0]->s_nchans;
        if (nchannels != x->x_nchannels) {
            x->x_vec = std::make_unique<t_sample *[]>(nchannels);
        }
        for (int i = 0; i < nchannels; ++i){
            x->x_vec[i] = &sp[0]->s_vec[i * blocksize];
        }
    } else
#endif
    {
        nchannels = x->x_nchannels;
        for (int i = 0; i < nchannels; ++i){
            x->x_vec[i] = sp[i]->s_vec;
        }
    }
#if PD_FLOATSIZE != AOO_SAMPLE_SIZE
    x->x_buffer.resize(nchannels * blocksize);
#endif

    if (blocksize != x->x_blocksize || samplerate != x->x_samplerate
            || nchannels != x->x_nchannels) {
        x->x_source->setup(nchannels, samplerate, blocksize, kAooFixedBlockSize);
        x->x_blocksize = blocksize;
        x->x_samplerate = samplerate;
        x->x_nchannels = nchannels;

        x->update_format();
    }

    dsp_add(aoo_send_perform, 2, (t_int)x, (t_int)x->x_blocksize);
}

static void aoo_send_set(t_aoo_send *x, t_floatarg f1, t_floatarg f2)
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
        x->x_node->release((t_pd *)x, x->x_source.get());
    }

    if (id != x->x_id) {
        x->x_source->setId(id);
        x->x_id = id;
    }

    if (port) {
        x->x_node = t_node::get((t_pd *)x, port, x->x_source.get(), id);
    } else {
        x->x_node = nullptr;
    }
    x->x_port = port;
}

static void aoo_send_port(t_aoo_send *x, t_floatarg f)
{
    aoo_send_set(x, f, x->x_id);
}

static void aoo_send_id(t_aoo_send *x, t_floatarg f)
{
    aoo_send_set(x, x->x_port, f);
}

static void * aoo_send_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_send_class);
    new (x) t_aoo_send(argc, argv);
    return x;
}

t_aoo_send::t_aoo_send(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_send_tick);

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
    int ninlets;
    if (x_multi) {
        // one multi-channel inlet
        // NB: x_nchannels will be set in the "dsp" method and the format
        // will be updated accordingly.
        ninlets = 1;
    } else {
        // NB: users may explicitly specify 0 channels for pure message streams!
        // (In this case, the user must provide the number of "message channels",
        // if needed, with the "format" message.)
        if (argc > 0) {
            ninlets = atom_getfloat(argv);
            argv++;
            argc--;
            if (ninlets < 0) {
                ninlets = 0;
            } else if (ninlets > AOO_MAX_NUM_CHANNELS) {
                // see comment above AOO_MAX_NUM_CHANNELS
                pd_error(this, "%s: channel count (%d) out of range",
                         classname(this), ninlets);
                ninlets = 0;
            }
        } else {
            ninlets = 1; // default
        }
        x_nchannels = ninlets;
    }

    // arg #2/#1 (optional): port number
    // NB: 0 means "don't listen"
    int port = atom_getfloatarg(0, argc, argv);

    // arg #3/#2 (optional): ID
    AooId id = atom_getfloatarg(1, argc, argv);
    if (id < 0) {
        pd_error(this, "%s: bad id % d, setting to 0", classname(this), id);
        id = 0;
    }
    x_id = id;

    // make additional inlets
    for (int i = 1; i < ninlets; i++) {
        inlet_new(&x_obj, &x_obj.ob_pd, &s_signal, &s_signal);
    }
    // channel vector
    if (x_nchannels > 0) {
        x_vec = std::make_unique<t_sample *[]>(x_nchannels);
    }
    // make event outlet
    x_msgout = outlet_new(&x_obj, 0);

    // create and initialize AooSource object
    x_source = AooSource::create(x_id);

    // set event handler
    x_source->setEventHandler((AooEventHandler)aoo_send_handle_event,
                              this, kAooEventModePoll);

    // set default format
    t_atom codec;
    SETSYMBOL(&codec, gensym("pcm"));
    x_format.push_back(codec);
    update_format();

    x_source->setBufferSize(DEFBUFSIZE * 0.001);

    // finally we're ready to receive messages
    aoo_send_port(this, port);
}

static void aoo_send_free(t_aoo_send *x)
{
    x->~t_aoo_send();
}

t_aoo_send::~t_aoo_send()
{
    // first stop receiving messages
    if (x_node){
        x_node->release((t_pd *)this, x_source.get());
    }

    clock_free(x_clock);
}

void aoo_send_tilde_setup(void)
{
    aoo_send_class = class_new(gensym("aoo_send~"), (t_newmethod)(void *)aoo_send_new,
        (t_method)aoo_send_free, sizeof(t_aoo_send), CLASS_MULTICHANNEL, A_GIMME, A_NULL);
    CLASS_MAINSIGNALIN(aoo_send_class, t_aoo_send, x_f);
    class_addlist(aoo_send_class, (t_method)aoo_send_list);
    class_addmethod(aoo_send_class, (t_method)aoo_send_dsp,
                    gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_port,
                    gensym("port"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_id,
                    gensym("id"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_set,
                    gensym("set"), A_DEFFLOAT, A_DEFFLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_add,
                    gensym("add"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_remove,
                    gensym("remove"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_active,
                    gensym("active"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_start,
                    gensym("start"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_stop,
                    gensym("stop"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_reset,
                    gensym("reset"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_sink_list,
                    gensym("sink_list"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_auto_invite,
                    gensym("auto_invite"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_invite,
                    gensym("invite"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_uninvite,
                    gensym("uninvite"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_format,
                    gensym("format"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_codec_set,
                    gensym("codec_set"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_codec_get,
                    gensym("codec_get"), A_SYMBOL, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_sink_channel,
                    gensym("sink_channel"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_packetsize,
                    gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_ping,
                    gensym("ping"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_stream_time,
                    gensym("stream_time"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_resend,
                    gensym("resend"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_redundancy,
                    gensym("redundancy"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_resample_method,
                    gensym("resample_method"), A_SYMBOL, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_dynamic_resampling,
                    gensym("dynamic_resampling"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_dll_bandwidth,
                    gensym("dll_bandwidth"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_real_samplerate,
                    gensym("real_samplerate"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_binary,
                    gensym("binary"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_buffersize,
                    gensym("buffersize"), A_FLOAT, A_NULL);
}
