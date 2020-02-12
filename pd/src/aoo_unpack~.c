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

#define DEFBUFSIZE 10

static t_class *aoo_unpack_class;

typedef struct _aoo_unpack
{
    t_object x_obj;
    t_float x_f;
    aoo_sink *x_aoo_sink;
    t_sample **x_vec;
    int x_n;
    t_outlet *x_msgout;
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
    aoo_sink_setbuffersize(x->x_aoo_sink, f);
}

static void aoo_unpack_process(const aoo_sample **data, int32_t n, t_aoo_unpack *x)
{
    assert(sizeof(t_sample) == sizeof(aoo_sample));
    for (int i = 0; i < x->x_n; ++i){
        memcpy(x->x_vec[i], data[i], sizeof(aoo_sample) * n);
    }
}

static t_int * aoo_unpack_perform(t_int *w)
{
    t_aoo_unpack *x = (t_aoo_unpack *)(w[1]);
    int n = (int)(w[2]);

    if (!aoo_sink_process(x->x_aoo_sink)){
        // output zeros
        for (int i = 0; i < x->x_n; ++i){
            memset(x->x_vec[i], 0, sizeof(t_float) * n);
        }
    }

    return w + 3;
}

static void aoo_unpack_dsp(t_aoo_unpack *x, t_signal **sp)
{
    int n = (aoo_bitdepth)sp[0]->s_n;
    int sr = sp[0]->s_sr;

    for (int i = 0; i < x->x_n; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }
    aoo_sink_setup(x->x_aoo_sink, x->x_n, sr, n,
                   (aoo_processfn)aoo_unpack_process, x);

    dsp_add(aoo_unpack_perform, 2, (t_int)x, (t_int)sp[0]->s_n);
}

static void * aoo_unpack_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_unpack *x = (t_aoo_unpack *)pd_new(aoo_unpack_class);

    // arg #1: ID
    int id = atom_getfloatarg(0, argc, argv);
    x->x_aoo_sink = aoo_sink_new(id >= 0 ? id : 0);

    // arg #2: num channels
    int nchannels = atom_getfloatarg(1, argc, argv);
    x->x_n = nchannels > 1 ? nchannels : 1;

    // arg #3: buffer size (ms)
    aoo_unpack_buffersize(x, argc > 2 ? atom_getfloat(argv + 2) : DEFBUFSIZE);

    // make signal outlets
    for (int i = 0; i < x->x_n; ++i){
        outlet_new(&x->x_obj, &s_signal);
    }
    x->x_vec = (t_sample **)getbytes(sizeof(t_sample *) * x->x_n);
    // message outlet
    x->x_msgout = outlet_new(&x->x_obj, 0);

    return x;
}

static void aoo_unpack_free(t_aoo_unpack *x)
{
    // clean up
    freebytes(x->x_vec, sizeof(t_sample *) * x->x_n);
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
}
