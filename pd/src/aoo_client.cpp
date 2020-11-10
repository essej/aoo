/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoo/aoo_net.hpp"

#include "common/sync.hpp"

#include "oscpack/osc/OscReceivedElements.h"

#include <thread>
#include <functional>
#include <vector>
#include <map>

using namespace aoo;

#define AOO_CLIENT_POLL_INTERVAL 2

t_class *aoo_client_class;

enum t_target
{
    TARGET_BROADCAST,
    TARGET_PEER,
    TARGET_GROUP,
    TARGET_NONE
};

#define DEJITTER 1

struct t_osc_message
{
    t_osc_message(const char *data, int32_t size,
                  const ip_address& address)
        : data_(data, size), address_(address) {}
    const char *data() const { return data_.data(); }
    int32_t size() const { return data_.size(); }
    const ip_address& address() const { return address_; }
private:
    std::string data_;
    ip_address address_;
};

struct t_aoo_client
{
    t_aoo_client(int argc, t_atom *argv);
    ~t_aoo_client();

    t_object x_obj;

    aoo::net::iclient::pointer x_client;
    i_node *x_node = nullptr;
    std::thread x_thread;

    // for OSC messages
    ip_address x_peer;
    t_symbol *x_group = nullptr;
    t_dejitter *x_dejitter = nullptr;
    t_float x_offset = -1; // immediately
    t_target x_target = TARGET_BROADCAST;
    bool x_connected = false;
    bool x_schedule = true;
    bool x_discard = false;
    std::multimap<float, t_osc_message> x_queue;

    void handle_peer_message(const char *data, int32_t size,
                             const ip_address& address, aoo::time_tag t);

    void handle_peer_bundle(const osc::ReceivedBundle& bundle,
                            const ip_address& address, aoo::time_tag t);

    void perform_message(const char *data, int32_t size,
                         const ip_address& address, double delay);

    void perform_message(const t_osc_message& msg){
        perform_message(msg.data(), msg.size(), msg.address(), 0);
    }

    void send_message(int argc, t_atom *argv,
                      const void *target, int32_t len);

    // replies
    using t_reply = std::function<void()>;
    std::vector<t_reply> replies_;
    aoo::shared_mutex reply_mutex_;
    void push_reply(t_reply reply){
        aoo::scoped_lock lock(reply_mutex_);
        replies_.push_back(std::move(reply));
    }

    t_clock *x_clock = nullptr;
    t_clock *x_queue_clock = nullptr;
    t_outlet *x_stateout = nullptr;
    t_outlet *x_msgout = nullptr;
    t_outlet *x_addrout = nullptr;
};

struct t_group_request {
    t_aoo_client *obj;
    t_symbol *group;
    t_symbol *pwd;
};

static void aoo_client_peer_list(t_aoo_client *x)
{
    if (x->x_node){
        x->x_node->list_peers(x->x_msgout);
    }
}

void aoo_client_send(t_aoo_client *x)
{
    x->x_client->send();
}

void aoo_client_handle_message(t_aoo_client *x, const char * data, int32_t n,
                               const ip_address& addr)
{
    x->x_client->handle_message(data, n, addr.address(), addr.length());
}

// send OSC messages to peers
void t_aoo_client::send_message(int argc, t_atom *argv,
                                const void *target, int32_t len)
{
    if (!argc){
        return;
    }
    if (!x_connected){
        pd_error(this, "%s: not connected", classname(this));
        return;
    }

    char *buf;
    int32_t count;

    // schedule OSC message as bundle (not needed for OSC bundles!)
    if (x_offset >= 0 && atom_getsymbol(argv)->s_name[0] != '#') {
        // make timetag relative to current OSC time
    #if DEJITTER
        aoo::time_tag now = get_osctime_dejitter(x_dejitter);
    #else
        aoo::time_tag now = get_osctime();
    #endif
        auto time = now + aoo::time_tag::from_seconds(x_offset * 0.001);

        const int headersize = 20; //#bundle string (8), timetag (8), message size (4)
        count = argc + headersize;
        buf = (char *)alloca(count);
        // make bundle header
        memcpy(buf, "#bundle\0", 8); // string length is exactly 8 bytes
        aoo::to_bytes((uint64_t)time, buf + 8);
        aoo::to_bytes((int32_t)argc, buf + 16);
        // add message to bundle
        for (int i = 0; i < argc; ++i){
            buf[i + headersize] = argv[i].a_type == A_FLOAT ? argv[i].a_w.w_float : 0;
        }
    } else {
        // send as is
        buf = (char *)alloca(argc);
        for (int i = 0; i < argc; ++i){
            buf[i] = argv[i].a_type == A_FLOAT ? argv[i].a_w.w_float : 0;
        }
        count = argc;
    }

    x_client->send_message(buf, count, target, len, 0);

    x_node->notify();
}

