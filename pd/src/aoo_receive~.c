#include "m_pd.h"

#include "aoo_common.h"

#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <inttypes.h>

#define DEFBUFSIZE 25

/*///////////////////// aoo_receive~ ////////////////////*/

t_class *aoo_receive_class;

typedef struct _source
{
    t_endpoint *s_endpoint;
    int32_t s_id;
} t_source;

typedef struct _aoo_receive
{
    t_object x_obj;
    t_float x_f;
    aoo_sink *x_aoo_sink;
    int32_t x_samplerate;
    int32_t x_blocksize;
    int32_t x_nchannels;
    int32_t x_id;
    t_sample **x_vec;
    // sinks
    t_source *x_sources;
    int x_numsources;
    t_aoo_server * x_listener;
    pthread_mutex_t x_mutex;
    // events
    t_outlet *x_eventout;
    t_clock *x_clock;
} t_aoo_receive;

static t_source * aoo_receive_findsource(t_aoo_receive *x, int argc, t_atom *argv)
{
    struct sockaddr_storage sa;
    socklen_t len;
    int32_t id;
    if (aoo_getsourcearg(x, argc, argv, &sa, &len, &id)){
        for (int i = 0; i < x->x_numsources; ++i){
            if (endpoint_match(x->x_sources[i].s_endpoint, &sa) &&
                x->x_sources[i].s_id == id)
            {
                return &x->x_sources[i];
            }
        }
        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        pd_error(x, "%s: couldn't find source %s %d %d",
                 classname(x), host->s_name, port, id);
    }
    return 0;
}

// called from the network thread
void aoo_receive_handle_message(t_aoo_receive *x, const char * data,
                                       int32_t n, void *src, aoo_replyfn fn)
{
    // synchronize with aoo_receive_dsp()
    pthread_mutex_lock(&x->x_mutex);
    // handle incoming message
    aoo_sink_handlemessage(x->x_aoo_sink, data, n, src, fn);
    // send outgoing messages
    while (aoo_sink_send(x->x_aoo_sink)) ;
    pthread_mutex_unlock(&x->x_mutex);
}

static void aoo_receive_buffersize(t_aoo_receive *x, t_floatarg f)
{
    int32_t bufsize = f;
    aoo_sink_setoption(x->x_aoo_sink, aoo_opt_buffersize, AOO_ARG(bufsize));
}

static void aoo_receive_timefilter(t_aoo_receive *x, t_floatarg f)
{
    float bandwidth = f;
    aoo_sink_setoption(x->x_aoo_sink, aoo_opt_timefilter_bandwidth, AOO_ARG(bandwidth));
}

static void aoo_receive_packetsize(t_aoo_receive *x, t_floatarg f)
{
    int32_t packetsize = f;
    aoo_sink_setoption(x->x_aoo_sink, aoo_opt_packetsize, AOO_ARG(packetsize));
}

static void aoo_receive_ping(t_aoo_receive *x, t_floatarg f)
{
    int32_t interval = f;
    aoo_sink_setoption(x->x_aoo_sink, aoo_opt_ping_interval, AOO_ARG(interval));
}

static void aoo_receive_reset(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc){
        // reset specific source
        t_source *source = aoo_receive_findsource(x, argc, argv);
        if (source){
            aoo_sink_setsourceoption(x->x_aoo_sink, source->s_endpoint,
                                     source->s_id, aoo_opt_reset, AOO_ARG_NULL);
        }
    } else {
        // reset all sources
        aoo_sink_setoption(x->x_aoo_sink, aoo_opt_reset, AOO_ARG_NULL);
    }
}

static void aoo_receive_resend(t_aoo_receive *x, t_symbol *s, int argc, t_atom *argv)
{
    int32_t limit, interval, maxnumframes;
    if (!aoo_parseresend(x, argc, argv, &limit, &interval, &maxnumframes)){
        return;
    }
    aoo_sink_setoption(x->x_aoo_sink, aoo_opt_resend_limit, AOO_ARG(limit));
    aoo_sink_setoption(x->x_aoo_sink, aoo_opt_resend_interval, AOO_ARG(interval));
    aoo_sink_setoption(x->x_aoo_sink, aoo_opt_resend_maxnumframes, AOO_ARG(maxnumframes));
}

