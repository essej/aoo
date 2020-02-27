#include "m_pd.h"
#include "aoo/aoo.h"

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
    t_atom x_sink_id_arg;
    int32_t x_sink_id;
    int32_t x_sink_chn;
} t_aoo_pack;

static void aoo_pack_tick(t_aoo_pack *x)
{
    if (!aoo_source_send(x->x_aoo_source)){
        bug("aoo_pack_tick");
    }
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

static void aoo_pack_format(t_aoo_pack *x, t_symbol *s, int argc, t_atom *argv)
{
    t_symbol *type = atom_getsymbolarg(0, argc, argv);
    if (type == gensym(AOO_CODEC_PCM)){
        aoo_format fmt;
        fmt.nchannels = x->x_settings.nchannels;
        fmt.blocksize = argc > 1 ? atom_getfloat(argv + 1) : 64;
        fmt.samplerate = argc > 2 ? atom_getfloat(argv + 2) : sys_getsr();

        aoo_pcm_settings pcm;
        int bitdepth = argc > 3 ? atom_getfloat(argv + 3) : 4;
        switch (bitdepth){
        case 2:
            pcm.bitdepth = AOO_INT16;
            break;
        case 3:
            pcm.bitdepth = AOO_INT24;
            break;
        case 0: // default
        case 4:
            pcm.bitdepth = AOO_FLOAT32;
            break;
        case 8:
            pcm.bitdepth = AOO_FLOAT64;
            break;
        default:
            pd_error(x, "%s: bad bitdepth argument %d", classname(x), bitdepth);
            return;
        }
        fmt.codec = type->s_name;
        fmt.settings = &pcm;

        aoo_source_setformat(x->x_aoo_source, &fmt);
    } else {
        pd_error(x, "%s: unknown codec '%s'", classname(x), type->s_name);
    }
}

static void aoo_pack_channel(t_aoo_pack *x, t_floatarg f)
{
    if (f >= 0){
        aoo_source_setsinkchannel(x->x_aoo_source, x, x->x_sink_id, f);
        x->x_sink_chn = f;
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
                return;
            }
            x->x_sink_id = AOO_ID_WILDCARD;
        } else {
            int32_t id = atom_getfloat(argv);
            aoo_source_addsink(x->x_aoo_source, x, id, (aoo_replyfn)aoo_pack_reply);
            x->x_sink_id = id;
        }
        aoo_pack_channel(x, atom_getfloatarg(1, argc, argv));
    }
}

static void aoo_pack_clear(t_aoo_pack *x)
{
    aoo_source_removeall(x->x_aoo_source);
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
        if (x->x_sink_id_arg.a_type != A_NULL){
            // set sink ID
            aoo_pack_set(x, 0, 1, &x->x_sink_id_arg);
            aoo_pack_channel(x, x->x_sink_chn);
        }
    }
}

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
    x->x_sink_id = -1;
    if (argc > 2){
        x->x_sink_id_arg.a_type = argv[2].a_type;
        x->x_sink_id_arg.a_w = argv[2].a_w;
    } else {
        x->x_sink_id_arg.a_type = A_NULL;
    }

    // arg #4: sink channel
    x->x_sink_chn = atom_getfloatarg(3, argc, argv);

    // make additional inlets
    if (nchannels > 1){
        while (--nchannels){
            inlet_new(&x->x_obj, &x->x_obj.ob_pd, &s_signal, &s_signal);
        }
    }
    x->x_vec = (t_sample **)getbytes(sizeof(t_sample *) * nchannels);
    // make message outlet
    x->x_out = outlet_new(&x->x_obj, 0);

    // default format
    aoo_format fmt;
    fmt.blocksize = 64;
    fmt.samplerate = sys_getsr();
    fmt.nchannels = nchannels;
    fmt.codec = AOO_CODEC_PCM;
    aoo_pcm_settings pcm;
    pcm.bitdepth = AOO_FLOAT32;
    fmt.settings = &pcm;
    aoo_source_setformat(x->x_aoo_source, &fmt);

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
}
