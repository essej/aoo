/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "common/sync.hpp"

#include <vector>
#include <string.h>
#include <assert.h>
#include <stdio.h>
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

using namespace aoo;

// for hardware buffer sizes up to 1024 @ 44.1 kHz
#define DEFBUFSIZE 25

t_class *aoo_send_class;

struct t_sink
{
    aoo::endpoint *s_endpoint;
    int32_t s_id;
};

struct t_aoo_send
{
    t_aoo_send(int argc, t_atom *argv);
    ~t_aoo_send();

    t_object x_obj;

    t_float x_f = 0;
    aoo::isource::pointer x_source;
    int32_t x_samplerate = 0;
    int32_t x_blocksize = 0;
    int32_t x_nchannels = 0;
    int32_t x_port = 0;
    int32_t x_id = 0;
    std::unique_ptr<t_float *[]> x_vec;
    // sinks
    std::vector<t_sink> x_sinks;
    // node
    i_node *x_node = nullptr;
    aoo::shared_mutex x_lock;
    // events
    t_clock *x_clock = nullptr;
    t_outlet *x_msgout = nullptr;
    bool x_accept = true;
};

static void aoo_send_doaddsink(t_aoo_send *x, aoo::endpoint *e, int32_t id)
{
    x->x_source->add_sink(e, id, (aoo_replyfn)endpoint_send);

    // add sink to list
    x->x_sinks.push_back({e, id});

    // output message
    t_atom msg[3];
    if (endpoint_to_atoms(*e, id, 3, msg)){
        outlet_anything(x->x_msgout, gensym("sink_add"), 3, msg);
    } else {
        bug("aoo_endpoint_to_atoms");
    }
}

static void aoo_send_doremoveall(t_aoo_send *x)
{
    x->x_source->remove_all();

    int numsinks = x->x_sinks.size();
    if (!numsinks){
        return;
    }

    // temporary copies (for reentrancy)
    t_sink *sinks = (t_sink *)alloca(sizeof(t_sink) * numsinks);
    std::copy(x->x_sinks.begin(), x->x_sinks.end(), sinks);

    x->x_sinks.clear();

    // output messages
    for (int i = 0; i < numsinks; ++i){
        t_atom msg[3];
        if (endpoint_to_atoms(*sinks[i].s_endpoint, sinks[i].s_id, 3, msg)){
            outlet_anything(x->x_msgout, gensym("sink_remove"), 3, msg);
        } else {
            bug("aoo_endpoint_to_atoms");
        }
    }
}

static void aoo_send_doremovesink(t_aoo_send *x, aoo::endpoint *e, int32_t id)
{
    x->x_source->remove_sink(e, id);

    // remove from list
    if (id == AOO_ID_WILDCARD){
        // pre-allocate array of removed sinks
        int removed = 0;
        t_sink *sinks = (t_sink *)alloca(sizeof(t_sink) * x->x_sinks.size());

        // remove all sinks matching endpoint
        for (auto it = x->x_sinks.begin(); it != x->x_sinks.end();){
            if (it->s_endpoint == e){
                sinks[removed++] = *it;
                it = x->x_sinks.erase(it);
            } else {
                ++it;
            }
        }

        // output messages
        for (int i = 0; i < removed; ++i){
            t_atom msg[3];
            if (endpoint_to_atoms(*sinks[i].s_endpoint, sinks[i].s_id, 3, msg)){
                outlet_anything(x->x_msgout, gensym("sink_remove"), 3, msg);
            } else {
                bug("aoo_endpoint_to_atoms");
            }
        }
        return;
    } else {
        // remove the sink matching endpoint and id
        for (auto it = x->x_sinks.begin(); it != x->x_sinks.end(); ++it){
            if (it->s_endpoint == e && it->s_id == id){
                x->x_sinks.erase(it);

                // output message
                t_atom msg[3];
                if (endpoint_to_atoms(*e, id, 3, msg)){
                    outlet_anything(x->x_msgout, gensym("sink_remove"), 3, msg);
                } else {
                    bug("aoo_endpoint_to_atoms");
                }
                return;
            }
        }
    }

    // only wildcard IDs are allowed to not match anything
    if (id != AOO_ID_WILDCARD){
        bug("aoo_send_doremovesink");
    }
}

// called from the network receive thread
void aoo_send_handle_message(t_aoo_send *x, const char * data,
                                int32_t n, void *endpoint, aoo_replyfn fn)
{
    // synchronize with aoo_receive_dsp()
    aoo::shared_scoped_lock lock(x->x_lock);
    // handle incoming message
    x->x_source->handle_message(data, n, endpoint, fn);
}

