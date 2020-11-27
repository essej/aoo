/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

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

using namespace aoo;

#define DEFBUFSIZE 20

static t_class *aoo_unpack_class;

struct t_aoo_unpack
{
    t_aoo_unpack(int argc, t_atom *argv);
    ~t_aoo_unpack();

    t_object x_obj;

    t_float x_f = 0;
    aoo::isink::pointer x_sink;
    aoo::ip_address x_addr; // fake address
    int32_t x_samplerate = 0;
    int32_t x_blocksize = 0;
    int32_t x_nchannels = 0;
    std::unique_ptr<t_sample *[]> x_vec;
    t_outlet *x_dataout = nullptr;
    t_outlet *x_msgout = nullptr;
    t_clock *x_clock = nullptr;
};

static int32_t aoo_unpack_reply(t_aoo_unpack *x, const char *data, int32_t n,
                                const void *address, int32_t addrlen)
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
    char *msg = (char *)alloca(argc);
    for (int i = 0; i < argc; ++i){
        msg[i] = (int)(argv[i].a_type == A_FLOAT ? argv[i].a_w.w_float : 0.f);
    }
    // handle incoming message
    x->x_sink->handle_message(msg, argc, x->x_addr.address(), x->x_addr.length());
    // send outgoing messages
    while (x->x_sink->send()) ;
}

static void aoo_unpack_invite(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->invite_source(x->x_addr.address(), x->x_addr.length(), f);
    // send outgoing messages
    while (x->x_sink->send()) ;
}

static void aoo_unpack_uninvite(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->uninvite_source(x->x_addr.address(), x->x_addr.length(), f);
    // send outgoing messages
    while (x->x_sink->send()) ;
}

static void aoo_unpack_buffersize(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->set_buffersize(f);
}

static void aoo_unpack_timefilter(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->set_timefilter_bandwidth(f);
}

static void aoo_unpack_reset(t_aoo_unpack *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc){
        // reset specific source
        int32_t id = atom_getfloat(argv);
        x->x_sink->reset_source(x->x_addr.address(), x->x_addr.length(), id);
    } else {
        // reset all sources
        x->x_sink->reset();
    }
}

static void aoo_unpack_packetsize(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->set_packetsize(f);
}

static void aoo_unpack_resend(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->set_resend_enable(f != 0);
}

static void aoo_unpack_resend_limit(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->set_resend_maxnumframes(f);
}

static void aoo_unpack_resend_interval(t_aoo_unpack *x, t_floatarg f)
{
    x->x_sink->set_resend_interval(f);
}

static void aoo_unpack_handle_event(t_aoo_unpack *x, const aoo_event *event)
{
    // handle events
    t_atom msg[32];
    switch (event->type){
    case AOO_SOURCE_ADD_EVENT:
    {
        auto e = (const aoo_source_event *)event;
        SETFLOAT(&msg[0], e->id);
        outlet_anything(x->x_msgout, gensym("source_add"), 1, msg);
        break;
    }
    case AOO_SOURCE_FORMAT_EVENT:
    {
        auto e = (const aoo_format_event *)event;
        aoo_format_storage f;
        SETFLOAT(&msg[0], e->id);
        int fsize = format_to_atoms(*e->format, 31, msg + 1); // skip first atom
        outlet_anything(x->x_msgout, gensym("source_format"), fsize + 1, msg);
        break;
    }
    case AOO_SOURCE_STATE_EVENT:
    {
        auto e = (const aoo_source_state_event *)event;
        SETFLOAT(&msg[0], e->id);
        SETFLOAT(&msg[1], e->state);
        outlet_anything(x->x_msgout, gensym("source_state"), 2, msg);
        break;
    }
    case AOO_BLOCK_LOST_EVENT:
    {
        auto e = (const aoo_block_lost_event *)event;
        SETFLOAT(&msg[0], e->id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_lost"), 2, msg);
        break;
    }
    case AOO_BLOCK_REORDERED_EVENT:
    {
        auto e = (const aoo_block_reordered_event *)event;
        SETFLOAT(&msg[0], e->id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_reordered"), 2, msg);
        break;
    }
    case AOO_BLOCK_RESENT_EVENT:
    {
        auto e = (const aoo_block_resent_event *)event;
        SETFLOAT(&msg[0], e->id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_resent"), 2, msg);
        break;
    }
    case AOO_BLOCK_GAP_EVENT:
    {
        auto e = (const aoo_block_gap_event *)event;
        SETFLOAT(&msg[0], e->id);
        SETFLOAT(&msg[1], e->count);
        outlet_anything(x->x_msgout, gensym("block_gap"), 2, msg);
        break;
    }
    case AOO_PING_EVENT:
    {
        auto e = (const aoo_ping_event *)event;
        uint64_t t1 = e->tt1;
        uint64_t t2 = e->tt2;
        double diff = aoo_osctime_duration(t1, t2) * 1000.0;

        SETFLOAT(msg, e->id);
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
    x->x_sink->poll_events((aoo_eventhandler)aoo_unpack_handle_event, x);
}

uint64_t aoo_pd_osctime(int n, t_float sr);

static t_int * aoo_unpack_perform(t_int *w)
{
    t_aoo_unpack *x = (t_aoo_unpack *)(w[1]);
    int n = (int)(w[2]);

    uint64_t t = aoo_osctime_now();
    if (x->x_sink->process(x->x_vec.get(), n, t) <= 0){
        // output zeros
        for (int i = 0; i < x->x_nchannels; ++i){
            memset(x->x_vec[i], 0, sizeof(t_sample) * n);
        }
    }

    // handle events
    if (x->x_sink->events_available()){
        clock_delay(x->x_clock, 0);
    }

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
    aoo_id id = atom_getfloatarg(0, argc, argv);

    // arg #2: num channels
    int nchannels = atom_getfloatarg(1, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x_nchannels = nchannels;

    // arg #3: buffer size (ms)
    int buffersize = argc > 2 ? atom_getfloat(argv + 2) : DEFBUFSIZE;

    // make signal outlets
    for (int i = 0; i < nchannels; ++i){
        outlet_new(&x_obj, &s_signal);
    }
    x_vec = std::make_unique<t_sample *[]>(nchannels);
    // message outlet
    x_dataout = outlet_new(&x_obj, 0);
    // event outlet
    x_msgout = outlet_new(&x_obj, 0);

    // create and initialize aoo_sink object
    auto sink = aoo::isink::create(id >= 0 ? id : 0,
                                   (aoo_replyfn)aoo_unpack_reply, this);
    x_sink.reset(sink);

    x_sink->set_buffersize(buffersize);
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
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_timefilter,
                    gensym("timefilter"), A_FLOAT, A_NULL);
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
