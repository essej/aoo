#include "m_pd.h"
#include "aoo/aoo.h"

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

#define DEFBUFSIZE 10

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
    aoo_event *x_eventbuf;
    int x_eventbufsize;
    int x_numevents;
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

static void aoo_unpack_tick(t_aoo_unpack *x)
{
    for (int i = 0; i < x->x_numevents; ++i){
        if (x->x_eventbuf[i].type == AOO_SOURCE_STATE_EVENT){
            aoo_source_state_event *e = &x->x_eventbuf[i].source_state;
            t_atom msg[2];
            SETFLOAT(&msg[0], e->id);
            SETFLOAT(&msg[1], e->state);
            outlet_anything(x->x_eventout, gensym("source"), 2, msg);
        }
    }
    x->x_numevents = 0;
}

static void aoo_unpack_process(t_aoo_unpack *x, const aoo_sample **data, int32_t n,
                               const aoo_event *events, int32_t nevents)
{
    assert(sizeof(t_sample) == sizeof(aoo_sample));
    for (int i = 0; i < x->x_settings.nchannels; ++i){
        memcpy(x->x_vec[i], data[i], sizeof(aoo_sample) * n);
    }
    // handle events
    if (nevents > 0){
        // resize event buffer if necessary
        if (nevents > x->x_eventbufsize){
            x->x_eventbuf = (aoo_event *)resizebytes(x->x_eventbuf,
                sizeof(aoo_event) * x->x_eventbufsize, sizeof(aoo_event) * nevents);
            x->x_eventbufsize = nevents;
        }
        // copy events
        for (int i = 0; i < nevents; ++i){
            x->x_eventbuf[i] = events[i];
        }
        x->x_numevents = nevents;

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
    // pre-allocate event buffer
    x->x_eventbuf = getbytes(sizeof(aoo_event) * 16);
    x->x_eventbufsize = 16;
    x->x_numevents = 0;
    x->x_clock = clock_new(x, (t_method)aoo_unpack_tick);

    // arg #1: ID
    int id = atom_getfloatarg(0, argc, argv);
    x->x_aoo_sink = aoo_sink_new(id >= 0 ? id : 0);
    memset(&x->x_settings, 0, sizeof(aoo_sink_settings));
    x->x_settings.userdata = x;
    x->x_settings.processfn = (aoo_processfn)aoo_unpack_process;

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
    freebytes(x->x_eventbuf, sizeof(aoo_event) * x->x_eventbufsize);
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

    aoo_setup();
}
