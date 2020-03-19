#include "m_pd.h"

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

typedef struct _sink
{
    t_endpoint *s_endpoint;
    int32_t s_id;
} t_sink;

typedef struct _aoo_send
{
    t_object x_obj;
    t_float x_f;
    aoo_source *x_aoo_source;
    aoo_source_settings x_settings;
    t_float **x_vec;
    // sinks
    t_sink *x_sinks;
    int x_numsinks;
    // events
    t_clock *x_clock;
    t_outlet *x_eventout;
    // socket
    int x_quit;
    int x_socket;
    t_endpoint *x_endpoints;
    // threading
    pthread_t x_thread;
    pthread_cond_t x_cond;
    pthread_mutex_t x_mutex;
} t_aoo_send;

int aoo_send_getsinkarg(void *x, int argc, t_atom *argv,
                        struct sockaddr_storage *sa, socklen_t *len, int32_t *id)
{
    if (argc < 3){
        return 0;
    }

    t_symbol *hostname = atom_getsymbol(argv);
    int port = atom_getfloat(argv + 1);

    if (!socket_getaddr(hostname->s_name, port, sa, len)){
        pd_error(x, "%s: couldn't resolve hostname '%s'", classname(x), hostname->s_name);
        return 0;
    }

    if (argv[2].a_type == A_SYMBOL){
        if (*argv[2].a_w.w_symbol->s_name == '*'){
            *id = AOO_ID_WILDCARD;
        } else {
            pd_error(x, "%s: bad ID '%s'!",
                     classname(x), argv[2].a_w.w_symbol->s_name);
            return 0;
        }
    } else {
        *id = atom_getfloat(argv + 2);
    }
    return 1;
}

