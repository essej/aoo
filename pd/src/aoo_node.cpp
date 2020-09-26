/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoonet/aoonet.h"

#include "common/sync.hpp"

#include <vector>
#include <list>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>

using namespace aoo;

#ifndef AOO_NODE_POLL
 #define AOO_NODE_POLL 0
#endif

#define AOO_POLL_INTERVAL 1000 // microseconds

// aoo_receive

extern t_class *aoo_receive_class;

struct t_aoo_receive;

void aoo_receive_send(t_aoo_receive *x);

void aoo_receive_handle_message(t_aoo_receive *x, const char * data,
                                int32_t n, void *endpoint, aoo_replyfn fn);

void aoo_receive_update(t_aoo_receive *x);

// aoo_send

extern t_class *aoo_send_class;

struct t_aoo_send;

void aoo_send_send(t_aoo_send *x);

void aoo_send_handle_message(t_aoo_send *x, const char * data,
                             int32_t n, void *endpoint, aoo_replyfn fn);

// aoo_client

extern t_class *aoo_client_class;

struct t_aoo_client;

void aoo_client_send(t_aoo_client *x);

void aoo_client_handle_message(t_aoo_client *x, const char * data,
                               int32_t n, void *endpoint, aoo_replyfn fn);

/*////////////////////// aoo node //////////////////*/

struct t_client
{
    t_pd *c_obj;
    int32_t c_id;
};

struct t_peer
{
    t_symbol *group;
    t_symbol *user;
    aoo::endpoint *endpoint;
};

static t_class *node_proxy_class;

struct t_node;

struct t_node_proxy
{
    t_node_proxy(t_node *node){
        x_pd = node_proxy_class;
        x_node = node;
    }

    t_pd x_pd;
    t_node *x_node;
};

struct t_node : public i_node
{
    t_node(t_symbol *s, int socket, int port);
    ~t_node();

    t_node_proxy x_proxy; // we can't directly bind t_node because of vtable
    t_symbol *x_bindsym;
    // dependants
    std::vector<t_client> x_clients;
    aoo::shared_mutex x_clientlock;
    // peer list
    std::vector<t_peer> x_peers;
    // socket
    int x_socket = -1;
    int x_port = 0;
    std::list<aoo::endpoint> x_endpoints; // endpoints must not move in memory!
    aoo::shared_mutex x_endpointlock;
    // threading
#if AOO_NODE_POLL
    std::thread x_thread;
#else
    std::thread x_sendthread;
    std::thread x_receivethread;
    std::mutex x_mutex;
    std::condition_variable x_condition;
#endif
    std::atomic<bool> x_quit{false};

    // public methods
    void release(t_pd *obj) override;

    int socket() const override { return x_socket; }

    int port() const override { return x_port; }

    int sendto(const char *buf, int32_t size,
               const ip_address& addr) override
    {
        return socket_sendto(x_socket, buf, size, addr);
    }

    aoo::endpoint * get_endpoint(const ip_address& addr) override;

    aoo::endpoint * find_peer(t_symbol *group, t_symbol *user) override;

    void add_peer(t_symbol *group, t_symbol *user,
                  const ip_address& addr) override;

    void remove_peer(t_symbol *group, t_symbol *user) override;

    void remove_all_peers() override;

    void remove_group(t_symbol *group) override;

    void notify() override;

    // private methods
    bool add_client(t_pd *obj, int32_t id);

    aoo::endpoint* find_endpoint(const ip_address& addr);

    void do_send();

    void do_receive();
};

// public methods

aoo::endpoint * t_node::get_endpoint(const ip_address& addr)
{
    aoo::scoped_lock lock(x_endpointlock);
    auto ep = find_endpoint(addr);
    if (ep)
        return ep;
    else {
        // add endpoint
        x_endpoints.emplace_back(x_socket, addr);
        return &x_endpoints.back();
    }
}

aoo::endpoint * t_node::find_peer(t_symbol *group, t_symbol *user)
{
    for (auto& peer : x_peers){
        if (peer.group == group && peer.user == user){
            return peer.endpoint;
        }
    }
    return nullptr;
}

void t_node::add_peer(t_symbol *group, t_symbol *user,
                      const ip_address& addr)
{
    if (find_peer(group, user)){
        bug("t_node::add_peer: peer already added");
        return;
    }

    auto e = get_endpoint(addr);

    x_peers.push_back({ group, user, e });
}

void t_node::remove_peer(t_symbol *group, t_symbol *user)
{
    for (auto it = x_peers.begin(); it != x_peers.end(); ++it){
        if (it->group == group && it->user == user){
            x_peers.erase(it);
            return;
        }
    }
    bug("t_node::remove_peer: couldn't find peer");
}

