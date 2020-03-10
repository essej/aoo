#include "m_pd.h"
#include "aoo/aoo.h"
#include "aoo/aoo_pcm.h"
#include "aoo/aoo_opus.h"

#include <string.h>
#include <assert.h>
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

#define classname(x) class_getname(*(t_pd *)x)

static t_class *aoo_pack_class;

typedef struct _aoo_pack
{
    t_object x_obj;
    t_float x_f;
    aoo_source *x_aoo_source;
    aoo_source_settings x_settings;
    t_float **x_vec;
    t_clock *x_clock;
    t_outlet *x_out;
    int32_t x_sink_id;
    int32_t x_sink_chn;
} t_aoo_pack;

static void aoo_pack_tick(t_aoo_pack *x)
{
    aoo_source_send(x->x_aoo_source);
}

static void aoo_pack_reply(t_aoo_pack *x, const char *data, int32_t n)
{
    t_atom *a = (t_atom *)alloca(n * sizeof(t_atom));
    for (int i = 0; i < n; ++i){
        SETFLOAT(&a[i], (unsigned char)data[i]);
    }
    outlet_list(x->x_out, &s_list, n, a);
}

static void aoo_pack_list(t_aoo_pack *x, t_symbol *s, int argc, t_atom *argv)
{
    char *msg = (char *)alloca(argc);
    for (int i = 0; i < argc; ++i){
        msg[i] = (int)(argv[i].a_type == A_FLOAT ? argv[i].a_w.w_float : 0.f);
    }
    aoo_source_handlemessage(x->x_aoo_source, msg, argc, x, (aoo_replyfn)aoo_pack_reply);
}

int aoo_parseformat(void *x, aoo_format_storage *f, int argc, t_atom *argv);

static void aoo_pack_format(t_aoo_pack *x, t_symbol *s, int argc, t_atom *argv)
{
    aoo_format_storage f;
    f.header.nchannels = x->x_settings.nchannels;
    if (aoo_parseformat(x, &f, argc, argv)){
        aoo_source_setformat(x->x_aoo_source, &f.header);
    }
}

static void aoo_pack_channel(t_aoo_pack *x, t_floatarg f)
{
    x->x_sink_chn = f > 0 ? f : 0;
    if (x->x_sink_id != AOO_ID_NONE){
        aoo_source_setsinkchannel(x->x_aoo_source, x, x->x_sink_id, x->x_sink_chn);
    }
}

static void aoo_pack_packetsize(t_aoo_pack *x, t_floatarg f)
{
    x->x_settings.packetsize = f;
    if (x->x_settings.blocksize){
        aoo_source_setup(x->x_aoo_source, &x->x_settings);
    }
}

static void aoo_pack_timefilter(t_aoo_pack *x, t_floatarg f)
{
    x->x_settings.time_filter_bandwidth = f;
    if (x->x_settings.blocksize){
        aoo_source_setup(x->x_aoo_source, &x->x_settings);
    }
}

static void aoo_pack_set(t_aoo_pack *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc){
        // remove old sink
        aoo_source_removeall(x->x_aoo_source);
        // add new sink
        if (argv->a_type == A_SYMBOL){
            if (*argv->a_w.w_symbol->s_name == '*'){
                aoo_source_addsink(x->x_aoo_source, x, AOO_ID_WILDCARD, (aoo_replyfn)aoo_pack_reply);
            } else {
                pd_error(x, "%s: bad argument '%s' to 'set' message!",
                         classname(x), argv->a_w.w_symbol->s_name);
                return;
            }
            aoo_source_setsinkchannel(x->x_aoo_source, x, AOO_ID_WILDCARD, x->x_sink_chn);
            x->x_sink_id = AOO_ID_WILDCARD;
        } else {
            int32_t id = atom_getfloat(argv);
            aoo_source_addsink(x->x_aoo_source, x, id, (aoo_replyfn)aoo_pack_reply);
            aoo_source_setsinkchannel(x->x_aoo_source, x, id, x->x_sink_chn);
            x->x_sink_id = id;
        }
        aoo_pack_channel(x, atom_getfloatarg(1, argc, argv));
    }
}

static void aoo_pack_clear(t_aoo_pack *x)
{
    aoo_source_removeall(x->x_aoo_source);
    x->x_sink_id = AOO_ID_NONE;
}

uint64_t aoo_pd_osctime(int n, t_float sr);