static void aoo_send_handleevents(t_aoo_send *x,
                                  const aoo_event *events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        if (events[i].type == AOO_PING_EVENT){
            t_endpoint *e = (t_endpoint *)events[i].sink.endpoint;
            t_symbol *host;
            int port;
            if (!endpoint_getaddress(e, &host, &port)){
                continue;
            }
            t_atom msg[3];
            SETSYMBOL(msg, host);
            SETFLOAT(msg + 1, port);
            SETFLOAT(msg + 2, events[i].sink.id);
            outlet_anything(x->x_eventout, gensym("ping"), 3, msg);
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
        aoo_source_setoption(x->x_aoo_source, aoo_opt_format, AOO_ARG(f.header));
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_channel(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    struct sockaddr_storage sa;
    socklen_t len;
    int32_t id;
    if (argc < 4){
        pd_error(x, "%s: too few arguments for 'channel' message", classname(x));
        return;
    }
    if (aoo_send_getsinkarg(x, argc, argv, &sa, &len, &id)){
        t_endpoint *e = endpoint_find(x->x_endpoints, &sa);
        if (!e){
            pd_error(x, "%s: couldn't find sink!", classname(x));
            return;
        }
        int32_t chn = atom_getfloat(argv + 3);

        pthread_mutex_lock(&x->x_mutex);
        aoo_source_setsinkoption(x->x_aoo_source, e, id,
                                 aoo_opt_channelonset, AOO_ARG(chn));
        pthread_mutex_unlock(&x->x_mutex);
    }
}

static void aoo_send_packetsize(t_aoo_send *x, t_floatarg f)
{
    pthread_mutex_lock(&x->x_mutex);
    int32_t packetsize = f;
    aoo_source_setoption(x->x_aoo_source, aoo_opt_packetsize, AOO_ARG(packetsize));
    pthread_mutex_unlock(&x->x_mutex);
}

static void aoo_send_resend(t_aoo_send *x, t_floatarg f)
{
    pthread_mutex_lock(&x->x_mutex);
    int32_t bufsize = f;
    aoo_source_setoption(x->x_aoo_source, aoo_opt_resend_buffersize, AOO_ARG(bufsize));
    pthread_mutex_unlock(&x->x_mutex);
}

static void aoo_send_timefilter(t_aoo_send *x, t_floatarg f)
{
    pthread_mutex_lock(&x->x_mutex);
    float bandwidth;
    aoo_source_setoption(x->x_aoo_source, aoo_opt_timefilter_bandwidth, AOO_ARG(bandwidth));
    pthread_mutex_unlock(&x->x_mutex);
}

void *aoo_send_threadfn(void *y)
{
    t_aoo_send *x = (t_aoo_send *)y;

    pthread_mutex_lock(&x->x_mutex);
    while (!x->x_quit){
        // send all available outgoing packets
        while (aoo_source_send(x->x_aoo_source)) ;
        // check for pending incoming packets
        while (1){
            // receive packet
            char buf[AOO_MAXPACKETSIZE];
            struct sockaddr_storage sa;
            socklen_t len;
            int nbytes = socket_receive(x->x_socket, buf, AOO_MAXPACKETSIZE, &sa, &len, 1);
            if (nbytes > 0){
                t_endpoint * e = endpoint_find(x->x_endpoints, &sa);
                if (e){
                    aoo_source_handlemessage(x->x_aoo_source, buf, nbytes,
                                                     e, (aoo_replyfn)endpoint_send);
                } else {
                    fprintf(stderr, "aoo_send~: received message from unknown endpoint!");
                    fflush(stderr);
                }
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

void aoo_send_add(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (x->x_socket < 0){
        pd_error(x, "%s: can't add sink - no socket!", classname(x));
    }

    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'add' message", classname(x));
        return;
    }

    struct sockaddr_storage sa;
    socklen_t len;
    int32_t id;
    if (aoo_send_getsinkarg(x, argc, argv, &sa, &len, &id)){
        t_endpoint *e = endpoint_find(x->x_endpoints, &sa);
        if (e){
            // check if sink exists
            for (int i = 0; i < x->x_numsinks; ++i){
                if ((x->x_sinks[i].s_endpoint == e) &&
                    (x->x_sinks[i].s_id == id))
                {
                    t_symbol *host;
                    int port;
                    if (endpoint_getaddress(e, &host, &port)){
                        pd_error(x, "%s: sink %s %d %d already added!",
                                 classname(x), host->s_name, port, id);
                        return;
                    }
                }
            }
        }

        // enter critical section
        pthread_mutex_lock(&x->x_mutex);
        if (!e){
            // add endpoint
            e = endpoint_new(x->x_socket, &sa, len);

            if (x->x_endpoints){
                e->next = x->x_endpoints;
                x->x_endpoints = e;
            } else {
                x->x_endpoints = e;
            }
        }

        aoo_source_addsink(x->x_aoo_source, e, id, (aoo_replyfn)endpoint_send);

        if (argc > 3){
            int32_t chn = atom_getfloat(argv + 3);
            aoo_source_setsinkoption(x->x_aoo_source, e, id,
                                     aoo_opt_channelonset, AOO_ARG(chn));
        }
        // leave critical section
        pthread_mutex_unlock(&x->x_mutex);

        // add sink to list
        int oldsize = x->x_numsinks;
        if (oldsize){
            x->x_sinks = (t_sink *)resizebytes(x->x_sinks,
                oldsize * sizeof(t_sink), (oldsize + 1) * sizeof(t_sink));
        } else {
            x->x_sinks = (t_sink *)getbytes(sizeof(t_sink));
        }
        t_sink *sink = &x->x_sinks[oldsize];
        sink->s_endpoint = e;
        sink->s_id = id;
        x->x_numsinks++;

        // print message
        t_symbol *host;
        int port;
        if (endpoint_getaddress(e, &host, &port)){
            verbose(0, "added sink %s %d %d", host->s_name, port, id);
        }
    }
}

static void aoo_send_remove(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'remove' message", classname(x));
        return;
    }

    struct sockaddr_storage sa;
    socklen_t len;
    int32_t id;
    if (aoo_send_getsinkarg(x, argc, argv, &sa, &len, &id)){
        t_endpoint *e = endpoint_find(x->x_endpoints, &sa);
        t_sink *sink = 0;
        if (e){
            // check if sink exists
            for (int i = 0; i < x->x_numsinks; ++i){
                if ((x->x_sinks[i].s_endpoint == e) &&
                    (x->x_sinks[i].s_id == id))
                {
                    sink = &x->x_sinks[i];
                }
            }
        }
        if (!sink){
            t_symbol *host = atom_getsymbol(argv);
            int port = atom_getfloat(argv + 1);
            pd_error(x, "%s: couldn't find sink %s %d %d!",
                     classname(x), host->s_name, port, id);
            return;
        }

        pthread_mutex_lock(&x->x_mutex);
        aoo_source_removesink(x->x_aoo_source, e, id);
        pthread_mutex_unlock(&x->x_mutex);

        // remove from list
        int oldsize = x->x_numsinks;
        if (oldsize > 1){
            memmove(sink, sink + 1,
                    (x->x_sinks + oldsize - (sink + 1)) * sizeof(t_sink));
            x->x_sinks = (t_sink *)resizebytes(x->x_sinks,
                oldsize * sizeof(t_sink), (oldsize - 1) * sizeof(t_sink));
        } else {
            freebytes(x->x_sinks, sizeof(t_sink));
            x->x_sinks = 0;
        }
        x->x_numsinks--;

        // print message
        t_symbol *host;
        int port;
        if (endpoint_getaddress(e, &host, &port)){
            verbose(0, "removed sink %s %d %d", host->s_name, port, id);
        }
    }
}

static void aoo_send_clear(t_aoo_send *x)
{
    pthread_mutex_lock(&x->x_mutex);
    aoo_source_removeall(x->x_aoo_source);
    pthread_mutex_unlock(&x->x_mutex);

    // clear sink list
    if (x->x_numsinks){
        freebytes(x->x_sinks, x->x_numsinks * sizeof(t_sink));
        x->x_numsinks = 0;
    }
}

static void aoo_send_listsinks(t_aoo_send *x)
{
    for (int i = 0; i < x->x_numsinks; ++i){
        t_sink *s = &x->x_sinks[i];
        t_symbol *host;
        int port;
        if (endpoint_getaddress(s->s_endpoint, &host, &port)){
            t_atom msg[3];
            SETSYMBOL(msg, host);
            SETFLOAT(msg + 1, port);
            SETFLOAT(msg + 2, s->s_id);
            outlet_anything(x->x_eventout, gensym("sink"), 3, msg);
        } else {
            pd_error(x, "%s: couldn't get endpoint address for sink", classname(x));
        }
    }
}

static t_int * aoo_send_perform(t_int *w)
{
    t_aoo_send *x = (t_aoo_send *)(w[1]);
    int n = (int)(w[2]);

    assert(sizeof(t_sample) == sizeof(aoo_sample));

    uint64_t t = aoo_pd_osctime(n, x->x_settings.samplerate);
    if (aoo_source_process(x->x_aoo_source, (const aoo_sample **)x->x_vec, n, t) > 0){
        pthread_cond_signal(&x->x_cond);
    }
    if (aoo_source_eventsavailable(x->x_aoo_source) > 0){
        clock_set(x->x_clock, 0);
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


static void * aoo_send_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_send *x = (t_aoo_send *)pd_new(aoo_send_class);

    x->x_clock = clock_new(x, (t_method)aoo_send_tick);
    x->x_endpoints = 0;
    x->x_sinks = 0;
    x->x_numsinks = 0;
    x->x_socket = socket_udp();
    if (x->x_socket < 0){
        pd_error(x, "%s: couldn't create socket", classname(x));
    }
    pthread_mutex_init(&x->x_mutex, 0);
    pthread_cond_init(&x->x_cond, 0);

    // arg #1: ID
    int src = atom_getfloatarg(0, argc, argv);
    x->x_aoo_source = aoo_source_new(src >= 0 ? src : 0);

    memset(&x->x_settings, 0, sizeof(aoo_source_settings));
    x->x_settings.userdata = x;
    x->x_settings.eventhandler = (aoo_eventhandler)aoo_send_handleevents;

    // arg #2: num channels
    int nchannels = atom_getfloatarg(1, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x->x_settings.nchannels = nchannels;

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
    aoo_source_setoption(x->x_aoo_source, aoo_opt_format, AOO_ARG(fmt.header));

    // create thread
    x->x_quit = 0;
    if (x->x_socket >= 0){
        pthread_create(&x->x_thread, 0, aoo_send_threadfn, x);
    } else {
        x->x_thread = 0;
    }
    return x;
}

static void aoo_send_free(t_aoo_send *x)
{
    // notify thread and join
    if (x->x_thread){
        x->x_quit = 1;
        pthread_cond_signal(&x->x_cond);
        pthread_join(x->x_thread, 0);
    }

    aoo_source_free(x->x_aoo_source);

    pthread_mutex_destroy(&x->x_mutex);
    pthread_cond_destroy(&x->x_cond);

    if (x->x_socket >= 0){
        socket_close(x->x_socket);
    }

    t_endpoint *e = x->x_endpoints;
    while (e){
        t_endpoint *next = e->next;
        endpoint_free(e);
        e = next;
    }

    freebytes(x->x_vec, sizeof(t_sample *) * x->x_settings.nchannels);
    if (x->x_sinks){
        freebytes(x->x_sinks, x->x_numsinks * sizeof(t_sink));
    }

    clock_free(x->x_clock);
}

EXPORT void aoo_send_tilde_setup(void)
{
    aoo_send_class = class_new(gensym("aoo_send~"), (t_newmethod)(void *)aoo_send_new,
        (t_method)aoo_send_free, sizeof(t_aoo_send), 0, A_GIMME, A_NULL);
    CLASS_MAINSIGNALIN(aoo_send_class, t_aoo_send, x_f);
    class_addmethod(aoo_send_class, (t_method)aoo_send_dsp, gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_add, gensym("add"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_remove, gensym("remove"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_clear, gensym("clear"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_format, gensym("format"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_channel, gensym("channel"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_packetsize, gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_resend, gensym("resend"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_timefilter, gensym("timefilter"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_listsinks, gensym("list_sinks"), A_NULL);

    aoo_setup();
}