// called from the network send thread
void aoo_send_send(t_aoo_send *x)
{
    // synchronize with aoo_receive_dsp()
    aoo::shared_scoped_lock lock(x->x_lock);
    // send outgoing messages
    while (x->x_source->send()) ;
}

static int32_t aoo_send_handle_events(t_aoo_send *x, const aoo_event **events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        switch (events[i]->type){
        case AOO_PING_EVENT:
        {
            auto e = (aoo_ping_event *)events[i];
            auto ep = (aoo::endpoint *)e->endpoint;
            double diff1 = aoo_osctime_duration(e->tt1, e->tt2) * 1000.0;
            double diff2 = aoo_osctime_duration(e->tt2, e->tt3) * 1000.0;
            double rtt = aoo_osctime_duration(e->tt1, e->tt3) * 1000.0;

            t_atom msg[7];
            if (endpoint_to_atoms(*ep, e->id, 3, msg)){
                SETFLOAT(msg + 3, diff1);
                SETFLOAT(msg + 4, diff2);
                SETFLOAT(msg + 5, rtt);
                SETFLOAT(msg + 6, e->lost_blocks);
                outlet_anything(x->x_msgout, gensym("ping"), 7, msg);
            } else {
                bug("aoo_endpoint_to_atoms");
            }
            break;
        }
        case AOO_INVITE_EVENT:
        {
            auto e = (aoo_sink_event *)events[i];
            auto ep = (aoo::endpoint *)e->endpoint;

            if (x->x_accept){
                aoo_send_doaddsink(x, ep, e->id);
            } else {
                t_atom msg[3];
                if (endpoint_to_atoms(*ep, e->id, 3, msg)){
                    outlet_anything(x->x_msgout, gensym("invite"), 3, msg);
                } else {
                    bug("aoo_endpoint_to_atoms");
                }
            }

            break;
        }
        case AOO_UNINVITE_EVENT:
        {
            auto e = (aoo_sink_event *)events[i];
            auto ep = (aoo::endpoint *)e->endpoint;

            if (x->x_accept){
                aoo_send_doremovesink(x, ep, e->id);
            } else {
                t_atom msg[3];
                if (endpoint_to_atoms(*ep, e->id, 3, msg)){
                    outlet_anything(x->x_msgout, gensym("uninvite"), 3, msg);
                } else {
                    bug("aoo_endpoint_to_atoms");
                }
            }
            break;
        }
        default:
            break;
        }
    }
    return 1;
}

static void aoo_send_tick(t_aoo_send *x)
{
    x->x_source->handle_events((aoo_eventhandler)aoo_send_handle_events, x);
}

static void aoo_send_format(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    aoo_format_storage f;
    f.header.nchannels = x->x_nchannels;
    if (format_parse(x, f, argc, argv)){
        x->x_source->set_format(f.header);
        // output actual format
        t_atom msg[16];
        int n = format_to_atoms(f.header, 16, msg);
        if (n > 0){
            outlet_anything(x->x_msgout, gensym("format"), n, msg);
        }
    }
}

static t_sink *aoo_send_findsink(t_aoo_send *x, const ip_address& addr, int32_t id)
{
    for (auto& sink : x->x_sinks){
        if (sink.s_id == id && sink.s_endpoint->address() == addr){
            return &sink;
        }
    }
    return nullptr;
}

static void aoo_send_accept(t_aoo_send *x, t_floatarg f)
{
    x->x_accept = f != 0;
}

static void aoo_send_channel(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    ip_address addr;
    int32_t id;
    if (argc < 4){
        pd_error(x, "%s: too few arguments for 'channel' message", classname(x));
        return;
    }
    if (get_sinkarg(x, x->x_node, argc, argv, addr, id)){
        auto sink = aoo_send_findsink(x, addr, id);
        if (!sink){
            pd_error(x, "%s: couldn't find sink!", classname(x));
            return;
        }
        int32_t chn = atom_getfloat(argv + 3);

        x->x_source->set_sink_channelonset(sink->s_endpoint, sink->s_id, chn);
    }
}

static void aoo_send_packetsize(t_aoo_send *x, t_floatarg f)
{
    x->x_source->set_packetsize(f);
}

static void aoo_send_ping(t_aoo_send *x, t_floatarg f)
{
    x->x_source->set_ping_interval(f);
}

static void aoo_send_resend(t_aoo_send *x, t_floatarg f)
{
    x->x_source->set_buffersize(f);
}

