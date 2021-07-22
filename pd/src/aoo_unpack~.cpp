/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoo/aoo_sink.hpp"

#include <string.h>
#include <assert.h>

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

#define DEFBUFSIZE 20

static t_class *aoo_unpack_class;

struct t_aoo_unpack
{
    t_aoo_unpack(int argc, t_atom *argv);
    ~t_aoo_unpack();

    t_object x_obj;

    t_float x_f = 0;
    AooSink::Ptr x_sink;
    aoo::ip_address x_addr; // fake address
    int32_t x_samplerate = 0;
    int32_t x_blocksize = 0;
    int32_t x_nchannels = 0;
    std::unique_ptr<t_sample *[]> x_vec;
    t_outlet *x_dataout = nullptr;
    t_outlet *x_msgout = nullptr;
    t_clock *x_clock = nullptr;
};

static int32_t aoo_unpack_send(t_aoo_unpack *x, const char *data, int32_t n,
                               const void *address, int32_t addrlen, uint32_t flags)
{
    t_atom *a = (t_atom *)alloca(n * sizeof(t_atom));
    for (int i = 0; i < n; ++i){
        SETFLOAT(&a[i], (unsigned char)data[i]);
    }
    outlet_list(x->x_dataout, &s_list, n, a);
    return 1;
}

static void aoo_unpack_list(t_aoo_unpack *x, t_symbol *s, int argc, t_atom *argv)
{
    auto msg = (AooByte *)alloca(argc);
    for (int i = 0; i < argc; ++i){
        msg[i] = (int)(argv[i].a_type == A_FLOAT ? argv[i].a_w.w_float : 0.f);
    }
    // handle incoming message
    x->x_sink->handleMessage(msg, argc, x->x_addr.address(), x->x_addr.length());
}

static void aoo_unpack_invite(t_aoo_unpack *x, t_floatarg f)
{
    AooEndpoint ep { x->x_addr.address(),
        (AooAddrSize)x->x_addr.length(), (AooId)f };
    x->x_sink->inviteSource(ep);
    // send outgoing messages
    x->x_sink->send((AooSendFunc)aoo_unpack_send, x);
}

static void aoo_unpack_uninvite(t_aoo_unpack *x, t_floatarg f)
{
    AooEndpoint ep { x->x_addr.address(),
        (AooAddrSize)x->x_addr.length(), (AooId)f };
    x->x_sink->uninviteSource(ep);
    // send outgoing messages
    x->x_sink->send((AooSendFunc)aoo_unpack_send, x);
}

static void aoo_unpack_buffersize(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->setBufferSize(f * 0.001);
}

static void aoo_unpack_dll_bandwidth(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->setDllBandwidth(f);
}

static void aoo_unpack_reset(t_aoo_unpack *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc){
        // reset specific source
        AooId id = atom_getfloat(argv);
        AooEndpoint ep { x->x_addr.address(), (AooAddrSize)x->x_addr.length(), id };
        x->x_sink->resetSource(ep);
    } else {
        // reset all sources
        x->x_sink->reset();
    }
}

static void aoo_unpack_packetsize(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->setPacketSize(f);
}

static void aoo_unpack_resend(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->setResendData(f != 0);
}

static void aoo_unpack_resend_limit(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->setResendLimit(f);
}

static void aoo_unpack_resend_interval(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->setResendInterval(f * 0.001);
}