static void aoo_client_broadcast(t_aoo_client *x, t_symbol *s, int argc, t_atom *argv)
{
    if (x->x_node){
        x->send_message(argc, argv, 0, 0);
    }
}

static void aoo_client_send_group(t_aoo_client *x, t_symbol *s, int argc, t_atom *argv)
{
    if (x->x_node){
        if (argc > 1){
            if (argv->a_type == A_SYMBOL){
                auto group = argv->a_w.w_symbol;
                x->send_message(argc - 1, argv + 1, group->s_name, 0);
            }
        }
        pd_error(x, "%s: bad arguments to 'send_group' - expecting <group> <data...>",
                 classname(x));
    }
}

static void aoo_client_send_peer(t_aoo_client *x, t_symbol *s, int argc, t_atom *argv)
{
    if (x->x_node){
        ip_address address;
        if (get_peer_arg(x, x->x_node, argc, argv, address)){
            x->send_message(argc - 2, argv + 2, address.address(), address.length());
        }
    }
}

static void aoo_client_list(t_aoo_client *x, t_symbol *s, int argc, t_atom *argv)
{
    if (x->x_node){
        switch (x->x_target){
        case TARGET_PEER:
            x->send_message(argc, argv, x->x_peer.address(), x->x_peer.length());
            break;
        case TARGET_GROUP:
            x->send_message(argc, argv, x->x_group->s_name, 0);
            break;
        case TARGET_BROADCAST:
            x->send_message(argc, argv, 0, 0);
            break;
        default: // TARGET_NONE
            break;
        }
    }
}

static void aoo_client_offset(t_aoo_client *x, t_floatarg f)
{
    x->x_offset = f;
}

static void aoo_client_schedule(t_aoo_client *x, t_floatarg f)
{
    x->x_schedule = (f != 0);
}

static void aoo_client_discard_late(t_aoo_client *x, t_floatarg f)
{
    x->x_discard = (f != 0);
}

static void aoo_client_target(t_aoo_client *x, t_symbol *s, int argc, t_atom *argv)
{
    if (x->x_node){
        if (argc > 1){
            // <ip> <port> or <group> <peer>
            if (get_peer_arg(x, x->x_node, argc, argv, x->x_peer)){
                x->x_target = TARGET_PEER;
            } else {
                // this is important, so that we don't accidentally broadcast!
                x->x_target = TARGET_NONE;
            }
        } else if (argc == 1){
            // <group>
            if (argv->a_type == A_SYMBOL){
                x->x_target = TARGET_GROUP;
                x->x_group = argv->a_w.w_symbol;
            } else {
                pd_error(x, "%s: bad argument to 'target' message", classname(x));
                x->x_target = TARGET_NONE;
            }
        } else {
            x->x_target = TARGET_BROADCAST;
        }
    }
}

// handle incoming OSC message/bundle from peer
void t_aoo_client::perform_message(const char *data, int32_t size,
                                   const ip_address& address, double delay)
{
    // 1) peer + time tag
    t_atom info[3];
    address_to_atoms(address, 2, info);
    SETFLOAT(info + 2, delay);

    outlet_list(x_addrout, &s_list, 3, info);

    // 2) OSC message
    auto msg = (t_atom *)alloca(size * sizeof(t_atom));
    for (int i = 0; i < size; ++i){
        SETFLOAT(msg + i, (uint8_t)data[i]);
    }

    outlet_list(x_msgout, &s_list, size, msg);
}

