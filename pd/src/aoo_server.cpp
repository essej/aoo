/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoo_server.hpp"

#include "common/sync.hpp"
#include "common/utils.hpp"

#include <thread>

#define AOO_SERVER_POLL_INTERVAL 2

static t_class *aoo_server_class;

struct t_aoo_server
{
    t_aoo_server(int argc, t_atom *argv);
    ~t_aoo_server();

    t_object x_obj;

    AooServer::Ptr x_server;
    std::thread x_thread;
    std::thread x_udp_thread;
    int x_port = 0;
    int x_numclients = 0;
    t_clock *x_clock = nullptr;
    t_outlet *x_stateout = nullptr;
    t_outlet *x_msgout = nullptr;

    void close();

    void run();
    void receive();
};

void t_aoo_server::close() {
    if (x_port > 0) {
        x_server->stop();
        if (x_thread.joinable()) {
            x_thread.join();
        }
        if (x_udp_thread.joinable()) {
            x_udp_thread.join();
        }
    }
    clock_unset(x_clock);
    x_port = 0;
}

void t_aoo_server::run() {
    auto err = x_server->run(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        sys_lock();
        pd_error(this, "%s: server error: %s", classname(this), msg.c_str());
        // TODO: handle error
        sys_unlock();
    }
}

void t_aoo_server::receive() {
    auto err = x_server->receive(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        sys_lock();
        pd_error(this, "%s: UDP error: %s", classname(this), msg.c_str());
        // TODO: handle error
        sys_unlock();
    }
}

static void aoo_server_handle_event(t_aoo_server *x, const AooEvent *event, int32_t)
{
    switch (event->type) {
    case kAooEventClientLogin:
    {
        auto& e = event->clientLogin;

        // TODO: address + metadata
        t_atom msg;
        char id[64];
        snprintf(id, sizeof(id), "0x%X", e.id);
        SETSYMBOL(&msg, gensym(id));

        if (e.error == kAooOk) {
            outlet_anything(x->x_msgout, gensym("client_add"), 1, &msg);

            x->x_numclients++;

            outlet_float(x->x_stateout, x->x_numclients);
        } else {
            pd_error(x, "%s: client %d failed to login: %s",
                     classname(x), e.id, aoo_strerror(e.error));
        }

        break;
    }
    case kAooEventClientLogout:
    {
        auto& e = event->clientLogout;

        if (e.errorCode != kAooOk) {
            pd_error(x, "%s: client error: %s", classname(x), e.errorMessage);
        }

        // TODO: address
        t_atom msg;
        char id[64];
        snprintf(id, sizeof(id), "0x%X", e.id);
        SETSYMBOL(&msg, gensym(id));

        outlet_anything(x->x_msgout, gensym("client_remove"), 1, &msg);

        x->x_numclients--;

        if (x->x_numclients >= 0) {
            outlet_float(x->x_stateout, x->x_numclients);
        } else {
            bug("kAooEventClientLogout");
            x->x_numclients = 0;
        }

        break;
    }
    case kAooEventGroupAdd:
    {
        // TODO add group
        t_atom msg;
        SETSYMBOL(&msg, gensym(event->groupAdd.name));
        // TODO metadata
        outlet_anything(x->x_msgout, gensym("group_add"), 1, &msg);

        break;
    }
    case kAooEventGroupRemove:
    {
        // TODO remove group
        t_atom msg;
        SETSYMBOL(&msg, gensym(event->groupRemove.name));
        outlet_anything(x->x_msgout, gensym("group_remove"), 1, &msg);

        break;
    }
    case kAooEventGroupJoin:
    {
        auto& e = event->groupJoin;

        t_atom msg[3];
        SETSYMBOL(msg, gensym(e.groupName));
        SETSYMBOL(msg + 1, gensym(e.userName));
        SETFLOAT(msg + 2, e.userId); // always small
        outlet_anything(x->x_msgout, gensym("group_join"), 3, msg);

        break;
    }
    case kAooEventGroupLeave:
    {
        auto& e = event->groupLeave;

        t_atom msg[3];
        SETSYMBOL(msg, gensym(e.groupName));
        SETSYMBOL(msg + 1, gensym(e.userName));
        SETFLOAT(msg + 2, e.userId); // always small
        outlet_anything(x->x_msgout, gensym("group_leave"), 3, msg);

        break;
    }
    default:
        logpost(x, PD_DEBUG, "%s: unknown event type %d",
                classname(x), event->type);
        break;
    }
}

static void aoo_server_tick(t_aoo_server *x)
{
    x->x_server->pollEvents();
    clock_delay(x->x_clock, AOO_SERVER_POLL_INTERVAL);
}

static void aoo_server_relay(t_aoo_server *x, t_floatarg f) {
    x->x_server->setUseInternalRelay(f != 0);
}

static void aoo_server_port(t_aoo_server *x, t_floatarg f)
{
    int port = f;
    if (port == x->x_port) {
        return;
    }

    x->close();

    x->x_numclients = 0;
    outlet_float(x->x_stateout, 0);

    if (port > 0) {
        AooServerSettings settings;
        settings.portNumber = port;
        if (auto err = x->x_server->setup(settings); err != kAooOk) {
            std::string msg;
            if (err == kAooErrorSocket) {
                msg = aoo::socket::strerror(aoo::socket::get_last_error());
            } else {
                msg = aoo_strerror(err);
            }
            pd_error(x, "%s: setup failed: %s", classname(x), msg.c_str());
            return;
        }
        // start server threads
        x->x_thread = std::thread([x, pd=pd_this]() {
            aoo::sync::set_low_realtime_priority();
        #ifdef PDINSTANCE
            pd_setinstance(pd);
        #endif
            x->run();
        });
        x->x_udp_thread = std::thread([x, pd=pd_this]() {
            aoo::sync::set_low_realtime_priority();
#ifdef PDINSTANCE
            pd_setinstance(pd);
#endif
            x->receive();
        });
        // start clock
        clock_delay(x->x_clock, AOO_SERVER_POLL_INTERVAL);

        x->x_port = port;
    }
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

    x_server = AooServer::create(); // does not really fail...

    // first set event handler!
    x_server->setEventHandler((AooEventHandler)aoo_server_handle_event,
                              this, kAooEventModePoll);

    int port = atom_getfloatarg(0, argc, argv);
    aoo_server_port(this, port);
}

static void aoo_server_free(t_aoo_server *x)
{
    x->~t_aoo_server();
}

t_aoo_server::~t_aoo_server()
{
    close();
    clock_free(x_clock);
}

void aoo_server_setup(void)
{
    aoo_server_class = class_new(gensym("aoo_server"), (t_newmethod)(void *)aoo_server_new,
        (t_method)aoo_server_free, sizeof(t_aoo_server), 0, A_GIMME, A_NULL);
    class_addmethod(aoo_server_class, (t_method)aoo_server_relay,
         gensym("relay"), A_FLOAT, A_NULL);
    class_addmethod(aoo_server_class, (t_method)aoo_server_port,
         gensym("port"), A_FLOAT, A_NULL);
}
