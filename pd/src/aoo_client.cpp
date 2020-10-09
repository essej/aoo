/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoo/aoo_net.hpp"

#include "common/sync.hpp"

#include <thread>
#include <functional>
#include <vector>

using namespace aoo;

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

    // replies
    using t_reply = std::function<void()>;
    std::vector<t_reply> replies_;
    aoo::shared_mutex reply_mutex_;
    void push_reply(t_reply reply){
        aoo::scoped_lock lock(reply_mutex_);
        replies_.push_back(std::move(reply));
    }

    t_clock *x_clock = nullptr;
    t_outlet *x_stateout = nullptr;
    t_outlet *x_msgout = nullptr;
};

struct t_group_request {
    t_aoo_client *obj;
    t_symbol *group;
};

static void aoo_client_list_peers(t_aoo_client *x)
{
    if (x->x_node){
        x->x_node->list_peers(x->x_msgout);
    }
}

void aoo_client_send(t_aoo_client *x)
{
    x->x_client->send();
}

void aoo_client_handle_message(t_aoo_client *x, const char * data,
                               int32_t n, void *endpoint, aoo_replyfn fn)
{
    auto e = (aoo::endpoint *)endpoint;
    x->x_client->handle_message(data, n, (void *)e->address().address(),
                                e->address().length());
}

static void aoo_client_handle_event(t_aoo_client *x, const aoo_event *event)
{
    switch (event->type){
    case AOO_NET_DISCONNECT_EVENT:
    {
        post("%s: disconnected from server", classname(x));

        x->x_node->remove_all_peers();

        outlet_float(x->x_stateout, 0); // disconnected
        break;
    }
    case AOO_NET_PEER_JOIN_EVENT:
    {
        auto e = (const aoo_net_peer_event *)event;

        ip_address addr((const sockaddr *)e->address, e->length);
        auto group = gensym(e->group_name);
        auto user = gensym(e->user_name);
        auto id = e->user_id;

        x->x_node->add_peer(group, user, id, addr);

        t_atom msg[5];
        SETSYMBOL(msg, group);
        SETSYMBOL(msg + 1, user);
        SETFLOAT(msg + 2, id);
        address_to_atoms(addr, 2, msg + 3);

        outlet_anything(x->x_msgout, gensym("peer_join"), 5, msg);
        break;
    }
    case AOO_NET_PEER_LEAVE_EVENT:
    {
        auto e = (const aoo_net_peer_event *)event;

        ip_address addr((const sockaddr *)e->address, e->length);
        auto group = gensym(e->group_name);
        auto user = gensym(e->user_name);
        auto id = e->user_id;

        x->x_node->remove_peer(group, user);

        t_atom msg[5];
        SETSYMBOL(msg, group);
        SETSYMBOL(msg + 1, user);
        SETFLOAT(msg + 2, id);
        address_to_atoms(addr, 2, msg + 3);

        outlet_anything(x->x_msgout, gensym("peer_leave"), 5, msg);
        break;
    }
    case AOO_NET_ERROR_EVENT:
    {
        auto e = (const aoo_net_error_event *)event;
        pd_error(x, "%s: %s", classname(x), e->errormsg);
        break;
    }
    default:
        pd_error(x, "%s: got unknown event %d", classname(x), event->type);
        break;
    }
}

static void aoo_client_tick(t_aoo_client *x)
{
    x->x_client->poll_events((aoo_eventhandler)aoo_client_handle_event, x);

    x->x_node->notify();

    // handle server replies
    if (x->reply_mutex_.try_lock()){
        for (auto& reply : x->replies_){
            reply();
        }
        x->replies_.clear();
        x->reply_mutex_.unlock();
    } else {
        LOG_DEBUG("aoo_client_tick: would block");
    }

    clock_delay(x->x_clock, AOO_CLIENT_POLL_INTERVAL);
}

struct t_error_reply {
    int code;
    std::string msg;
};

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

        // LATER also send user ID
        auto cb = [](void *y, int32_t result, const void *data){
            auto x = (t_aoo_client *)y;

            if (result == 0){
                x->push_reply([x](){
                    outlet_float(x->x_stateout, 1); // connected
                });
            } else {
                auto reply = (const aoo_net_error_reply *)data;
                t_error_reply error { reply->errorcode, reply->errormsg };

                x->push_reply([x, error=std::move(error)](){
                    pd_error(x, "%s: couldn't connect to server: %s",
                             classname(x), error.msg.c_str());

                    outlet_float(x->x_stateout, 0); // fail
                });
            }
        };
        x->x_client->connect(host->s_name, port,
                             username->s_name, pwd->s_name, cb, x);
    }
}