static void aoo_client_queue_tick(t_aoo_client *x)
{
    auto& queue = x->x_queue;
    auto now = clock_getlogicaltime();

    for (auto it = queue.begin(); it != queue.end();){
        auto time = it->first;
        auto& msg = it->second;
        if (time <= now){
            x->perform_message(msg);
            it = queue.erase(it);
        } else {
            ++it;
        }
    }
    // reset clock
    if (!queue.empty()){
        // make sure update_jitter_offset() is called once per DSP tick!
        clock_set(x->x_queue_clock, queue.begin()->first);
    }
}



void t_aoo_client::handle_peer_message(const char *data, int32_t size,
                                       const ip_address& address, aoo::time_tag t)
{
    if (!t.is_immediate()){
        auto now = aoo::get_osctime();
        auto delay = aoo::time_tag::duration(now, t) * 1000.0;
        if (x_schedule){
            if (delay > 0){
                // put on queue and schedule on clock (using logical time)
                t_osc_message msg(data, size, address);
                auto abstime = clock_getsystimeafter(delay);
                auto pos = x_queue.emplace(abstime, std::move(msg));
                // only set clock if we're the first element in the queue
                if (pos == x_queue.begin()){
                    clock_set(x_queue_clock, abstime);
                }
            } else if (!x_discard){
                // treat like immediate message
                perform_message(data, size, address, 0);
            }
        } else {
            // output immediately with delay
            perform_message(data, size, address, delay);
        }
    } else {
        // send immediately
        perform_message(data, size, address, 0);
    }
}

void t_aoo_client::handle_peer_bundle(const osc::ReceivedBundle& bundle,
                                      const ip_address& address, aoo::time_tag t)
{
    auto it = bundle.ElementsBegin();
    while (it != bundle.ElementsEnd()){
        if (it->IsBundle()){
            osc::ReceivedBundle b(*it);
            handle_peer_bundle(b, address, b.TimeTag());
        } else {
            handle_peer_message(it->Contents(), it->Size(), address, t);
        }
        ++it;
    }
}