static void aoo_send_redundancy(t_aoo_send *x, t_floatarg f)
{
    x->x_source->set_redundancy(f);
}

static void aoo_send_timefilter(t_aoo_send *x, t_floatarg f)
{
    x->x_source->set_timefilter_bandwidth(f);
}

static void aoo_send_add(t_aoo_send *x, t_symbol *s, int argc, t_atom *argv)
{
    if (!x->x_node){
        pd_error(x, "%s: can't add sink - no socket!", classname(x));
        return;
    }

    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'add' message", classname(x));
        return;
    }

    ip_address addr;
    int32_t id;
    if (get_sinkarg(x, x->x_node, argc, argv, addr, id)){
        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        auto e = x->x_node->get_endpoint(addr);
        // check if sink exists
        if (id != AOO_ID_WILDCARD){
            for (auto& sink : x->x_sinks){
                if (sink.s_endpoint == e){
                    if (sink.s_id == AOO_ID_WILDCARD){
                        pd_error(x, "%s: sink %s %d %d already added via wildcard!",
                                 classname(x), host->s_name, port, id);
                        return;
                    } else if (sink.s_id == id){
                        pd_error(x, "%s: sink %s %d %d already added!",
                                 classname(x), host->s_name, port, id);
                        return;
                    }
                }
            }
        }

        if (argc > 3){
            x->x_source->set_sink_channelonset(e, id, atom_getfloat(argv + 3));
        }

        if (id == AOO_ID_WILDCARD){
            // first remove all sinks on this endpoint
            aoo_send_doremovesink(x, e, AOO_ID_WILDCARD);
        }

        aoo_send_doaddsink(x, e, id);

        // print message (use actual hostname)
        if (endpoint_get_address(*e, host, port)){
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
    if (!x->x_node){
        pd_error(x, "%s: can't remove sink - no socket!", classname(x));
        return;
    }

    if (!argc){
        aoo_send_doremoveall(x);
        return;
    }

    if (argc < 3){
        pd_error(x, "%s: too few arguments for 'remove' message", classname(x));
        return;
    }

    ip_address addr;
    int32_t id;
    if (get_sinkarg(x, x->x_node, argc, argv, addr, id)){
        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        aoo::endpoint *e = nullptr;
        if (id != AOO_ID_WILDCARD){
            // check if sink exists
            for (auto& sink : x->x_sinks){
                if (sink.s_endpoint->address() == addr){
                    if (sink.s_id == AOO_ID_WILDCARD){
                        pd_error(x, "%s: can't remove sink %s %d %d because of wildcard!",
                                 classname(x), host->s_name, port, id);
                        return;
                    } else if (sink.s_id == id) {
                        e = sink.s_endpoint;
                        break;
                    }
                }
            }
        } else {
            e = x->x_node->get_endpoint(addr);
        }

        if (!e){
            pd_error(x, "%s: couldn't find sink %s %d %d!",
                     classname(x), host->s_name, port, id);
            return;
        }

        aoo_send_doremovesink(x, e, id);

        // print message (use actual hostname)
        if (endpoint_get_address(*e, host, port)){
            if (id == AOO_ID_WILDCARD){
                verbose(0, "removed all sinks on %s %d", host->s_name, port);
            } else {
                verbose(0, "removed sink %s %d %d", host->s_name, port, id);
            }
        }
    }
}

static void aoo_send_start(t_aoo_send *x)
{
    x->x_source->start();
}

static void aoo_send_stop(t_aoo_send *x)
{
    x->x_source->stop();
}

static void aoo_send_listsinks(t_aoo_send *x)
{
    for (auto& sink : x->x_sinks){
        t_symbol *host;
        int port;
        if (endpoint_get_address(*sink.s_endpoint, host, port)){
            t_atom msg[3];
            SETSYMBOL(msg, host);
            SETFLOAT(msg + 1, port);
            if (sink.s_id == AOO_ID_WILDCARD){
                SETSYMBOL(msg + 2, gensym("*"));
            } else {
                SETFLOAT(msg + 2, sink.s_id);
            }
            outlet_anything(x->x_msgout, gensym("sink"), 3, msg);
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
    if (x->x_source->process((const aoo_sample **)x->x_vec.get(), n, t) > 0){
        if (x->x_node){
            x->x_node->notify();
        }
    }
    if (x->x_source->events_available() > 0){
        clock_delay(x->x_clock, 0);
    }

    return w + 3;
}

static void aoo_send_dsp(t_aoo_send *x, t_signal **sp)
{
    int32_t blocksize = sp[0]->s_n;
    int32_t samplerate = sp[0]->s_sr;

    for (int i = 0; i < x->x_nchannels; ++i){
        x->x_vec[i] = sp[i]->s_vec;
    }

    // synchronize with network threads!
    aoo::scoped_lock lock(x->x_lock); // writer lock!

    if (blocksize != x->x_blocksize || samplerate != x->x_samplerate){
        x->x_source->setup(samplerate, blocksize, x->x_nchannels);
        x->x_blocksize = blocksize;
        x->x_samplerate = samplerate;
    }

    dsp_add(aoo_send_perform, 2, (t_int)x, (t_int)x->x_blocksize);
}

static void aoo_send_port(t_aoo_send *x, t_floatarg f)
{
    int port = f;

    // 0 is allowed -> don't listen
    if (port < 0){
        pd_error(x, "%s: bad port %d", classname(x), port);
        return;
    }

    if (x->x_node){
        x->x_node->release((t_pd *)x, x->x_id);
    }

    x->x_node = port ? i_node::get(port, (t_pd *)x, x->x_id) : 0;
    x->x_port = port;
}

static void aoo_send_id(t_aoo_send *x, t_floatarg f)
{
    int id = f;

    if (id == x->x_id){
        return;
    }

    if (id < 0){
        pd_error(x, "%s: bad id %d", classname(x), id);
        return;
    }

    if (x->x_node){
        x->x_node->release((t_pd *)x, x->x_id);
    }

    x->x_source->set_id(id);

    x->x_node = x->x_port ? i_node::get(x->x_port, (t_pd *)x, id) : 0;
    x->x_id = id;
}

static void * aoo_send_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_send_class);
    new (x) t_aoo_send(argc, argv);
    return x;
}

t_aoo_send::t_aoo_send(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_send_tick);

    // arg #1: port number
    x_port = atom_getfloatarg(0, argc, argv);

    // arg #2: ID
    int id = atom_getfloatarg(1, argc, argv);
    if (id < 0){
        pd_error(this, "%s: bad id % d, setting to 0", classname(this), id);
        id = 0;
    }
    x_id = id;

    // arg #3: num channels
    int nchannels = atom_getfloatarg(2, argc, argv);
    if (nchannels < 1){
        nchannels = 1;
    }
    x_nchannels = nchannels;

    // make additional inlets
    if (nchannels > 1){
        int i = nchannels;
        while (--i){
            inlet_new(&x_obj, &x_obj.ob_pd, &s_signal, &s_signal);
        }
    }
    x_vec = std::make_unique<t_sample *[]>(nchannels);

    // make event outlet
    x_msgout = outlet_new(&x_obj, 0);

    // create and initialize aoo_source object
    x_source.reset(aoo::isource::create(x_id));

    aoo_format_storage fmt;
    format_makedefault(fmt, nchannels);
    x_source->set_format(fmt.header);

    x_source->set_buffersize(DEFBUFSIZE);

    // finally we're ready to receive messages
    aoo_send_port(this, x_port);
}

