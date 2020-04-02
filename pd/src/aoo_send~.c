/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "m_pd.h"

#include "aoo_common.h"

#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

// for hardware buffer sizes up to 1024 @ 44.1 kHz
#define DEFBUFSIZE 25

t_class *aoo_send_class;

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
    int32_t x_samplerate;
    int32_t x_blocksize;
    int32_t x_nchannels;
    int32_t x_id;
    t_float **x_vec;
    // sinks
    t_sink *x_sinks;
    int x_numsinks;
    // server
    t_aoo_server *x_server;
    aoo_lock x_lock;
    // events
    t_clock *x_clock;
    t_outlet *x_eventout;
} t_aoo_send;

// called from the network receive thread
void aoo_send_handle_message(t_aoo_send *x, const char * data,
                                int32_t n, void *src, aoo_replyfn fn)
{
    // synchronize with aoo_receive_dsp()
    aoo_lock_lock_shared(&x->x_lock);
    // handle incoming message
    aoo_source_handlemessage(x->x_aoo_source, data, n, src, fn);
    aoo_lock_unlock_shared(&x->x_lock);
}

// called from the network send thread
void aoo_send_send(t_aoo_send *x)
{
    // synchronize with aoo_receive_dsp()
    aoo_lock_lock_shared(&x->x_lock);
    // send outgoing messages
    while (aoo_source_send(x->x_aoo_source)) ;
    aoo_lock_unlock_shared(&x->x_lock);
}

static void aoo_send_handleevents(t_aoo_send *x,
                                  const aoo_event *events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        switch (events[i].type){
        case AOO_PING_EVENT:
        {
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
            break;
        }
        case AOO_INVITE_EVENT:
        {
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
            outlet_anything(x->x_eventout, gensym("invite"), 3, msg);
            break;
        }
        case AOO_UNINVITE_EVENT:
        {
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
            outlet_anything(x->x_eventout, gensym("uninvite"), 3, msg);
            break;
        }
        default:
            break;
        }
    }
}

static void aoo_send_tick(t_aoo_send *x)
{
    aoo_source_handleevents(x->x_aoo_source,
                            (aoo_eventhandler)aoo_send_handleevents, x);
}

static void aoo_send_format(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    aoo_format_storage f;
    f.header.nchannels = x->x_nchannels;
    if (aoo_parseformat(x, &f, argc, argv)){
        aoo_source_setoption(x->x_aoo_source, aoo_opt_format, AOO_ARG(f.header));
    }
}

static t_sink *aoo_send_findsink(t_aoo_send *x,
                                 const struct sockaddr_storage *sa, int32_t id)
{
    for (int i = 0; i < x->x_numsinks; ++i){
        if (x->x_sinks[i].s_id == id &&
            endpoint_match(x->x_sinks[i].s_endpoint, sa))
        {
            return &x->x_sinks[i];
        }
    }
    return 0;
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
    if (aoo_getsinkarg(x, argc, argv, &sa, &len, &id)){
        t_sink *sink = aoo_send_findsink(x, &sa, id);
        if (!sink){
            pd_error(x, "%s: couldn't find sink!", classname(x));
            return;
        }
        int32_t chn = atom_getfloat(argv + 3);

        aoo_source_setsinkoption(x->x_aoo_source, sink->s_endpoint, sink->s_id,
                                 aoo_opt_channelonset, AOO_ARG(chn));
    }
}

static void aoo_send_packetsize(t_aoo_send *x, t_floatarg f)
{
    int32_t packetsize = f;
    aoo_source_setoption(x->x_aoo_source, aoo_opt_packetsize, AOO_ARG(packetsize));
}

static void aoo_send_resend(t_aoo_send *x, t_floatarg f)
{
    int32_t bufsize = f;
    aoo_source_setoption(x->x_aoo_source, aoo_opt_resend_buffersize, AOO_ARG(bufsize));
}

static void aoo_send_timefilter(t_aoo_send *x, t_floatarg f)
{
    float bandwidth;
    aoo_source_setoption(x->x_aoo_source, aoo_opt_timefilter_bandwidth, AOO_ARG(bandwidth));
}

