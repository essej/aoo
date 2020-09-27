/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoo/aoo_net.hpp"

#include <thread>

using namespace aoo;

#define AOO_SERVER_POLL_INTERVAL 2

static t_class *aoo_server_class;

struct t_aoo_server
{
    t_aoo_server(int argc, t_atom *argv);
    ~t_aoo_server();

    t_object x_obj;

    aoo::net::iserver::pointer x_server;
    int32_t x_numusers = 0;
    std::thread x_thread;
    t_clock *x_clock = nullptr;
    t_outlet *x_stateout = nullptr;
    t_outlet *x_msgout = nullptr;
};

static int32_t aoo_server_handle_events(t_aoo_server *x,
                                        const aoo_event **events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        switch (events[i]->type){
        case AOO_NET_SERVER_USER_JOIN_EVENT:
        {
            aoo_net_server_user_event *e = (aoo_net_server_user_event *)events[i];

            t_atom msg;
            SETSYMBOL(&msg, gensym(e->name));
            outlet_anything(x->x_msgout, gensym("user_join"), 1, &msg);

            x->x_numusers++;

            outlet_float(x->x_stateout, x->x_numusers);

            break;
        }
        case AOO_NET_SERVER_USER_LEAVE_EVENT:
        {
            aoo_net_server_user_event *e = (aoo_net_server_user_event *)events[i];

            t_atom msg;
            SETSYMBOL(&msg, gensym(e->name));
            outlet_anything(x->x_msgout, gensym("user_leave"), 1, &msg);

            x->x_numusers--;

            outlet_float(x->x_stateout, x->x_numusers);

            break;
        }
        case AOO_NET_SERVER_GROUP_JOIN_EVENT:
        {
            aoo_net_server_group_event *e = (aoo_net_server_group_event *)events[i];

            t_atom msg[2];
            SETSYMBOL(msg, gensym(e->group));
            SETSYMBOL(msg + 1, gensym(e->user));
            outlet_anything(x->x_msgout, gensym("group_join"), 2, msg);

            break;
        }
        case AOO_NET_SERVER_GROUP_LEAVE_EVENT:
        {
            aoo_net_server_group_event *e = (aoo_net_server_group_event *)events[i];

            t_atom msg[2];
            SETSYMBOL(msg, gensym(e->group));
            SETSYMBOL(msg + 1, gensym(e->user));
            outlet_anything(x->x_msgout, gensym("group_leave"), 2, msg);

            break;
        }
        case AOO_NET_SERVER_ERROR_EVENT:
        {
            aoo_net_server_event *e = (aoo_net_server_event *)events[i];
            pd_error(x, "%s: %s", classname(x), e->errormsg);
            break;
        }
        default:
            pd_error(x, "%s: got unknown event %d", classname(x), events[i]->type);
            break;
        }
    }
    return 1;
}

static void aoo_server_tick(t_aoo_server *x)
{
    x->x_server->handle_events((aoo_eventhandler)aoo_server_handle_events, x);
    clock_delay(x->x_clock, AOO_SERVER_POLL_INTERVAL);
}

static void * aoo_server_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_server_class);
    new (x) t_aoo_server(argc, argv);
    return x;
}

t_aoo_server::t_aoo_server(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_server_tick);
    x_stateout = outlet_new(&x_obj, 0);
    x_msgout = outlet_new(&x_obj, 0);

    int port = argc ? atom_getfloat(argv) : 0;

    if (port > 0){
        int32_t err;
        x_server.reset(aoo::net::iserver::create(port, &err));
        if (x_server){
            verbose(0, "aoo server listening on port %d", port);
            // start thread
            x_thread = std::thread([this](){
                x_server->run();
            });
            // start clock
            clock_delay(x_clock, AOO_SERVER_POLL_INTERVAL);
        } else {
            char buf[MAXPDSTRING];
            socket_strerror(err, buf, sizeof(buf));
            pd_error(this, "%s: %s (%d)", classname(this), buf, err);
        }
    }
}

static void aoo_server_free(t_aoo_server *x)
{
    x->~t_aoo_server();
}

t_aoo_server::~t_aoo_server()
{
    if (x_server){
        x_server->quit();
        // wait for thread to finish
        x_thread.join();
    }
    clock_free(x_clock);
}

void aoo_server_setup(void)
{
    aoo_server_class = class_new(gensym("aoo_server"), (t_newmethod)(void *)aoo_server_new,
        (t_method)aoo_server_free, sizeof(t_aoo_server), 0, A_GIMME, A_NULL);
}