static void aoo_client_handle_event(t_aoo_client *x, const aoo_event *event)
{
    switch (event->type){
    case AOO_NET_MESSAGE_EVENT:
    {
        auto e = (const aoo_net_message_event *)event;

        ip_address address((const sockaddr *)e->address, e->length);

        try {
            osc::ReceivedPacket packet(e->data, e->size);
            if (packet.IsBundle()){
                osc::ReceivedBundle bundle(packet);
                x->handle_peer_bundle(bundle, address, bundle.TimeTag());
            } else {
                x->handle_peer_message(packet.Contents(), packet.Size(),
                                       address, aoo::time_tag::immediate());
            }
        } catch (const osc::Exception &err){
            pd_error(x, "%s: bad OSC message - %s", classname(x), err.what());
        }
        break;
    }
    case AOO_NET_DISCONNECT_EVENT:
    {
        post("%s: disconnected from server", classname(x));

        x->x_node->remove_all_peers();
        x->x_connected = false;

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

static void aoo_client_group_join(t_aoo_client *x, t_symbol *group, t_symbol *pwd);

static void aoo_client_connect(t_aoo_client *x, t_symbol *s, int argc, t_atom *argv)
{
    if (argc < 3){
        pd_error(x, "%s: too few arguments for '%s' method", classname(x), s->s_name);
        return;
    }
    if (x->x_client){
        // first remove peers (to be sure)
        x->x_node->remove_all_peers();

        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        t_symbol *userName = atom_getsymbol(argv + 2);
        t_symbol *userPwd = argc > 3 ? atom_getsymbol(argv + 3) : gensym("");
        t_symbol *group = argc > 4 ? atom_getsymbol(argv + 4) : nullptr;
        t_symbol *groupPwd = argc > 5 ? atom_getsymbol(argv + 5) : nullptr;

        // LATER also send user ID
        auto cb = [](void *x, int32_t result, const void *data){
            auto request = (t_group_request *)x;
            auto obj = request->obj;
            auto group = request->group;
            auto pwd = request->pwd;

            if (result == 0){
                obj->push_reply([obj, group, pwd](){
                    obj->x_connected = true;

                    outlet_float(obj->x_stateout, 1); // connected

                    if (group && pwd){
                        aoo_client_group_join(obj, group, pwd);
                    }
                });
            } else {
                auto reply = (const aoo_net_error_reply *)data;
                t_error_reply error { reply->errorcode, reply->errormsg };

                obj->push_reply([obj, error=std::move(error)](){
                    pd_error(obj, "%s: couldn't connect to server: %s",
                             classname(obj), error.msg.c_str());

                    if (!obj->x_connected){
                        outlet_float(obj->x_stateout, 0);
                    }
                });
            }

            delete request;
        };
        x->x_client->connect(host->s_name, port,
                             userName->s_name, userPwd->s_name, cb,
                             new t_group_request { x, group, groupPwd });
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
                    x->x_connected = false;

                    outlet_float(x->x_stateout, 0); // disconnected
                });
            } else {
                auto reply = (const aoo_net_error_reply *)data;
                t_error_reply error { reply->errorcode, reply->errormsg };

                x->push_reply([x, error=std::move(error)](){
                    pd_error(x, "%s: couldn't disconnect from server: %s",
                             classname(x), error.msg.c_str());
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
                                cb, new t_group_request { x, group, nullptr });
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
        x->x_client->leave_group(group->s_name, cb,
                                 new t_group_request { x, group, nullptr });
    }
}

static void * aoo_client_new(t_symbol *s, int argc, t_atom *argv)
{
    void *x = pd_new(aoo_client_class);
    new (x) t_aoo_client(argc, argv);
    return x;
}

t_aoo_client::t_aoo_client(int argc, t_atom *argv)
{
    x_clock = clock_new(this, (t_method)aoo_client_tick);
    x_queue_clock = clock_new(this, (t_method)aoo_client_queue_tick);
    x_stateout = outlet_new(&x_obj, 0);
    x_msgout = outlet_new(&x_obj, 0);
    x_addrout = outlet_new(&x_obj, 0);

    int port = argc ? atom_getfloat(argv) : 0;

    x_node = port > 0 ? i_node::get((t_pd *)this, port, 0) : nullptr;

    if (x_node){
        x_client.reset(aoo::net::iclient::create(x_node->socket()));
        if (x_client){
            verbose(0, "new aoo client on port %d", port);
            // get dejitter context
            x_dejitter = get_dejitter();
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
    clock_free(x_queue_clock);
}

void aoo_client_setup(void)
{
    aoo_client_class = class_new(gensym("aoo_client"), (t_newmethod)(void *)aoo_client_new,
        (t_method)aoo_client_free, sizeof(t_aoo_client), 0, A_GIMME, A_NULL);
    class_sethelpsymbol(aoo_client_class, gensym("aoo_net"));

    class_addmethod(aoo_client_class, (t_method)aoo_client_connect,
                    gensym("connect"), A_GIMME, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_disconnect,
                    gensym("disconnect"), A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_group_join,
                    gensym("group_join"), A_SYMBOL, A_SYMBOL, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_group_leave,
                    gensym("group_leave"), A_SYMBOL, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_peer_list,
                    gensym("peer_list"), A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_broadcast,
                    gensym("broadcast"), A_GIMME, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_send_peer,
                    gensym("send_peer"), A_GIMME, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_send_group,
                    gensym("send_group"), A_GIMME, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_target,
                    gensym("target"), A_GIMME, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_list,
                    gensym("send"), A_GIMME, A_NULL);
    class_addlist(aoo_client_class, aoo_client_list); // shortcut for "send"
    class_addmethod(aoo_client_class, (t_method)aoo_client_offset,
                    gensym("offset"), A_DEFFLOAT, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_schedule,
                    gensym("schedule"), A_FLOAT, A_NULL);
    class_addmethod(aoo_client_class, (t_method)aoo_client_discard_late,
                    gensym("discard_late"), A_FLOAT, A_NULL);
}