static void aoo_send_doremovesink(t_aoo_send *x, t_endpoint *e, int32_t id)
{
    int n = x->x_numsinks;
    if (n > 1){
        if (id == AOO_ID_WILDCARD){
            // remove all sinks matching endpoint
            t_sink *end = x->x_sinks + n;
            for (t_sink *s = x->x_sinks; s != end; ){
                if (s->s_endpoint == e){
                    memmove(s, s + 1,
                            (end - s - 1) * sizeof(t_sink));
                    end--;
                } else {
                    s++;
                }
            }
            int newsize = end - x->x_sinks;
            x->x_sinks = (t_sink *)resizebytes(x->x_sinks,
                n * sizeof(t_sink), newsize * sizeof(t_sink));
            x->x_numsinks = newsize;
            return;
        } else {
            // remove the sink matching endpoint and id
            for (int i = 0; i < n; ++i){
                if ((x->x_sinks[i].s_endpoint == e) &&
                    (x->x_sinks[i].s_id == id))
                {
                    memmove(&x->x_sinks[i], &x->x_sinks[i + 1],
                            (n - i - 1) * sizeof(t_sink));
                    x->x_sinks = (t_sink *)resizebytes(x->x_sinks,
                        n * sizeof(t_sink), (n - 1) * sizeof(t_sink));
                    x->x_numsinks--;
                    return;
                }
            }
        }
    } else if (n == 1) {
        if ((x->x_sinks->s_endpoint == e) &&
            (id == AOO_ID_WILDCARD || id == x->x_sinks->s_id))
        {
            freebytes(x->x_sinks, sizeof(t_sink));
            x->x_sinks = 0;
            x->x_numsinks = 0;
            return;
        }
    }
    if (id != AOO_ID_WILDCARD){
        bug("aoo_send_doremovesink");
    }
}

static void aoo_send_add(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->x_server){
        pd_error(x, "%s: can't add sink - no server!", classname(x));
    }

    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'add' message", classname(x));
        return;
    }

    struct sockaddr_storage sa;
    socklen_t len;
    int32_t id;
    if (aoo_getsinkarg(x, argc, argv, &sa, &len, &id)){
        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        t_endpoint *e = aoo_server_getendpoint(x->x_server, &sa, len);
        // check if sink exists
        if (id != AOO_ID_WILDCARD){
            for (int i = 0; i < x->x_numsinks; ++i){
                if (x->x_sinks[i].s_endpoint == e){
                    if (x->x_sinks[i].s_id == AOO_ID_WILDCARD){
                        pd_error(x, "%s: sink %s %d %d already added via wildcard!",
                                 classname(x), host->s_name, port, id);
                        return;
                    } else if (x->x_sinks[i].s_id == id){
                        pd_error(x, "%s: sink %s %d %d already added!",
                                 classname(x), host->s_name, port, id);
                        return;
                    }
                }
            }
        }

        aoo_source_addsink(x->x_aoo_source, e, id, (aoo_replyfn)endpoint_send);

        if (argc > 3){
            int32_t chn = atom_getfloat(argv + 3);
            aoo_source_setsinkoption(x->x_aoo_source, e, id,
                                     aoo_opt_channelonset, AOO_ARG(chn));
        }

        if (id == AOO_ID_WILDCARD){
            // first remove all sinks on this endpoint
            aoo_send_doremovesink(x, e, AOO_ID_WILDCARD);
        }
        // add sink to list
        int n = x->x_numsinks;
        if (n){
            x->x_sinks = (t_sink *)resizebytes(x->x_sinks,
                n * sizeof(t_sink), (n + 1) * sizeof(t_sink));
        } else {
            x->x_sinks = (t_sink *)getbytes(sizeof(t_sink));
        }
        t_sink *sink = &x->x_sinks[n];
        sink->s_endpoint = e;
        sink->s_id = id;
        x->x_numsinks++;

        // print message (use actual hostname)
        if (endpoint_getaddress(e, &host, &port)){
            if (id == AOO_ID_WILDCARD){
                verbose(0, "added all sinks on %s %d", host->s_name, port);
            } else {
                verbose(0, "added sink %s %d %d", host->s_name, port, id);
            }
        }
    }
}

static void aoo_send_remove(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->x_server){
        pd_error(x, "%s: can't remove sink - no server!", classname(x));
    }

    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'remove' message", classname(x));
        return;
    }

    struct sockaddr_storage sa;
    socklen_t len;
    int32_t id;
    if (aoo_getsinkarg(x, argc, argv, &sa, &len, &id)){
        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        t_endpoint *e = 0;
        if (id != AOO_ID_WILDCARD){
            // check if sink exists
            for (int i = 0; i < x->x_numsinks; ++i){
                t_sink *sink = &x->x_sinks[i];
                if (endpoint_match(sink->s_endpoint, &sa)){
                    if (sink->s_id == AOO_ID_WILDCARD){
                        pd_error(x, "%s: can't remove sink %s %d %d because of wildcard!",
                                 classname(x), host->s_name, port, id);
                        return;
                    } else if (sink->s_id == id) {
                        e = sink->s_endpoint;
                        break;
                    }
                }
            }
        } else {
            e = aoo_server_getendpoint(x->x_server, &sa, len);
        }

        if (!e){
            pd_error(x, "%s: couldn't find sink %s %d %d!",
                     classname(x), host->s_name, port, id);
            return;
        }

        aoo_source_removesink(x->x_aoo_source, e, id);

        // remove from list
        aoo_send_doremovesink(x, e, id);

        // print message (use actual hostname)
        if (endpoint_getaddress(e, &host, &port)){
            if (id == AOO_ID_WILDCARD){
                verbose(0, "removed all sinks on %s %d", host->s_name, port);
            } else {
                verbose(0, "removed sink %s %d %d", host->s_name, port, id);
            }
        }
    }
}