void t_node::remove_group(t_symbol *group)
{
    for (auto it = x_peers.begin(); it != x_peers.end(); ) {
        // remove all peers matching group
        if (it->group == group){
            it = x_peers.erase(it);
        } else {
            ++it;
        }
    }
}

void t_node::remove_all_peers()
{
    x_peers.clear();
}

void t_node::notify()
{
#if !AOO_NODE_POLL
    x_condition.notify_all();
#endif
}

// private methods

bool t_node::add_client(t_pd *obj, int32_t id)
{
    // check client and add to list
    aoo::scoped_lock lock(x_clientlock);
#if 1
    for (auto& client : x_clients)
    {
        if (pd_class(obj) == pd_class(client.c_obj)
            && id == client.c_id)
        {
            if (obj == client.c_obj){
                bug("t_node::add_client: client already added!");
            } else {
                if (pd_class(obj) == aoo_client_class){
                    pd_error(obj, "%s on port %d already exists!",
                             classname(obj), x_port);
                } else {
                    pd_error(obj, "%s with ID %d on port %d already exists!",
                             classname(obj), id, x_port);
                }
            }
            return false;
        }
    }
#endif
    x_clients.push_back({ obj, id });
    return true;
}

aoo::endpoint * t_node::find_endpoint(const ip_address& addr)
{
    for (auto& e : x_endpoints){
        if (e.address() == addr){
            return &e;
        }
    }
    return nullptr;
}

void t_node::do_send()
{
    aoo::shared_scoped_lock lock(x_clientlock);

    for (auto& c : x_clients){
        if (pd_class(c.c_obj) == aoo_receive_class){
            aoo_receive_send((t_aoo_receive *)c.c_obj);
        } else if (pd_class(c.c_obj) == aoo_send_class){
            aoo_send_send((t_aoo_send *)c.c_obj);
        } else if (pd_class(c.c_obj) == aoo_client_class){
            aoo_client_send((t_aoo_client *)c.c_obj);
        } else {
            fprintf(stderr, "bug: t_node::do_send\n");
            fflush(stderr);
        }
    }
}

void t_node::do_receive()
{
    ip_address addr;
    char buf[AOO_MAXPACKETSIZE];
    int nbytes = socket_receive(x_socket, buf, AOO_MAXPACKETSIZE,
                                &addr, AOO_POLL_INTERVAL);
    if (nbytes > 0){
        // try to find endpoint
        aoo::unique_lock lock(x_endpointlock);
        auto ep = find_endpoint(addr);
        if (!ep){
            // add endpoint
            x_endpoints.emplace_back(x_socket, addr);
            ep = &x_endpoints.back();
        }
        lock.unlock();
        // get sink ID
        int32_t type, id;
        if ((aoo_parse_pattern(buf, nbytes, &type, &id) > 0)
            || (aoonet_parse_pattern(buf, nbytes, &type) > 0))
        {
            aoo::shared_scoped_lock l(x_clientlock);
            if (type == AOO_TYPE_SINK){
                // forward OSC packet to matching client(s)
                for (auto& c : x_clients){
                    if ((pd_class(c.c_obj) == aoo_receive_class) &&
                        ((id == AOO_ID_WILDCARD) || (id == c.c_id)))
                    {
                        t_aoo_receive *rcv = (t_aoo_receive *)c.c_obj;
                        aoo_receive_handle_message(rcv, buf, nbytes,
                                                   ep, endpoint::send);
                        if (id != AOO_ID_WILDCARD)
                            break;
                    }
                }
            } else if (type == AOO_TYPE_SOURCE){
                // forward OSC packet to matching senders(s)
                for (auto& c : x_clients){
                    if ((pd_class(c.c_obj) == aoo_send_class) &&
                        ((id == AOO_ID_WILDCARD) || (id == c.c_id)))
                    {
                        t_aoo_send *snd = (t_aoo_send *)c.c_obj;
                        aoo_send_handle_message(snd, buf, nbytes,
                            ep, endpoint::send);
                        if (id != AOO_ID_WILDCARD)
                            break;
                    }
                }
            } else if (type == AOO_TYPE_CLIENT || type == AOO_TYPE_PEER){
                // forward OSC packet to matching client
                for (auto& c :x_clients){
                    if (pd_class(c.c_obj) == aoo_client_class){
                        aoo_client_handle_message((t_aoo_client *)c.c_obj,
                                                  buf, nbytes, ep, endpoint::send);
                        break;
                    }
                }
            } else if (type == AOO_TYPE_SERVER){
                // ignore
            } else {
                fprintf(stderr, "bug: unknown aoo type\n");
                fflush(stderr);
            }
        #if !AOO_NODE_POLL
            // notify send thread
            x_condition.notify_all();
        #endif
        } else {
            // not a valid AoO OSC message
            fprintf(stderr, "aoo_node: not a valid AOO message!\n");
            fflush(stderr);
        }
    } else if (nbytes == 0){
        // timeout -> update clients
        aoo::shared_scoped_lock lock(x_clientlock);
        for (auto& c : x_clients){
            if (pd_class(c.c_obj) == aoo_receive_class){
                aoo_receive_update((t_aoo_receive *)c.c_obj);
            }
        }
    #if !AOO_NODE_POLL
        // notify send thread
        x_condition.notify_all();
    #endif
    } else {
        // ignore errors when quitting
        if (!x_quit){
            socket_error_print("recv");
        }
    }
}