static void aoo_receive_listsources(t_aoo_receive *x)
{
    for (int i = 0; i < x->x_numsources; ++i){
        t_source *s = &x->x_sources[i];
        t_symbol *host;
        int port;
        if (endpoint_getaddress(s->s_endpoint, &host, &port)){
            t_atom msg[3];
            SETSYMBOL(msg, host);
            SETFLOAT(msg + 1, port);
            SETFLOAT(msg + 2, s->s_id);
            outlet_anything(x->x_eventout, gensym("source"), 3, msg);
        } else {
            pd_error(x, "%s: couldn't get endpoint address for source", classname(x));
        }
    }
}

static void aoo_receive_listen(t_aoo_receive *x, t_floatarg f)
{
    int port = f;
    if (x->x_listener){
        if (aoo_server_port(x->x_listener) == port){
            return;
        }
        // release old listener
        aoo_server_release(x->x_listener, (t_pd *)x, x->x_id);
    }
    // add new listener
    if (port){
        x->x_listener = aoo_server_add((t_pd *)x, x->x_id, port);
        if (x->x_listener){
            post("listening on port %d", aoo_server_port(x->x_listener));
        }
    } else {
        // stop listening
        x->x_listener = 0;
    }
}

static int32_t aoo_sourceevent_to_atoms(const aoo_source_event *e, t_atom *argv)
{
    t_endpoint *c = (t_endpoint *)e->endpoint;
    t_symbol *host;
    int port;
    if (endpoint_getaddress(c, &host, &port)){
        SETSYMBOL(argv, host);
        SETFLOAT(argv + 1, port);
        SETFLOAT(argv + 2, e->id);
        return 1;
    }
    return 0;
}

static void aoo_receive_handleevents(t_aoo_receive *x,
                                     const aoo_event *events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        t_atom msg[32];
        switch (events[i].type){
        case AOO_SOURCE_ADD_EVENT:
        {
            const aoo_source_event *e = &events[i].source;

            // first add to source list
            int oldsize = x->x_numsources;
            if (oldsize){
                x->x_sources = (t_source *)resizebytes(x->x_sources,
                    oldsize * sizeof(t_source), (oldsize + 1) * sizeof(t_source));
            } else {
                x->x_sources = (t_source *)getbytes(sizeof(t_source));
            }
            t_source *s = &x->x_sources[oldsize];
            s->s_endpoint = (t_endpoint *)e->endpoint;
            s->s_id = e->id;
            x->x_numsources++;

            // output event
            if (!aoo_sourceevent_to_atoms(e, msg)){
                continue;
            }
            outlet_anything(x->x_eventout, gensym("source_add"), 3, msg);
            break;
        }
        case AOO_SOURCE_FORMAT_EVENT:
        {
            const aoo_source_event *e = &events[i].source;
            if (!aoo_sourceevent_to_atoms(e, msg)){
                continue;
            }
            aoo_format_storage f;
            int success = aoo_sink_getsourceoption(x->x_aoo_sink, e->endpoint, e->id,
                                                   aoo_opt_format, AOO_ARG(f));
            if (success){
                int fsize = aoo_printformat(&f, 29, msg + 3); // skip first three atoms
                outlet_anything(x->x_eventout, gensym("source_format"), fsize + 3, msg);
            }
            break;
        }
        case AOO_SOURCE_STATE_EVENT:
        {
            const aoo_source_state_event *e = &events[i].source_state;
            if (!aoo_sourceevent_to_atoms(&e->source, msg)){
                continue;
            }
            SETFLOAT(&msg[3], e->state);
            outlet_anything(x->x_eventout, gensym("source_state"), 4, msg);
            break;
        }
        case AOO_BLOCK_LOST_EVENT:
        {
            const aoo_block_loss_event *e = &events[i].block_loss;
            if (!aoo_sourceevent_to_atoms(&e->source, msg)){
                continue;
            }
            SETFLOAT(&msg[3], e->count);
            outlet_anything(x->x_eventout, gensym("block_lost"), 4, msg);
            break;
        }
        case AOO_BLOCK_REORDERED_EVENT:
        {
            const aoo_block_reorder_event *e = &events[i].block_reorder;
            if (!aoo_sourceevent_to_atoms(&e->source, msg)){
                continue;
            }
            SETFLOAT(&msg[3], e->count);
            outlet_anything(x->x_eventout, gensym("block_reordered"), 4, msg);
            break;
        }
        case AOO_BLOCK_RESENT_EVENT:
        {
            const aoo_block_resend_event *e = &events[i].block_resend;
            if (!aoo_sourceevent_to_atoms(&e->source, msg)){
                continue;
            }
            SETFLOAT(&msg[3], e->count);
            outlet_anything(x->x_eventout, gensym("block_resent"), 4, msg);
            break;
        }
        case AOO_BLOCK_GAP_EVENT:
        {
            const aoo_block_gap_event *e = &events[i].block_gap;
            if (!aoo_sourceevent_to_atoms(&e->source, msg)){
                continue;
            }
            SETFLOAT(&msg[3], e->count);
            outlet_anything(x->x_eventout, gensym("block_gap"), 4, msg);
            break;
        }
        default:
            break;
        }
    }
}