static void aoo_send_clear(t_aoo_send *x)
{
    aoo_source_removeall(x->x_aoo_source);

    // clear sink list
    if (x->x_numsinks){
        freebytes(x->x_sinks, x->x_numsinks * sizeof(t_sink));
        x->x_numsinks = 0;
    }
}

static void aoo_send_start(t_aoo_send *x)
{
    aoo_source_setoption(x->x_aoo_source, aoo_opt_resume, AOO_ARG_NULL);
}

static void aoo_send_stop(t_aoo_send *x)
{
    aoo_source_setoption(x->x_aoo_source, aoo_opt_stop, AOO_ARG_NULL);
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
            if (s->s_id == AOO_ID_WILDCARD){
                SETSYMBOL(msg + 2, gensym("*"));
            } else {
                SETFLOAT(msg + 2, s->s_id);
            }
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

    uint64_t t = aoo_osctime_get();
    if (aoo_source_process(x->x_aoo_source, (const aoo_sample **)x->x_vec, n, t) > 0){
        if (x->x_server){
            aoo_server_notify(x->x_server);
        }
    }
    if (aoo_source_eventsavailable(x->x_aoo_source) > 0){
        clock_set(x->x_clock, 0);
    }

    return w + 3;
}

static void aoo_send_dsp(t_aoo_send *x, t_signal **sp)
{
    x->x_blocksize = sp[0]->s_n;
    x->x_samplerate = sp[0]->s_sr;

    for (int i = 0; i < x->x_nchannels; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }

    // synchronize with network threads!
    aoo_lock_lock(&x->x_lock); // writer lock!

    aoo_source_setup(x->x_aoo_source, x->x_samplerate,
                     x->x_blocksize, x->x_nchannels);

    aoo_lock_unlock(&x->x_lock);

    dsp_add(aoo_send_perform, 2, (t_int)x, (t_int)x->x_blocksize);
}


static void * aoo_send_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_send *x = (t_aoo_send *)pd_new(aoo_send_class);

    x->x_clock = clock_new(x, (t_method)aoo_send_tick);
    x->x_sinks = 0;
    x->x_numsinks = 0;
    aoo_lock_init(&x->x_lock);

    // arg #1: port number
    int port = atom_getfloatarg(0, argc, argv);

    // arg #2: ID
    int id = atom_getfloatarg(1, argc, argv);
    x->x_id = id > 0 ? id : 0;
    x->x_aoo_source = aoo_source_new(x->x_id);
    x->x_server = port ? aoo_server_addclient((t_pd *)x, x->x_id, port) : 0;

    // arg #3: num channels
    int nchannels = atom_getfloatarg(2, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x->x_nchannels = nchannels;
    x->x_blocksize = 0;
    x->x_samplerate = 0;

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

    return x;
}

static void aoo_send_free(t_aoo_send *x)
{
    if (x->x_server){
        aoo_server_removeclient(x->x_server, (t_pd *)x, x->x_id);
    }

    aoo_source_free(x->x_aoo_source);

    aoo_lock_destroy(&x->x_lock);

    freebytes(x->x_vec, sizeof(t_sample *) * x->x_nchannels);
    if (x->x_sinks){
        freebytes(x->x_sinks, x->x_numsinks * sizeof(t_sink));
    }

    clock_free(x->x_clock);
}

void aoo_send_tilde_setup(void)
{
    aoo_send_class = class_new(gensym("aoo_send~"), (t_newmethod)(void *)aoo_send_new,
        (t_method)aoo_send_free, sizeof(t_aoo_send), 0, A_GIMME, A_NULL);
    CLASS_MAINSIGNALIN(aoo_send_class, t_aoo_send, x_f);
    class_addmethod(aoo_send_class, (t_method)aoo_send_dsp, gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_add, gensym("add"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_remove, gensym("remove"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_clear, gensym("clear"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_start, gensym("start"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_stop, gensym("stop"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_format, gensym("format"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_channel, gensym("channel"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_packetsize, gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_resend, gensym("resend"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_timefilter, gensym("timefilter"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_listsinks, gensym("list_sinks"), A_NULL);
}