i_node * i_node::get(t_pd *obj, int port, int32_t id)
{
    t_node *x = nullptr;
    // make bind symbol for port number
    char buf[64];
    snprintf(buf, sizeof(buf), "aoo_node %d", port);
    t_symbol *s = gensym(buf);
    // find or create node
    auto y = (t_node_proxy *)pd_findbyclass(s, node_proxy_class);
    if (y){
        x = y->x_node;
    } else {
        // first create socket
        int sock = socket_udp();
        if (sock < 0){
            socket_error_print("socket");
            return 0;
        }

        // bind socket to given port
        if (socket_bind(sock, port) < 0){
            pd_error(obj, "%s: couldn't bind to port %d", classname(obj), port);
            socket_close(sock);
            return 0;
        }

        // increase send buffer size to 65 kB
        socket_setsendbufsize(sock, 2 << 15);
        // increase receive buffer size to 2 MB
        socket_setrecvbufsize(sock, 2 << 20);

        // finally create aoo node instance
        x = new t_node(s, sock, port);
    }

    if (!x->add_client(obj, id)){
        // never fails for new t_node!
        return nullptr;
    }

    return x;
}

t_node::t_node(t_symbol *s, int socket, int port)
    : x_proxy(this), x_bindsym(s),
      x_socket(socket), x_port(port)
{
    pd_bind(&x_proxy.x_pd, x_bindsym);

    // start threads
#if AOO_NODE_POLL
    x_thread = std::thread([this](){
        lower_thread_priority();

        while (!x_quit){
            do_receive();
            do_send();
        }
    });
#else
    x_sendthread = std::thread([this](){
        lower_thread_priority();

        std::unique_lock<std::mutex> lock(x_mutex);
        while (!x_quit){
            x_condition.wait(lock);
            do_send();
        }
    });
    x_receivethread = std::thread([this](){
        lower_thread_priority();

        while (!x_quit){
            do_receive();
        }
    });
#endif

    verbose(0, "new aoo node on port %d", x_port);
}

void t_node::release(t_pd *obj)
{
    if (x_clients.size() > 1){
        // just remove client from list
        aoo::scoped_lock l(x_clientlock);
        for (auto it = x_clients.begin(); it != x_clients.end(); ++it){
            if (obj == it->c_obj){
                x_clients.erase(it);
                return;
            }
        }
        bug("t_node::release: %s not found!", classname(obj));
    } else if (x_clients.size() == 1){
        // last instance
        delete this;
    } else {
        bug("t_node::release: negative refcount!");
    }
}

t_node::~t_node()
{
    pd_unbind(&x_proxy.x_pd, x_bindsym);
    // tell the threads that we're done
#if AOO_NODE_POLL
    // don't bother waking up the thread...
    // just set the flag and wait
    x_quit = true;
    x_thread.join();

    socket_close(x_socket);
#else
    {
        std::lock_guard<std::mutex> l(x_mutex);
        x_quit = true;
    }

    // notify send thread
    x_condition.notify_all();

    // try to wake up receive thread
    aoo::unique_lock lock(x_clientlock);
    int didit = socket_signal(x_socket, x_port);
    if (!didit){
        // force wakeup by closing the socket.
        // this is not nice and probably undefined behavior,
        // the MSDN docs explicitly forbid it!
        socket_close(x_socket);
    }
    lock.unlock();

    // wait for threads
    x_sendthread.join();
    x_receivethread.join();

    if (didit){
        socket_close(x_socket);
    }
#endif

    verbose(0, "released aoo node on port %d", x_port);
}

void aoo_node_setup(void)
{
    node_proxy_class = class_new(gensym("aoo node proxy"), 0, 0,
                               sizeof(t_node_proxy), CLASS_PD, A_NULL);
}