static void aoo_client_disconnect(t_aoo_client *x)
{
    if (x->x_client){
        auto cb = [](void *y, int32_t result, const void *data){
            auto x = (t_aoo_client *)y;
            if (result == 0){
                x->push_reply([x](){
                    // we have to remove the peers manually!
                    x->x_node->remove_all_peers();

                    outlet_float(x->x_stateout, 0); // disconnected
                });
            }
        };
        x->x_client->disconnect(cb, x);
    }
}

static void aoo_client_group_join(t_aoo_client *x, t_symbol *group, t_symbol *pwd)
{
    if (x->x_client){
        auto cb = [](void *x, int32_t result, const void *data){
            auto request = (t_group_request *)x;
            auto obj = request->obj;
            auto group = request->group;

            if (result == 0){
                obj->push_reply([obj, group](){
                    t_atom msg[2];
                    SETSYMBOL(msg, group);
                    SETFLOAT(msg + 1, 1); // 1: success
                    outlet_anything(obj->x_msgout, gensym("group_join"), 2, msg);
                });
            } else {
                auto reply = (const aoo_net_error_reply *)data;
                t_error_reply error { reply->errorcode, reply->errormsg };

                obj->push_reply([obj, group, error=std::move(error)](){
                    pd_error(obj, "%s: couldn't join group %s - %s",
                             classname(obj), group->s_name, error.msg.c_str());

                    t_atom msg[2];
                    SETSYMBOL(msg, group);
                    SETFLOAT(msg + 1, 0); // 0: fail
                    outlet_anything(obj->x_msgout, gensym("group_join"), 2, msg);
                });
            }

            delete request;
        };
        x->x_client->join_group(group->s_name, pwd->s_name,
                                cb, new t_group_request { x, group });
    }
}

static void aoo_client_group_leave(t_aoo_client *x, t_symbol *group)
{
    if (x->x_client){
        auto cb = [](void *x, int32_t result, const void *data){
            auto request = (t_group_request *)x;
            auto obj = request->obj;
            auto group = request->group;

            if (result == 0){
                obj->push_reply([obj, group](){
                    // we have to remove the peers manually!
                    obj->x_node->remove_group(group);

                    t_atom msg[2];
                    SETSYMBOL(msg, group);
                    SETFLOAT(msg + 1, 1); // 1: success
                    outlet_anything(obj->x_msgout, gensym("group_leave"), 2, msg);
                });
            } else {
                auto reply = (const aoo_net_error_reply *)data;
                t_error_reply error { reply->errorcode, reply->errormsg };

                obj->push_reply([obj, group, error=std::move(error)](){
                    pd_error(obj, "%s: couldn't leave group %s - %s",
                             classname(obj), group->s_name, error.msg.c_str());

                    t_atom msg[2];
                    SETSYMBOL(msg, group);
                    SETFLOAT(msg + 1, 0); // 0: fail
                    outlet_anything(obj->x_msgout, gensym("group_leave"), 2, msg);
                });
            }

            delete request;
        };
        x->x_client->leave_group(group->s_name, cb, new t_group_request { x, group });
    }
}

static void * aoo_client_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_client_class);
    new (x) t_aoo_client(argc, argv);
    return x;
}

static int32_t aoo_node_sendto(i_node *node,
        const char *data, int32_t size, const ip_address *addr)
{
    return node->sendto(data, size, *addr);
}

t_aoo_client::t_aoo_client(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_client_tick);
    x_stateout = outlet_new(&x_obj, 0);
    x_msgout = outlet_new(&x_obj, 0);

    int port = argc ? atom_getfloat(argv) : 0;

    x_node = port > 0 ? i_node::get((t_pd *)this, port, 0) : nullptr;

    if (x_node){
        x_client.reset(aoo::net::iclient::create(
                       x_node, (aoo_sendfn)aoo_node_sendto, port));
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

        x_node->release((t_pd *)this);
    }

    if (x_client){
        x_client->quit();
        // wait for thread to finish
        x_thread.join();
    }

    // ignore pending requests (doesn't leak)

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
    class_addmethod(aoo_client_class, (t_method)aoo_client_list_peers,
                    gensym("list_peers"), A_NULL);
}
