#include "m_pd.h"
#include "aoo/aoo.h"

#include "aoo_common.h"

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

#define classname(x) class_getname(*(t_pd *)x)

#define DEFBUFSIZE 20

static t_class *aoo_unpack_class;

typedef struct _aoo_unpack
{
    t_object x_obj;
    t_float x_f;
    aoo_sink *x_aoo_sink;
    aoo_sink_settings x_settings;
    t_sample **x_vec;
    t_outlet *x_msgout;
    t_outlet *x_eventout;
    t_clock *x_clock;
} t_aoo_unpack;

static void aoo_pack_reply(t_aoo_unpack *x, const char *data, int32_t n)
{
    t_atom *a = (t_atom *)alloca(n * sizeof(t_atom));
    for (int i = 0; i < n; ++i){
        SETFLOAT(&a[i], (unsigned char)data[i]);
    }
    outlet_list(x->x_msgout, &s_list, n, a);
}

static void aoo_unpack_list(t_aoo_unpack *x, t_symbol *s, int argc, t_atom *argv)
{
    char *msg = (char *)alloca(argc);
    for (int i = 0; i < argc; ++i){
        msg[i] = (int)(argv[i].a_type == A_FLOAT ? argv[i].a_w.w_float : 0.f);
    }
    aoo_sink_handlemessage(x->x_aoo_sink, msg, argc, x, (aoo_replyfn)aoo_pack_reply);
}

static void aoo_unpack_buffersize(t_aoo_unpack *x, t_floatarg f)
{
    x->x_settings.buffersize = f;
    if (x->x_settings.blocksize){
        aoo_sink_setup(x->x_aoo_sink, &x->x_settings);
    }
}

static void aoo_unpack_timefilter(t_aoo_unpack *x, t_floatarg f)
{
    x->x_settings.time_filter_bandwidth = f;
    if (x->x_settings.blocksize){
        aoo_sink_setup(x->x_aoo_sink, &x->x_settings);
    }
}

static void aoo_unpack_ping(t_aoo_unpack *x, t_floatarg f)
{
    x->x_settings.ping_interval = f > 0 ? f : 0;
    if (x->x_settings.blocksize){
        aoo_sink_setup(x->x_aoo_sink, &x->x_settings);
    }
}

static void aoo_unpack_resend(t_aoo_unpack *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!aoo_parseresend(x, &x->x_settings, argc, argv)){
        return;
    }
    if (x->x_settings.blocksize){
        aoo_sink_setup(x->x_aoo_sink, &x->x_settings);
    }
}

static void aoo_unpack_tick(t_aoo_unpack *x)
{
    aoo_sink_handleevents(x->x_aoo_sink);
}

static void aoo_unpack_handleevents(t_aoo_unpack *x,
                                    const aoo_event *events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        t_atom msg[32];
        switch (events[i].type){
        case AOO_FORMAT_EVENT:
        {
            const aoo_event_header *e = &events[i].header;
            aoo_format_storage f;
            if (aoo_sink_getsourceformat(x->x_aoo_sink, e->endpoint, e->id, &f)){
                SETFLOAT(&msg[0], events[i].header.id);
                int fsize = aoo_printformat(&f, 31, msg + 1); // skip first atom
                outlet_anything(x->x_eventout, gensym("format"), fsize + 1, msg);
            }
            break;
        }
        case AOO_SOURCE_STATE_EVENT:
        {
            const aoo_source_state_event *e = &events[i].source_state;
            SETFLOAT(&msg[0], e->header.id);
            SETFLOAT(&msg[1], e->state);
            outlet_anything(x->x_eventout, gensym("source_state"), 2, msg);
            break;
        }
        case AOO_BLOCK_LOSS_EVENT:
        {
            const aoo_block_loss_event *e = &events[i].block_loss;
            SETFLOAT(&msg[0], e->header.id);
            SETFLOAT(&msg[1], e->count);
            outlet_anything(x->x_eventout, gensym("block_loss"), 2, msg);
            break;
        }
        case AOO_BLOCK_REORDER_EVENT:
        {
            const aoo_block_reorder_event *e = &events[i].block_reorder;
            SETFLOAT(&msg[0], e->header.id);
            SETFLOAT(&msg[1], e->count);
            outlet_anything(x->x_eventout, gensym("block_reorder"), 2, msg);
            break;
        }
        case AOO_BLOCK_RESEND_EVENT:
        {
            const aoo_block_reorder_event *e = &events[i].block_resend;
            SETFLOAT(&msg[0], e->header.id);
            SETFLOAT(&msg[1], e->count);
            outlet_anything(x->x_eventout, gensym("block_resend"), 2, msg);
            break;
        }
        case AOO_BLOCK_GAP_EVENT:
        {
            const aoo_block_gap_event *e = &events[i].block_gap;
            SETFLOAT(&msg[0], e->header.id);
            SETFLOAT(&msg[1], e->count);
            outlet_anything(x->x_eventout, gensym("block_gap"), 2, msg);
            break;
        }
        default:
            break;
        }
    }
}