static void aoo_unpack_handle_event(t_aoo_unpack *x, const AooEvent *event, int32_t)
{
    // handle events
    t_atom msg[32];
    switch (event->type){
    case kAooEventSourceAdd:
    {
        auto e = (const AooEventSourceAdd *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        outlet_anything(x->x_msgout, gensym("source_add"), 1, msg);
        break;
    }
    case kAooEventSourceRemove:
    {
        auto e = (const AooEventSourceRemove *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        outlet_anything(x->x_msgout, gensym("source_remove"), 1, msg);
        break;
    }
    case kAooEventInviteTimeout:
    {
        auto e = (const AooEventInviteTimeout *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        outlet_anything(x->x_msgout, gensym("invite_timeout"), 1, msg);
        break;
    }
    case kAooEventFormatChange:
    {
        auto e = (const AooEventFormatChange *)event;
        AooFormatStorage f;
        SETFLOAT(&msg[0], e->endpoint.id);
        int fsize = format_to_atoms(*e->format, 31, msg + 1); // skip first atom
        outlet_anything(x->x_msgout, gensym("source_format"), fsize + 1, msg);
        break;
    }
    case kAooEventStreamState:
    {
        auto e = (const AooEventStreamState *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        SETFLOAT(&msg[1], e->state);
        outlet_anything(x->x_msgout, gensym("source_state"), 2, msg);
        break;
    }
    case kAooEventBlockLost:
    {
        auto e = (const AooEventBlockLost *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_lost"), 2, msg);
        break;
    }
    case kAooEventBlockReordered:
    {
        auto e = (const AooEventBlockReordered *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_reordered"), 2, msg);
        break;
    }
    case kAooEventBlockResent:
    {
        auto e = (const AooEventBlockResent *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_resent"), 2, msg);
        break;
    }
    case kAooEventBlockGap:
    {
        auto e = (const AooEventBlockGap *)event;
        SETFLOAT(&msg[0], e->endpoint.id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_gap"), 2, msg);
        break;
    }
    case kAooEventPing:
    {
        auto e = (const AooEventPing *)event;
        uint64_t t1 = e->tt1;
        uint64_t t2 = e->tt2;
        double diff = aoo_ntpTimeDuration(t1, t2) * 1000.0;

        SETFLOAT(msg, e->endpoint.id);
        SETFLOAT(msg + 1, diff);
        outlet_anything(x->x_msgout, gensym("ping"), 2, msg);
        break;
    }
    default:
        break;
    }
}

static void aoo_unpack_tick(t_aoo_unpack *x)
{
    // send outgoing messages
    x->x_sink->send((AooSendFunc)aoo_unpack_send, x);
    // poll events
    if (x->x_sink->eventsAvailable()){
        x->x_sink->pollEvents();
    }
}

uint64_t aoo_pd_osctime(int n, t_float sr);

static t_int * aoo_unpack_perform(t_int *w)
{
    t_aoo_unpack *x = (t_aoo_unpack *)(w[1]);
    int n = (int)(w[2]);

    x->x_sink->process(x->x_vec.get(), n,
                       aoo_getCurrentNtpTime());

    clock_delay(x->x_clock, 0);

    return w + 3;
}

static void aoo_unpack_dsp(t_aoo_unpack *x, t_signal **sp)
{
    int32_t blocksize = sp[0]->s_n;
    int32_t samplerate = sp[0]->s_sr;

    for (int i = 0; i < x->x_nchannels; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }

    if (blocksize != x->x_blocksize || samplerate != x->x_samplerate){
        x->x_sink->setup(samplerate, blocksize, x->x_nchannels);
        x->x_blocksize = blocksize;
        x->x_samplerate = samplerate;
    }

    dsp_add(aoo_unpack_perform, 2, (t_int)x, (t_int)x->x_blocksize);
}

static void * aoo_unpack_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_unpack_class);
    new (x) t_aoo_unpack(argc, argv);
    return x;
}

t_aoo_unpack::t_aoo_unpack(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_unpack_tick);

    // arg #1: ID
    AooId id = atom_getfloatarg(0, argc, argv);

    // arg #2: num channels
    int nchannels = atom_getfloatarg(1, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x_nchannels = nchannels;

    // arg #3: buffer size (ms)
    float buffersize = argc > 2 ? atom_getfloat(argv + 2) : DEFBUFSIZE;

    // make signal outlets
    for (int i = 0; i < nchannels; ++i){
        outlet_new(&x_obj, &s_signal);
    }
    x_vec = std::make_unique<t_sample *[]>(nchannels);
    // message outlet
    x_dataout = outlet_new(&x_obj, 0);
    // event outlet
    x_msgout = outlet_new(&x_obj, 0);

    // create and initialize AooSink object
    x_sink = AooSink::create(id, 0, nullptr);

    x_sink->setEventHandler((AooEventHandler)aoo_unpack_handle_event,
                            this, kAooEventModePoll);

    x_sink->setBufferSize(buffersize * 0.001);
}

static void aoo_unpack_free(t_aoo_unpack *x)
{
    x->~t_aoo_unpack();
}

t_aoo_unpack::~t_aoo_unpack()
{
    clock_free(x_clock);
}

void aoo_unpack_tilde_setup(void)
{
    aoo_unpack_class = class_new(gensym("aoo_unpack~"), (t_newmethod)(void *)aoo_unpack_new,
        (t_method)aoo_unpack_free, sizeof(t_aoo_unpack), 0, A_GIMME, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_dsp, gensym("dsp"), A_CANT, A_NULL);
    class_addlist(aoo_unpack_class, (t_method)aoo_unpack_list);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_invite,
                    gensym("invite"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_uninvite,
                    gensym("uninvite"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_buffersize,
                    gensym("bufsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_dll_bandwidth,
                    gensym("dll_bandwidth"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_packetsize,
                    gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_resend,
                    gensym("resend"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_resend_limit,
                    gensym("resend_limit"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_resend_interval,
                    gensym("resend_interval"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_reset,
                    gensym("reset"), A_GIMME, A_NULL);
}
