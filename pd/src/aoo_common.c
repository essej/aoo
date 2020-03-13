#include "m_pd.h"
#include "aoo/aoo.h"
#include "aoo/aoo_pcm.h"
#include "aoo/aoo_opus.h"

#include <stdio.h>
#include <inttypes.h>

#define classname(x) class_getname(*(t_pd *)x)

#define CLAMP(x, a, b) ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))

#ifndef AOO_DEBUG_OSCTIME
#define AOO_DEBUG_OSCTIME 0
#endif

#define AOO_PD_MINPERIOD 0.5

#ifndef AOO_PD_OSCTIMEHACK
#define AOO_PD_OSCTIMEHACK 0
#endif

uint64_t aoo_pd_osctime(int n, t_float sr)
{
    uint64_t t = aoo_osctime_get();
#if AOO_PD_OSCTIMEHACK || AOO_DEBUG_OSCTIME
    double s = aoo_osctime_toseconds(t);
    double period = (double)n / sr;
    double diff;
    static PERTHREAD double last = 0;
    if (last > 0){
        diff = s - last;
    } else {
        diff = period;
    }
    last = s;
#endif
#if AOO_PD_OSCTIMEHACK
    // HACK to catch blocks calculated in a row because of Pd's ringbuffer scheduler
    static PERTHREAD uint64_t osc = 0;
    static PERTHREAD int32_t count = 0;
    if (diff > period * AOO_PD_MINPERIOD){
        osc = t;
        count = 0;
    } else {
        // approximate timestamp
        count++;
        t = aoo_osctime_addseconds(t, period * count);
    }
#endif
#if AOO_DEBUG_OSCTIME
    s = aoo_osctime_toseconds(t);
    fprintf(stderr, "osctime: %" PRIu64 ", seconds: %f, diff (ms): %f\n", t, s, diff * 1000.0);
    fflush(stderr);
#endif
    return t;
}

static int aoo_getarg(const char *name, void *x, int which, int argc, t_atom *argv, t_float *f, t_float def)
{
    if (argc > which){
        if (argv[which].a_type == A_SYMBOL){
            t_symbol *sym = argv[which].a_w.w_symbol;
            if (sym == gensym("auto")){
                *f = def;
            } else {
                pd_error(x, "%s: bad '%s' argument '%s'", classname(x), name, sym->s_name);
                return 0;
            }
        } else {
            *f = atom_getfloat(argv + which);
        }
    } else {
        *f = def;
    }
    return 1;
}

int aoo_parseresend(void *x, aoo_sink_settings *s, int argc, t_atom *argv)
{
    t_float f;
    if (aoo_getarg("limit", x, 0, argc, argv, &f, AOO_RESEND_LIMIT)){
        s->resend_limit = f;
    } else {
        return 0;
    }
    if (aoo_getarg("interval", x, 1, argc, argv, &f, AOO_RESEND_INTERVAL)){
        s->resend_interval = f;
    } else {
        return 0;
    }
    if (aoo_getarg("maxnumframes", x, 2, argc, argv, &f, AOO_RESEND_MAXNUMFRAMES)){
        s->resend_maxnumframes = f;
    } else {
        return 0;
    }
    if (aoo_getarg("packetsize", x, 3, argc, argv, &f, AOO_RESEND_PACKETSIZE)){
        s->resend_limit = f;
    } else {
        return 0;
    }
    return 1;
}

void aoo_defaultformat(aoo_format_storage *f, int nchannels)
{
    aoo_format_pcm *fmt = (aoo_format_pcm *)f;
    fmt->header.codec = AOO_CODEC_PCM;
    fmt->header.blocksize = 64;
    fmt->header.samplerate = sys_getsr();
    fmt->header.nchannels = nchannels;
    fmt->bitdepth = AOO_PCM_FLOAT32;
}

int aoo_parseformat(void *x, aoo_format_storage *f, int argc, t_atom *argv)
{
    t_symbol *codec = atom_getsymbolarg(0, argc, argv);
    f->header.blocksize = argc > 1 ? atom_getfloat(argv + 1) : 64;
    f->header.samplerate = argc > 2 ? atom_getfloat(argv + 2) : sys_getsr();

    if (codec == gensym(AOO_CODEC_PCM)){
        aoo_format_pcm *fmt = (aoo_format_pcm *)f;
        fmt->header.codec = AOO_CODEC_PCM;

        int bitdepth = argc > 3 ? atom_getfloat(argv + 3) : 4;
        switch (bitdepth){
        case 2:
            fmt->bitdepth = AOO_PCM_INT16;
            break;
        case 3:
            fmt->bitdepth = AOO_PCM_INT24;
            break;
        case 0: // default
        case 4:
            fmt->bitdepth = AOO_PCM_FLOAT32;
            break;
        case 8:
            fmt->bitdepth = AOO_PCM_FLOAT64;
            break;
        default:
            pd_error(x, "%s: bad bitdepth argument %d", classname(x), bitdepth);
            return 0;
        }
    } else if (codec == gensym(AOO_CODEC_OPUS)){
        aoo_format_opus *fmt = (aoo_format_opus *)f;
        fmt->header.codec = AOO_CODEC_OPUS;
        // bitrate ("auto", "max" or float)
        if (argc > 3){
            if (argv[3].a_type == A_SYMBOL){
                t_symbol *sym = argv[3].a_w.w_symbol;
                if (sym == gensym("auto")){
                    fmt->bitrate = OPUS_AUTO;
                } else if (sym == gensym("max")){
                    fmt->bitrate = OPUS_BITRATE_MAX;
                } else {
                    pd_error(x, "%s: bad bitrate argument '%s'", classname(x), sym->s_name);
                    return 0;
                }
            } else {
                int bitrate = atom_getfloat(argv + 3);
                if (bitrate > 0){
                    fmt->bitrate = bitrate;
                } else {
                    pd_error(x, "%s: bitrate argument %d out of range", classname(x), bitrate);
                    return 0;
                }
            }
        } else {
            fmt->bitrate = OPUS_AUTO;
        }
        // complexity ("auto" or 0-10)
        if (argc > 4){
            if (argv[4].a_type == A_SYMBOL){
                t_symbol *sym = argv[4].a_w.w_symbol;
                if (sym == gensym("auto")){
                    fmt->complexity = OPUS_AUTO;
                } else {
                    pd_error(x, "%s: bad complexity argument '%s'", classname(x), sym->s_name);
                    return 0;
                }
            } else {
                int complexity = atom_getfloat(argv + 4);
                if (complexity < 0 || complexity > 10){
                    pd_error(x, "%s: complexity value %d out of range", classname(x), complexity);
                    return 0;
                }
                fmt->complexity = complexity;
            }
        } else {
            fmt->complexity = OPUS_AUTO;
        }
        // signal type ("auto", "music", "voice")
        if (argc > 5){
            t_symbol *type = atom_getsymbol(argv + 5);
            if (type == gensym("auto")){
                fmt->signal_type = OPUS_AUTO;
            } else if (type == gensym("music")){
                fmt->signal_type = OPUS_SIGNAL_MUSIC;
            } else if (type == gensym("voice")){
                fmt->signal_type = OPUS_SIGNAL_VOICE;
            } else {
                pd_error(x,"%s: unsupported signal type '%s'",
                         classname(x), type->s_name);
                return 0;
            }
        } else {
            fmt->signal_type = OPUS_AUTO;
        }
    } else {
        pd_error(x, "%s: unknown codec '%s'", classname(x), codec->s_name);
        return 0;
    }
    return 1;
}