static void aoo_receive_tick(t_aoo_receive *x)
{
    aoo_sink_handleevents(x->x_aoo_sink,
                          (aoo_eventhandler)aoo_receive_handleevents, x);
}

static t_int * aoo_receive_perform(t_int *w)
{
    t_aoo_receive *x = (t_aoo_receive *)(w[1]);
    int n = (int)(w[2]);

    uint64_t t = aoo_osctime_get();
    if (aoo_sink_process(x->x_aoo_sink, x->x_vec, n, t) <= 0){
        // output zeros
        for (int i = 0; i < x->x_nchannels; ++i){
            memset(x->x_vec[i], 0, sizeof(t_float) * n);
        }
    }

    // handle events
    if (aoo_sink_eventsavailable(x->x_aoo_sink) > 0){
        clock_delay(x->x_clock, 0);
    }

    return w + 3;
}

static void aoo_receive_dsp(t_aoo_receive *x, t_signal **sp)
{
    x->x_blocksize = (int)sp[0]->s_n;
    x->x_samplerate = sp[0]->s_sr;

    for (int i = 0; i < x->x_nchannels; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }

    // synchronize with aoo_receive_handle_message()
    pthread_mutex_lock(&x->x_mutex);

    aoo_sink_setup(x->x_aoo_sink, x->x_samplerate,
                   x->x_blocksize, x->x_nchannels);

    pthread_mutex_unlock(&x->x_mutex);

    dsp_add(aoo_receive_perform, 2, (t_int)x, (t_int)x->x_blocksize);
}

static void * aoo_receive_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_receive *x = (t_aoo_receive *)pd_new(aoo_receive_class);

    x->x_f = 0;
    x->x_listener = 0;
    x->x_sources = 0;
    x->x_numsources = 0;
    x->x_clock = clock_new(x, (t_method)aoo_receive_tick);
    pthread_mutex_init(&x->x_mutex, 0);

    // arg #1: ID
    int id = atom_getfloatarg(0, argc, argv);
    x->x_id = id >= 0 ? id : 0;
    x->x_aoo_sink = aoo_sink_new(x->x_id);

    // arg #2: num channels
    int nchannels = atom_getfloatarg(1, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x->x_nchannels = nchannels;
    x->x_blocksize = 0;
    x->x_samplerate = 0;

    // arg #3: port number
    if (argc > 2){
        aoo_receive_listen(x, atom_getfloat(argv + 2));
    }

    // arg #4: buffer size (ms)
    aoo_receive_buffersize(x, argc > 3 ? atom_getfloat(argv + 3) : DEFBUFSIZE);

    // make signal outlets
    for (int i = 0; i < nchannels; ++i){
        outlet_new(&x->x_obj, &s_signal);
    }
    x->x_vec = (t_sample **)getbytes(sizeof(t_sample *) * nchannels);

    // event outlet
    x->x_eventout = outlet_new(&x->x_obj, 0);

    return x;
}

static void aoo_receive_free(t_aoo_receive *x)
{
    if (x->x_listener){
        aoo_server_release(x->x_listener, (t_pd *)x, x->x_id);
    }

    aoo_sink_free(x->x_aoo_sink);

    pthread_mutex_destroy(&x->x_mutex);

    freebytes(x->x_vec, sizeof(t_sample *) * x->x_nchannels);
    if (x->x_sources){
        freebytes(x->x_sources, x->x_numsources * sizeof(t_source));
    }

    clock_free(x->x_clock);
}

void aoo_receive_tilde_setup(void)
{
    aoo_receive_class = class_new(gensym("aoo_receive~"), (t_newmethod)(void *)aoo_receive_new,
        (t_method)aoo_receive_free, sizeof(t_aoo_receive), 0, A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_dsp, gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_listen, gensym("listen"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_buffersize,
                    gensym("bufsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_timefilter,
                    gensym("timefilter"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_packetsize,
                    gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_resend,
                    gensym("resend"), A_GIMME, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_ping,
                    gensym("ping"), A_FLOAT, A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_listsources,
                    gensym("list_sources"), A_NULL);
    class_addmethod(aoo_receive_class, (t_method)aoo_receive_reset,
                    gensym("reset"), A_GIMME, A_NULL);
}