static void aoo_unpack_process(t_aoo_unpack *x,
                               const aoo_sample **data, int32_t n)
{
    assert(sizeof(t_sample) == sizeof(aoo_sample));
    for (int i = 0; i < x->x_settings.nchannels; ++i){
        memcpy(x->x_vec[i], data[i], sizeof(aoo_sample) * n);
    }
    // handle events
    if (aoo_sink_eventsavailable(x->x_aoo_sink)){
        clock_delay(x->x_clock, 0);
    }
}

uint64_t aoo_pd_osctime(int n, t_float sr);

static t_int * aoo_unpack_perform(t_int *w)
{
    t_aoo_unpack *x = (t_aoo_unpack *)(w[1]);
    int n = (int)(w[2]);

    uint64_t t = aoo_pd_osctime(n, x->x_settings.samplerate);
    if (!aoo_sink_process(x->x_aoo_sink, t)){
        // output zeros
        for (int i = 0; i < x->x_settings.nchannels; ++i){
            memset(x->x_vec[i], 0, sizeof(t_float) * n);
        }
    }

    return w + 3;
}

static void aoo_unpack_dsp(t_aoo_unpack *x, t_signal **sp)
{
    int n = x->x_settings.blocksize = (int)sp[0]->s_n;
    x->x_settings.samplerate = sp[0]->s_sr;

    for (int i = 0; i < x->x_settings.nchannels; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }
    aoo_sink_setup(x->x_aoo_sink, &x->x_settings);

    dsp_add(aoo_unpack_perform, 2, (t_int)x, (t_int)n);
}

static void * aoo_unpack_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_unpack *x = (t_aoo_unpack *)pd_new(aoo_unpack_class);
    x->x_clock = clock_new(x, (t_method)aoo_unpack_tick);
    // default settings
    memset(&x->x_settings, 0, sizeof(aoo_sink_settings));
    x->x_settings.userdata = x;
    x->x_settings.eventhandler = (aoo_eventhandler)aoo_unpack_handleevents;
    x->x_settings.processfn = (aoo_processfn)aoo_unpack_process;
    x->x_settings.ping_interval = AOO_PING_INTERVAL;
    x->x_settings.resend_limit = AOO_RESEND_LIMIT;
    x->x_settings.resend_interval = AOO_RESEND_INTERVAL;
    x->x_settings.resend_maxnumframes = AOO_RESEND_MAXNUMFRAMES;
    x->x_settings.resend_packetsize = AOO_RESEND_PACKETSIZE;

    // arg #1: ID
    int id = atom_getfloatarg(0, argc, argv);
    x->x_aoo_sink = aoo_sink_new(id >= 0 ? id : 0);

    // arg #2: num channels
    int nchannels = atom_getfloatarg(1, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x->x_settings.nchannels = nchannels;

    // arg #3: buffer size (ms)
    x->x_settings.buffersize = argc > 2 ? atom_getfloat(argv + 2) : DEFBUFSIZE;

    // make signal outlets
    for (int i = 0; i < nchannels; ++i){
        outlet_new(&x->x_obj, &s_signal);
    }
    x->x_vec = (t_sample **)getbytes(sizeof(t_sample *) * nchannels);
    // message outlet
    x->x_msgout = outlet_new(&x->x_obj, 0);
    // event outlet
    x->x_eventout = outlet_new(&x->x_obj, 0);

    return x;
}

static void aoo_unpack_free(t_aoo_unpack *x)
{
    // clean up
    freebytes(x->x_vec, sizeof(t_sample *) * x->x_settings.nchannels);
    clock_free(x->x_clock);
    aoo_sink_free(x->x_aoo_sink);
}

void aoo_unpack_tilde_setup(void)
{
    aoo_unpack_class = class_new(gensym("aoo_unpack~"), (t_newmethod)(void *)aoo_unpack_new,
        (t_method)aoo_unpack_free, sizeof(t_aoo_unpack), 0, A_GIMME, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_dsp, gensym("dsp"), A_CANT, A_NULL);
    class_addlist(aoo_unpack_class, (t_method)aoo_unpack_list);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_buffersize,
                    gensym("bufsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_timefilter,
                    gensym("timefilter"), A_FLOAT, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_resend,
                    gensym("resend"), A_GIMME, A_NULL);
    class_addmethod(aoo_unpack_class, (t_method)aoo_unpack_ping,
                    gensym("ping"), A_FLOAT, A_NULL);

    aoo_setup();
}