static void aoo_send_free(t_aoo_send *x)
{
    x->~t_aoo_send();
}

t_aoo_send::~t_aoo_send()
{
    // first stop receiving messages
    if (x_node){
        x_node->release((t_pd *)this, x_id);
    }

    clock_free(x_clock);
}

void aoo_send_tilde_setup(void)
{
    aoo_send_class = class_new(gensym("aoo_send~"), (t_newmethod)(void *)aoo_send_new,
        (t_method)aoo_send_free, sizeof(t_aoo_send), 0, A_GIMME, A_NULL);
    CLASS_MAINSIGNALIN(aoo_send_class, t_aoo_send, x_f);
    class_addmethod(aoo_send_class, (t_method)aoo_send_dsp,
                    gensym("dsp"), A_CANT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_port,
                    gensym("port"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_id,
                    gensym("id"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_add,
                    gensym("add"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_remove,
                    gensym("remove"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_start,
                    gensym("start"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_stop,
                    gensym("stop"), A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_accept,
                    gensym("accept"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_format,
                    gensym("format"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_channel,
                    gensym("channel"), A_GIMME, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_packetsize,
                    gensym("packetsize"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_ping,
                    gensym("ping"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_resend,
                    gensym("resend"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_redundancy,
                    gensym("redundancy"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_timefilter,
                    gensym("timefilter"), A_FLOAT, A_NULL);
    class_addmethod(aoo_send_class, (t_method)aoo_send_listsinks,
                    gensym("list_sinks"), A_NULL);
}
