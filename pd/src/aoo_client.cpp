/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoonet/aoonet.hpp"

#include <thread>

#define AOO_CLIENT_POLL_INTERVAL 2

t_class *aoo_client_class;

struct t_aoo_client
{
    t_aoo_client(int argc, t_atom *argv);
    ~t_aoo_client();

    t_object x_obj;

    aoo::net::iclient::pointer x_client;
    i_node *x_node = nullptr;
    std::thread x_thread;
    t_clock *x_clock = nullptr;
    t_outlet *x_stateout = nullptr;
    t_outlet *x_msgout = nullptr;
};

void aoo_client_send(t_aoo_client *x)
{
    x->x_client->send();
}

void aoo_client_handle_message(t_aoo_client *x, const char * data,
                               int32_t n, void *endpoint, aoo_replyfn fn)
{
    t_endpoint *e = (t_endpoint *)endpoint;
    x->x_client->handle_message(data, n, &e->e_addr);
}

static int32_t aoo_client_handle_events(t_aoo_client *x,
                                        const aoo_event **events, int32_t n)
{
    for (int i = 0; i < n; ++i){
        switch (events[i]->type){
        case AOONET_CLIENT_CONNECT_EVENT:
        {
            aoonet_client_group_event *e = (aoonet_client_group_event *)events[i];
            if (e->result > 0){
                outlet_float(x->x_stateout, 1); // connected
            } else {
                pd_error(x, "%s: couldn't connect to server - %s",
                         classname(x), e->errormsg);

                outlet_float(x->x_stateout, 0); // disconnected
            }
            break;
        }
        case AOONET_CLIENT_DISCONNECT_EVENT:
        {
            aoonet_client_group_event *e = (aoonet_client_group_event *)events[i];
            if (e->result == 0){
                pd_error(x, "%s: disconnected from server - %s",
                         classname(x), e->errormsg);
            }

            x->x_node->remove_all_peers();

            outlet_float(x->x_stateout, 0); // disconnected
            break;
        }
        case AOONET_CLIENT_GROUP_JOIN_EVENT:
        {
            aoonet_client_group_event *e = (aoonet_client_group_event *)events[i];
            if (e->result > 0){
                t_atom msg;
                SETSYMBOL(&msg, gensym(e->name));
                outlet_anything(x->x_msgout, gensym("group_join"), 1, &msg);
            } else {
                pd_error(x, "%s: couldn't join group %s - %s",
                         classname(x), e->name, e->errormsg);
            }
            break;
        }
        case AOONET_CLIENT_GROUP_LEAVE_EVENT:
        {
            aoonet_client_group_event *e = (aoonet_client_group_event *)events[i];
            if (e->result > 0){
                x->x_node->remove_group(gensym(e->name));

                t_atom msg;
                SETSYMBOL(&msg, gensym(e->name));
                outlet_anything(x->x_msgout, gensym("group_leave"), 1, &msg);
            } else {
                pd_error(x, "%s: couldn't leave group %s - %s",
                         classname(x), e->name, e->errormsg);
            }
            break;
        }
        case AOONET_CLIENT_PEER_JOIN_EVENT:
        {
            aoonet_client_peer_event *e = (aoonet_client_peer_event *)events[i];

            if (e->result > 0){
                x->x_node->add_peer(gensym(e->group), gensym(e->user),
                                    (const sockaddr *)e->address, e->length);

                t_atom msg[4];
                SETSYMBOL(msg, gensym(e->group));
                SETSYMBOL(msg + 1, gensym(e->user));
                if (sockaddr_to_atoms((const struct sockaddr *)e->address,
                                      e->length, msg + 2))
                {
                    outlet_anything(x->x_msgout, gensym("peer_join"), 4, msg);
                }
            } else {
                bug("%s: AOONET_CLIENT_PEER_JOIN_EVENT", classname(x));
            }
            break;
        }
        case AOONET_CLIENT_PEER_LEAVE_EVENT:
        {
            aoonet_client_peer_event *e = (aoonet_client_peer_event *)events[i];

            if (e->result > 0){
                x->x_node->remove_peer(gensym(e->group), gensym(e->user));

                t_atom msg[4];
                SETSYMBOL(msg, gensym(e->group));
                SETSYMBOL(msg + 1, gensym(e->user));
                if (sockaddr_to_atoms((const struct sockaddr *)e->address,
                                      e->length, msg + 2))
                {
                    outlet_anything(x->x_msgout, gensym("peer_leave"), 4, msg);
                }
            } else {
                bug("%s: AOONET_CLIENT_PEER_LEAVE_EVENT", classname(x));
            }
            break;
        }
        case AOONET_CLIENT_ERROR_EVENT:
        {
            aoonet_client_event *e = (aoonet_client_event *)events[i];
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

static void aoo_client_tick(t_aoo_client *x)
{
    x->x_client->handle_events((aoo_eventhandler)aoo_client_handle_events, x);

    x->x_node->notify();

    clock_delay(x->x_clock, AOO_CLIENT_POLL_INTERVAL);
}

static void aoo_client_connect(t_aoo_client *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc < 4){
        pd_error(x, "%s: too few arguments for '%s' method", classname(x), s->s_name);
        return;
    }
    if (x->x_client){
        // first remove peers (to be sure)
        x->x_node->remove_all_peers();

        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        t_symbol *username = atom_getsymbol(argv + 2);
        t_symbol *pwd = atom_getsymbol(argv + 3);

        x->x_client->connect(host->s_name, port, username->s_name, pwd->s_name);
    }
}

static void aoo_client_disconnect(t_aoo_client *x)
{
    if (x->x_client){
        x->x_node->remove_all_peers();

        x->x_client->disconnect();
    }
}

static void aoo_client_group_join(t_aoo_client *x, t_symbol *group, t_symbol *pwd)
{
    if (x->x_client){
        x->x_client->group_join(group->s_name, pwd->s_name);
    }
}

static void aoo_client_group_leave(t_aoo_client *x, t_symbol *s)
{
    if (x->x_client){
        x->x_client->group_leave(s->s_name);
    }
}

static void * aoo_client_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_client_class);
    new (x) t_aoo_client(argc, argv);
    return x;
}

static int32_t aoo_node_sendto(i_node *node,
        const char *data, int32_t size, const sockaddr *addr)
{
    return node->sendto(data, size, addr);
}

t_aoo_client::t_aoo_client(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_client_tick);
    x_stateout = outlet_new(&x_obj, 0);
    x_msgout = outlet_new(&x_obj, 0);

    int port = argc ? atom_getfloat(argv) : 0;

    x_node = port > 0 ? i_node::get(port, (t_pd *)this, 0) : nullptr;

    if (x_node){
        x_client.reset(aoo::net::iclient::create(
                       x_node,(aoo_sendfn)aoo_node_sendto, port));
        if (x_client){
            verbose(0, "new aoo client on port %d", port);
            // start thread
            x_thread = std::thread([this](){
               x_client->run();
            });
            // start clock
            clock_delay(x_clock, AOO_CLIENT_POLL_INTERVAL);
        }
    }
}

static void aoo_client_free(t_aoo_client *x)
{
    x->~t_aoo_client();
}

t_aoo_client::~t_aoo_client()
{
    if (x_node){
        x_node->remove_all_peers();

        x_node->release((t_pd *)this, 0);
    }

    if (x_client){
        x_client->quit();
        // wait for thread to finish
        x_thread.join();
    }

    clock_free(x_clock);
}

void aoo_client_setup(void)
{
    aoo_client_class = class_new(gensym("aoo_client"), (t_newmethod)(void *)aoo_client_new,
        (t_method)aoo_client_free, sizeof(t_aoo_client), 0, A_GIMME, A_NULL);
    class_sethelpsymbol(aoo_client_class, gensym("aoo_server"));

    class_addmethod(aoo_client_class, (t_method)aoo_client_connect,
                    gensym("connect"), A_GIMME, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_disconnect,
                    gensym("disconnect"), A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_group_join,
                    gensym("group_join"), A_SYMBOL, A_SYMBOL, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_group_leave,
                    gensym("group_leave"), A_SYMBOL, A_NULL);
}
