#include "m_pd.h"
#include "aoo/aoo.h"

#include "aoo_common.h"
#include "aoo_net.h"

#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

#define classname(x) class_getname(*(t_pd *)x)

#define DEFBUFSIZE 10

static t_class *aoo_send_class;

typedef struct _aoo_send
{
    t_object x_obj;
    t_float x_f;
    aoo_source *x_aoo_source;
    aoo_source_settings x_settings;
    t_float **x_vec;
    int32_t x_sink_id;
    int32_t x_sink_chn;
    // events
    t_clock *x_clock;
    t_outlet *x_eventout;
    // socket
    int x_socket;
    t_endpoint *x_endpoint;
    // threading
    pthread_t x_thread;
    pthread_cond_t x_cond;
    pthread_mutex_t x_mutex;
} t_aoo_send;

static void aoo_send_handleevents(t_aoo_send *x,
                                  const aoo_event *events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        if (events[i].type == AOO_PING_EVENT){
            if (events[i].header.endpoint == x){
                t_atom msg[3];
                if (!endpoint_getaddress(x->x_endpoint, msg, msg + 1)){
                    continue;
                }
                SETFLOAT(msg + 2, events[i].header.id);
                outlet_anything(x->x_eventout, gensym("ping"), 3, msg);
            } else {
                pd_error(x, "%s: received ping from unknown sink!",
                         classname(x));
            }
        }
    }
}

static void aoo_send_tick(t_aoo_send *x)
{
    aoo_source_handleevents(x->x_aoo_source);
}

