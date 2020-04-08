/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.h"
#include "aoo/aoo_net.h"

#include <pthread.h>

#define AOO_SERVER_POLL_INTERVAL 10

static t_class *aoo_server_class;

typedef struct _aoo_server
{
    t_object x_obj;
    aoonet_server *x_server;
    pthread_t x_thread;
    t_clock *x_clock;
    t_outlet *x_stateout;
    t_outlet *x_msgout;
} t_aoo_server;

static int32_t aoo_server_handle_events(t_aoo_server *x,
                                        const aoo_event **events, int32_t n)
{
    return 1;
}

static void aoo_server_tick(t_aoo_server *x)
{
    aoonet_server_handle_events(x->x_server,
                               (aoo_eventhandler)aoo_server_handle_events, x);
    clock_delay(x->x_clock, AOO_SERVER_POLL_INTERVAL);
}

static void *aoo_server_threadfn(void *y)
{
    t_aoo_server *x = (t_aoo_server *)y;
    aoonet_server_run(x->x_server);
    return 0;
}

static void * aoo_server_new(t_symbol *s, int argc, t_atom *argv)
{
    t_aoo_server *x = (t_aoo_server *)pd_new(aoo_server_class);

    x->x_clock = clock_new(x, (t_method)aoo_server_tick);
    x->x_stateout = outlet_new(&x->x_obj, 0);
    x->x_msgout = outlet_new(&x->x_obj, 0);

    int port = argc ? atom_getfloat(argv) : 0;

    if (port > 0){
        int32_t err;
        x->x_server = aoonet_server_new(port, &err);
        if (x->x_server){
            verbose(0, "aoo server listening on port %d", port);
            // start thread
            pthread_create(&x->x_thread, 0, aoo_server_threadfn, x);
            // start clock
            clock_delay(x->x_clock, AOO_SERVER_POLL_INTERVAL);
        } else {
            char buf[MAXPDSTRING];
            socket_strerror(err, buf, sizeof(buf));
            pd_error(x, "%s: %s (%d)", classname(x), buf, err);
        }
    }
    return x;
}

static void aoo_server_free(t_aoo_server *x)
{
    if (x->x_server){
        aoonet_server_quit(x->x_server);
        // wait for thread to finish
        pthread_join(x->x_thread, 0);
        aoonet_server_free(x->x_server);
    }
    clock_free(x->x_clock);
}

void aoo_server_setup(void)
{
    aoo_server_class = class_new(gensym("aoo_server"), (t_newmethod)(void *)aoo_server_new,
        (t_method)aoo_server_free, sizeof(t_aoo_server), 0, A_GIMME, A_NULL);
}