static t_int * aoo_pack_perform(t_int *w)
{
    t_aoo_pack *x = (t_aoo_pack *)(w[1]);
    int n = (int)(w[2]);

    assert(sizeof(t_sample) == sizeof(aoo_sample));

    uint64_t t = aoo_pd_osctime(n, x->x_settings.samplerate);
    if (aoo_source_process(x->x_aoo_source,(const aoo_sample **)x->x_vec, n, t)){
        clock_set(x->x_clock, 0);
    }
    return w + 3;
}

static void aoo_pack_dsp(t_aoo_pack *x, t_signal **sp)
{
    x->x_settings.blocksize = sp[0]->s_n;
    x->x_settings.samplerate = sp[0]->s_sr;
    aoo_source_setup(x->x_aoo_source, &x->x_settings);

    for (int i = 0; i < x->x_settings.nchannels; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }

    dsp_add(aoo_pack_perform, 2, (t_int)x, (t_int)sp[0]->s_n);

    clock_unset(x->x_clock);
}

static void aoo_pack_loadbang(t_aoo_pack *x, t_floatarg f)
{
    // LB_LOAD
    if (f == 0){
        if (x->x_sink_id != AOO_ID_NONE){
            // set sink ID
            t_atom a;
            SETFLOAT(&a, x->x_sink_id);
            aoo_pack_set(x, 0, 1, &a);
            aoo_pack_channel(x, x->x_sink_chn);
        }
    }
}

void aoo_defaultformat(aoo_format_storage *f, int nchannels);

static void * aoo_pack_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_pack *x = (t_aoo_pack *)pd_new(aoo_pack_class);

    x->x_f = 0;
    x->x_clock = clock_new(x, (t_method)aoo_pack_tick);

    // arg #1: ID
    int src = atom_getfloatarg(0, argc, argv);
    x->x_aoo_source = aoo_source_new(src >= 0 ? src : 0);
    memset(&x->x_settings, 0, sizeof(aoo_source_settings));
    x->x_settings.buffersize = AOO_SOURCE_DEFBUFSIZE;
    x->x_settings.packetsize = AOO_DEFPACKETSIZE;
    x->x_settings.time_filter_bandwidth = AOO_DLL_BW;

    // arg #2: num channels
    int nchannels = atom_getfloatarg(1, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x->x_settings.nchannels = nchannels;

    // arg #3: sink ID
    if (argc > 2){
        x->x_sink_id = atom_getfloat(argv + 2);
    } else {
        x->x_sink_id = AOO_ID_NONE;
    }

    // arg #4: sink channel
    x->x_sink_chn = atom_getfloatarg(3, argc, argv);

    // make additional inlets
    if (nchannels > 1){
        int i = nchannels;
        while (--i){
            inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
        }
    }
    x->x_vec = (t_sample **)getbytes(sizeof(t_sample *) * nchannels);
    // make message outlet
    x->x_out = outlet_new(&x->x_obj, 0);

    // default format
    aoo_format_storage fmt;
    aoo_defaultformat(&fmt, nchannels);
    aoo_source_setformat(x->x_aoo_source, &fmt.header);

    return x;
}

static void aoo_pack_free(t_aoo_pack *x)
{
    // clean up
    freebytes(x->x_vec, sizeof(t_sample *) * x->x_settings.nchannels);
    clock_free(x->x_clock);
    aoo_source_free(x->x_aoo_source);
}

void aoo_pack_tilde_setup(void)
{
    aoo_pack_class = class_new(gensym("aoo_pack~"), (t_newmethod)(void *)aoo_pack_new,
        (t_method)aoo_pack_free, sizeof(t_aoo_pack), 0, A_GIMME, A_NULL);
    CLASS_MAINSIGNALIN(aoo_pack_class, t_aoo_pack, x_f);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_dsp, gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_loadbang, gensym("loadbang"), A_FLOAT, A_NULL);
    class_addlist(aoo_pack_class, (t_method)aoo_pack_list);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_set, gensym("set"), A_GIMME, A_NULL);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_format, gensym("format"), A_GIMME, A_NULL);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_channel, gensym("channel"), A_FLOAT, A_NULL);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_packetsize, gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_clear, gensym("clear"), A_NULL);
    class_addmethod(aoo_pack_class, (t_method)aoo_pack_timefilter, gensym("timefilter"), A_FLOAT, A_NULL);

    aoo_setup();
}