static void aoo_send_format(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    aoo_format_storage f;
    f.header.nchannels = x->x_settings.nchannels;
    if (aoo_parseformat(x, &f, argc, argv)){
        pthread_mutex_lock(&x->x_mutex);
        aoo_source_setformat(x->x_aoo_source, &f.header);
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_channel(t_aoo_send *x, t_floatarg f)
{
    x->x_sink_chn = f > 0 ? f : 0;
    if (x->x_sink_id != AOO_ID_NONE){
        pthread_mutex_lock(&x->x_mutex);
        aoo_source_setsinkchannel(x->x_aoo_source, x, x->x_sink_id, x->x_sink_chn);
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_packetsize(t_aoo_send *x, t_floatarg f)
{
    x->x_settings.packetsize = f;
    if (x->x_settings.blocksize){
        pthread_mutex_lock(&x->x_mutex);
        aoo_source_setup(x->x_aoo_source, &x->x_settings);
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_resend(t_aoo_send *x, t_floatarg f)
{
    x->x_settings.resend_buffersize = f;
    if (x->x_settings.blocksize){
        pthread_mutex_lock(&x->x_mutex);
        aoo_source_setup(x->x_aoo_source, &x->x_settings);
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_timefilter(t_aoo_send *x, t_floatarg f)
{
    x->x_settings.time_filter_bandwidth = f;
    if (x->x_settings.blocksize){
        pthread_mutex_lock(&x->x_mutex);
        aoo_source_setup(x->x_aoo_source, &x->x_settings);
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_reply(t_aoo_send *x, const char *data, int32_t n)
{
    // called while holding the lock (socket might close or address might change!)
    if (x->x_endpoint){
        if (endpoint_send(x->x_endpoint, data, n) < 0){
            socket_error_print("sendto");
        }
    }
}

void *aoo_send_threadfn(void *y)
{
    t_aoo_send *x = (t_aoo_send *)y;

    pthread_mutex_lock(&x->x_mutex);
    while (x->x_socket >= 0){
        // send all available outgoing packets
        while (aoo_source_send(x->x_aoo_source)) ;
        // check for pending incoming packets
        while (1){
            // receive packet
            char buf[AOO_MAXPACKETSIZE];
            int nbytes = socket_receive(x->x_socket, buf, AOO_MAXPACKETSIZE, 1);
            if (nbytes > 0){
                aoo_source_handlemessage(x->x_aoo_source, buf, nbytes,
                                         x, (aoo_replyfn)aoo_send_reply);
            } else {
                break;
            }
        }
        // wait for more
        pthread_cond_wait(&x->x_cond, &x->x_mutex);
    }
    pthread_mutex_unlock(&x->x_mutex);

    return 0;
}

static void aoo_send_set(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc){
        pthread_mutex_lock(&x->x_mutex);
        // remove old sink
        aoo_source_removeall(x->x_aoo_source);
        // add new sink
        if (argv->a_type == A_SYMBOL){
            if (*argv->a_w.w_symbol->s_name == '*'){
                aoo_source_addsink(x->x_aoo_source, x, AOO_ID_WILDCARD, (aoo_replyfn)aoo_send_reply);
            } else {
                pd_error(x, "%s: bad argument '%s' to 'set' message!",
                         classname(x), argv->a_w.w_symbol->s_name);
                return;
            }
            aoo_source_setsinkchannel(x->x_aoo_source, x, AOO_ID_WILDCARD, x->x_sink_chn);
            x->x_sink_id = AOO_ID_WILDCARD;
        } else {
            int32_t id = atom_getfloat(argv);
            aoo_source_addsink(x->x_aoo_source, x, id, (aoo_replyfn)aoo_send_reply);
            aoo_source_setsinkchannel(x->x_aoo_source, x, id, x->x_sink_chn);
            x->x_sink_id = id;
        }
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_clear(t_aoo_send *x)
{
    pthread_mutex_lock(&x->x_mutex);
    aoo_source_removeall(x->x_aoo_source);
    x->x_sink_id = AOO_ID_NONE;
    pthread_mutex_unlock(&x->x_mutex);
}

static t_int * aoo_send_perform(t_int *w)
{
    t_aoo_send *x = (t_aoo_send *)(w[1]);
    int n = (int)(w[2]);

    assert(sizeof(t_sample) == sizeof(aoo_sample));

    if (x->x_endpoint){
        uint64_t t = aoo_pd_osctime(n, x->x_settings.samplerate);
        if (aoo_source_process(x->x_aoo_source, (const aoo_sample **)x->x_vec, n, t)){
            pthread_cond_signal(&x->x_cond);
        }
        if (aoo_source_eventsavailable(x->x_aoo_source)){
            clock_set(x->x_clock, 0);
        }
    }

    return w + 3;
}

static void aoo_send_dsp(t_aoo_send *x, t_signal **sp)
{
    pthread_mutex_lock(&x->x_mutex);
    x->x_settings.blocksize = sp[0]->s_n;
    x->x_settings.samplerate = sp[0]->s_sr;
    aoo_source_setup(x->x_aoo_source, &x->x_settings);
    pthread_mutex_unlock(&x->x_mutex);

    for (int i = 0; i < x->x_settings.nchannels; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }

    dsp_add(aoo_send_perform, 2, (t_int)x, (t_int)sp[0]->s_n);
}

void aoo_send_disconnect(t_aoo_send *x)
{
    if (x->x_endpoint){
        pthread_mutex_lock(&x->x_mutex);
        endpoint_free(x->x_endpoint);
        x->x_endpoint = 0;
        pthread_mutex_unlock(&x->x_mutex);
    }
}

void aoo_send_connect(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    t_symbol *hostname = atom_getsymbolarg(0, argc, argv);
    int port = atom_getfloatarg(1, argc, argv);

    if (x->x_socket < 0){
        pd_error(x, "%s: can't connect - no socket!", classname(x));
    }

    if (port <= 0){
        pd_error(x, "%s: bad port number %d", classname(x), port);
        return;
    }

    t_endpoint *e = endpoint_new(hostname->s_name, port, x->x_socket);
    if (e){
        pthread_mutex_lock(&x->x_mutex);
        if (x->x_endpoint){
            endpoint_free(x->x_endpoint);
        }
        x->x_endpoint = e; // assign while locked!
        if (x->x_settings.blocksize){
            // force time DLL update
            aoo_source_setup(x->x_aoo_source, &x->x_settings);
        }
        pthread_mutex_unlock(&x->x_mutex);

        t_atom ahost, aport;
        if (endpoint_getaddress(e, &ahost, &aport)){
            post("connected to %s on port %d", ahost.a_w.w_symbol->s_name, port);
        }
    } else {
        pd_error(x, "%s: couldn't resolve hostname '%s'", classname(x), hostname->s_name);
    }
}

static void * aoo_send_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_send *x = (t_aoo_send *)pd_new(aoo_send_class);

    x->x_clock = clock_new(x, (t_method)aoo_send_tick);

    x->x_endpoint = 0;
    x->x_socket = socket_udp();

    pthread_mutex_init(&x->x_mutex, 0);
    pthread_cond_init(&x->x_cond, 0);

    // arg #1: ID
    int src = atom_getfloatarg(0, argc, argv);
    x->x_aoo_source = aoo_source_new(src >= 0 ? src : 0);
    memset(&x->x_settings, 0, sizeof(aoo_source_settings));
    x->x_settings.userdata = x;
    x->x_settings.eventhandler = (aoo_eventhandler)aoo_send_handleevents;
    x->x_settings.buffersize = AOO_SOURCE_DEFBUFSIZE;
    x->x_settings.packetsize = AOO_DEFPACKETSIZE;
    x->x_settings.time_filter_bandwidth = AOO_DLL_BW;
    x->x_settings.resend_buffersize = AOO_RESEND_BUFSIZE;

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

    // make event outlet
    x->x_eventout = outlet_new(&x->x_obj, 0);

    // default format
    aoo_format_storage fmt;
    aoo_defaultformat(&fmt, nchannels);
    aoo_source_setformat(x->x_aoo_source, &fmt.header);

    // create thread
    pthread_create(&x->x_thread, 0, aoo_send_threadfn, x);

    // set sink
    if (x->x_sink_id != AOO_ID_NONE){
        // set sink ID
        t_atom a;
        SETFLOAT(&a, x->x_sink_id);
        aoo_send_set(x, 0, 1, &a);
        aoo_send_channel(x, x->x_sink_chn);
    }

    return x;
}

static void aoo_send_free(t_aoo_send *x)
{
    clock_free(x->x_clock);

    pthread_mutex_lock(&x->x_mutex);
    if (x->x_socket >= 0){
        socket_close(x->x_socket);
    }
    x->x_socket = -1; // sentinel
    pthread_mutex_unlock(&x->x_mutex);

    // notify thread and join
    pthread_cond_signal(&x->x_cond);
    pthread_join(x->x_thread, 0);

    pthread_mutex_destroy(&x->x_mutex);
    pthread_cond_destroy(&x->x_cond);

    aoo_source_free(x->x_aoo_source);

    if (x->x_endpoint){
        endpoint_free(x->x_endpoint);
    }

    freebytes(x->x_vec, sizeof(t_sample *) * x->x_settings.nchannels);
}

void aoo_send_tilde_setup(void)
{
    aoo_send_class = class_new(gensym("aoo_send~"), (t_newmethod)(void *)aoo_send_new,
        (t_method)aoo_send_free, sizeof(t_aoo_send), 0, A_GIMME, A_NULL);
    CLASS_MAINSIGNALIN(aoo_send_class, t_aoo_send, x_f);
    class_addmethod(aoo_send_class, (t_method)aoo_send_dsp, gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_connect, gensym("connect"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_disconnect, gensym("disconnect"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_set, gensym("set"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_format, gensym("format"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_channel, gensym("channel"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_packetsize, gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_resend, gensym("resend"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_clear, gensym("clear"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_timefilter, gensym("timefilter"), A_FLOAT, A_NULL);

    aoo_setup();
}
