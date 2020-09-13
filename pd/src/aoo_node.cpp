/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "aoonet/aoonet.h"

#include "common/sync.hpp"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>

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

static void lower_thread_priority(void)
{
#ifdef _WIN32
    // lower thread priority only for high priority or real time processes
    DWORD cls = GetPriorityClass(GetCurrentProcess());
    if (cls == HIGH_PRIORITY_CLASS || cls == REALTIME_PRIORITY_CLASS){
        int priority = GetThreadPriority(GetCurrentThread());
        SetThreadPriority(GetCurrentThread(), priority - 2);
    }
#else

#endif
}

/*////////////////////// aoo node //////////////////*/

static t_class *aoo_node_class;

struct t_client
{
    t_pd *c_obj;
    int32_t c_id;
};

struct t_peer
{
    t_symbol *group;
    t_symbol *user;
    t_endpoint *endpoint;
};

struct t_node
{
    t_node();
    ~t_node();

    t_pd x_pd;
    t_symbol *x_sym;
    // dependants
    t_client *x_clients;
    int x_numclients; // doubles as refcount
    aoo::shared_mutex x_clientlock;
    // peers
    t_peer *x_peers;
    int x_numpeers;
    // socket
    int x_socket;
    int x_port;
    t_endpoint *x_endpoints;
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
    std::atomic<bool> x_quit; // should be atomic, but works anyway
};

t_endpoint * aoo_node_endpoint(t_node *x,
                               const struct sockaddr_storage *sa, socklen_t len)
{
    aoo::scoped_lock lock(x->x_endpointlock);
    t_endpoint *ep = endpoint_find(x->x_endpoints, sa);
    if (!ep){
        // add endpoint
        ep = endpoint_new(&x->x_socket, sa, len);
        ep->next = x->x_endpoints;
        x->x_endpoints = ep;
    }
    return ep;
}

static t_peer * aoo_node_dofind_peer(t_node *x, t_symbol *group, t_symbol *user)
{
    for (int i = 0; i < x->x_numpeers; ++i){
        t_peer *p = &x->x_peers[i];
        if (p->group == group && p->user == user){
            return p;
        }
    }
    return 0;
}

t_endpoint * aoo_node_find_peer(t_node *x, t_symbol *group, t_symbol *user)
{
    t_peer *p = aoo_node_dofind_peer(x, group, user);
    return p ? p->endpoint : 0;
}

void aoo_node_add_peer(t_node *x, t_symbol *group, t_symbol *user,
                       const struct sockaddr *sa, socklen_t len)
{
    if (aoo_node_dofind_peer(x, group, user)){
        bug("aoo_node_add_peer");
        return;
    }

    t_endpoint *e = aoo_node_endpoint(x, (const struct sockaddr_storage *)sa, len);

    if (x->x_peers){
        x->x_peers = (t_peer *)resizebytes(x->x_peers, x->x_numpeers * sizeof(t_peer),
                                 (x->x_numpeers + 1) * sizeof(t_peer));
    } else {
        x->x_peers = (t_peer *)getbytes(sizeof(t_peer));
    }
    t_peer *p = &x->x_peers[x->x_numpeers++];
    p->group = group;
    p->user = user;
    p->endpoint = e;
}

void aoo_node_remove_peer(t_node *x, t_symbol *group, t_symbol *user)
{
    t_peer *p = aoo_node_dofind_peer(x, group, user);
    if (!p){
        bug("aoo_node_remove_peer");
        return;
    }
    if (x->x_numpeers > 1){
        int index = p - x->x_peers;
        memmove(p, p + 1, (x->x_numpeers - index - 1) * sizeof(t_peer));
        x->x_peers = (t_peer *)resizebytes(x->x_peers, x->x_numpeers * sizeof(t_peer),
                                 (x->x_numpeers - 1) * sizeof(t_peer));
    } else {
        freebytes(x->x_peers, sizeof(t_peer));
        x->x_peers = 0;
    }
    x->x_numpeers--;
}

void aoo_node_remove_group(t_node *x, t_symbol *group)
{
    if (x->x_peers){
        // remove all sinks matching endpoint
        int n = x->x_numpeers;
        t_peer *end = x->x_peers + n;
        for (t_peer *p = x->x_peers; p != end; ){
            if (p->group == group){
                memmove(p, p + 1, (end - p - 1) * sizeof(t_peer));
                end--;
            } else {
                p++;
            }
        }
        int newsize = end - x->x_peers;
        if (newsize > 0){
            x->x_peers = (t_peer *)resizebytes(x->x_peers,
                n * sizeof(t_peer), newsize * sizeof(t_peer));
        } else {
            freebytes(x->x_peers, n * sizeof(t_peer));
            x->x_peers = 0;
        }
        x->x_numpeers = newsize;
    }
}

void aoo_node_remove_all_peers(t_node *x)
{
    if (x->x_peers){
        freebytes(x->x_peers, x->x_numpeers * sizeof(t_peer));
        x->x_peers = 0;
        x->x_numpeers = 0;
    }
}

int aoo_node_socket(t_node *x)
{
    return x->x_socket;
}

int aoo_node_port(t_node *x)
{
    return x->x_port;
}

void aoo_node_notify(t_node *x)
{
#if !AOO_NODE_POLL
    x->x_condition.notify_all();
#endif
}

int32_t aoo_node_sendto(t_node *x, const char *buf, int32_t size,
                        const struct sockaddr *addr)
{
    int result = socket_sendto(x->x_socket, buf, size, addr);
    return result;
}

void aoo_node_dosend(t_node *x)
{
    aoo::shared_scoped_lock lock(x->x_clientlock);

    for (int i = 0; i < x->x_numclients; ++i){
        t_client *c = &x->x_clients[i];
        if (pd_class(c->c_obj) == aoo_receive_class){
            aoo_receive_send((t_aoo_receive *)c->c_obj);
        } else if (pd_class(c->c_obj) == aoo_send_class){
            aoo_send_send((t_aoo_send *)c->c_obj);
        } else if (pd_class(c->c_obj) == aoo_client_class){
            aoo_client_send((t_aoo_client *)c->c_obj);
        } else {
            fprintf(stderr, "bug: aoo_node_send\n");
            fflush(stderr);
        }
    }
}

void aoo_node_doreceive(t_node *x)
{
    struct sockaddr_storage sa;
    socklen_t len;
    char buf[AOO_MAXPACKETSIZE];
    int nbytes = socket_receive(x->x_socket, buf, AOO_MAXPACKETSIZE,
                                &sa, &len, AOO_POLL_INTERVAL);
    if (nbytes > 0){
        // try to find endpoint
        aoo::unique_lock lock(x->x_endpointlock);
        t_endpoint *ep = endpoint_find(x->x_endpoints, &sa);
        if (!ep){
            // add endpoint
            ep = endpoint_new(&x->x_socket, &sa, len);
            ep->next = x->x_endpoints;
            x->x_endpoints = ep;
        }
        lock.unlock();
        // get sink ID
        int32_t type, id;
        if ((aoo_parse_pattern(buf, nbytes, &type, &id) > 0)
            || (aoonet_parse_pattern(buf, nbytes, &type) > 0))
        {
            aoo::shared_scoped_lock l(x->x_clientlock);
            if (type == AOO_TYPE_SINK){
                // forward OSC packet to matching receiver(s)
                for (int i = 0; i < x->x_numclients; ++i){
                    if ((pd_class(x->x_clients[i].c_obj) == aoo_receive_class) &&
                        ((id == AOO_ID_WILDCARD) || (id == x->x_clients[i].c_id)))
                    {
                        t_aoo_receive *rcv = (t_aoo_receive *)x->x_clients[i].c_obj;
                        aoo_receive_handle_message(rcv, buf, nbytes,
                            ep, (aoo_replyfn)endpoint_send);
                        if (id != AOO_ID_WILDCARD)
                            break;
                    }
                }
            } else if (type == AOO_TYPE_SOURCE){
                // forward OSC packet to matching senders(s)
                for (int i = 0; i < x->x_numclients; ++i){
                    if ((pd_class(x->x_clients[i].c_obj) == aoo_send_class) &&
                        ((id == AOO_ID_WILDCARD) || (id == x->x_clients[i].c_id)))
                    {
                        t_aoo_send *snd = (t_aoo_send *)x->x_clients[i].c_obj;
                        aoo_send_handle_message(snd, buf, nbytes,
                            ep, (aoo_replyfn)endpoint_send);
                        if (id != AOO_ID_WILDCARD)
                            break;
                    }
                }
            } else if (type == AOO_TYPE_CLIENT || type == AOO_TYPE_PEER){
                // forward OSC packet to matching client
                for (int i = 0; i < x->x_numclients; ++i){
                    if (pd_class(x->x_clients[i].c_obj) == aoo_client_class)
                    {
                        t_aoo_client *c = (t_aoo_client *)x->x_clients[i].c_obj;
                        aoo_client_handle_message(c, buf, nbytes,
                            ep, (aoo_replyfn)endpoint_send);
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
            x->x_condition.notify_all();
        #endif
        } else {
            // not a valid AoO OSC message
            fprintf(stderr, "aoo_node: not a valid AOO message!\n");
            fflush(stderr);
        }
    } else if (nbytes == 0){
        // timeout -> update receivers
        aoo::shared_scoped_lock lock(x->x_clientlock);
        for (int i = 0; i < x->x_numclients; ++i){
            if (pd_class(x->x_clients[i].c_obj) == aoo_receive_class){
                t_aoo_receive *rcv = (t_aoo_receive *)x->x_clients[i].c_obj;
                aoo_receive_update(rcv);
            }
        }
    #if !AOO_NODE_POLL
        // notify send thread
        x->x_condition.notify_all();
    #endif
    } else {
        // ignore errors when quitting
        if (!x->x_quit){
            socket_error_print("recv");
        }
    }
}

#if AOO_NODE_POLL
static void aoo_node_thread(t_node *x)
{
    lower_thread_priority();

    while (!x->x_quit){
        aoo_node_doreceive(x);
        aoo_node_dosend(x);
    }
}
#else
static void aoo_node_send(t_node *x)
{
    lower_thread_priority();

    std::unique_lock<std::mutex> lock(x->x_mutex);
    while (!x->x_quit){
        x->x_condition.wait(lock);

        aoo_node_dosend(x);
    }
}

static void aoo_node_receive(t_node *x)
{
    lower_thread_priority();

    while (!x->x_quit){
        aoo_node_doreceive(x);
    }
}
#endif // AOO_NODE_POLL

t_node* aoo_node_add(int port, t_pd *obj, int32_t id)
{
    // make bind symbol for port number
    char buf[64];
    snprintf(buf, sizeof(buf), "aoo_node %d", port);
    t_symbol *s = gensym(buf);
    t_client client = { obj, id };
    t_node *x = (t_node *)pd_findbyclass(s, aoo_node_class);
    if (x){
        // check receiver and add to list
        aoo::scoped_lock lock(x->x_clientlock);
    #if 1
        for (int i = 0; i < x->x_numclients; ++i){
            if (pd_class(obj) == pd_class(x->x_clients[i].c_obj)
                && id == x->x_clients[i].c_id)
            {
                if (obj == x->x_clients[i].c_obj){
                    bug("aoo_node_add: client already added!");
                } else {
                    if (pd_class(obj) == aoo_client_class){
                        pd_error(obj, "%s on port %d already exists!",
                                 classname(obj), port);
                    } else {
                        pd_error(obj, "%s with ID %d on port %d already exists!",
                                 classname(obj), id, port);
                    }
                }
                return 0;
            }
        }
    #endif
        x->x_clients = (t_client *)resizebytes(x->x_clients, sizeof(t_client) * x->x_numclients,
                                                sizeof(t_client) * (x->x_numclients + 1));
        x->x_clients[x->x_numclients] = client;
        x->x_numclients++;
    } else {
        // make new aoo node

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

        // now create aoo node instance
        x = (t_node *)getbytes(sizeof(t_node));
        x->x_pd = aoo_node_class;
        x->x_sym = s;
        pd_bind(&x->x_pd, s);

        // add receiver
        x->x_clients = (t_client *)getbytes(sizeof(t_client));
        x->x_clients[0] = client;
        x->x_numclients = 1;

        x->x_peers = 0;
        x->x_numpeers = 0;

        x->x_socket = sock;
        x->x_port = port;
        x->x_endpoints = 0;

        // start threads
        x->x_quit = 0;
    #if AOO_NODE_POLL
        x->x_thread = std::thread(aoo_node_thread, x);
    #else
        x->x_sendthread = std::thread(aoo_node_send, x);
        x->x_receivethread = std::thread(aoo_node_receive, x);
    #endif

        verbose(0, "new aoo node on port %d", x->x_port);
    }
    return x;
}

void aoo_node_release(t_node *x, t_pd *obj, int32_t id)
{
    if (x->x_numclients > 1){
        // just remove receiver from list
        aoo::scoped_lock l(x->x_clientlock);
        int n = x->x_numclients;
        for (int i = 0; i < n; ++i){
            if (obj == x->x_clients[i].c_obj){
                if (id != x->x_clients[i].c_id){
                    bug("aoo_node_remove: wrong ID!");
                    return;
                }
                memmove(&x->x_clients[i], &x->x_clients[i + 1], (n - i - 1) * sizeof(t_client));
                x->x_clients = (t_client *)resizebytes(x->x_clients, n * sizeof(t_client),
                                                        (n - 1) * sizeof(t_client));
                x->x_numclients--;
                return;
            }
        }
        bug("aoo_node_release: %s not found!", classname(obj));
    } else if (x->x_numclients == 1){
        // last instance
        pd_unbind(&x->x_pd, x->x_sym);

        // tell the threads that we're done
    #if AOO_NODE_POLL
        // don't bother waking up the thread...
        // just set the flag and wait
        x->x_quit = 1;
        pthread_join(x->x_thread, 0);

        socket_close(x->x_socket);
    #else
        {
            std::lock_guard<std::mutex> l(x->x_mutex);
            x->x_quit = 1;
        }

        // notify send thread
        x->x_condition.notify_all();

        // try to wake up receive thread
        aoo::unique_lock lock(x->x_clientlock);
        int didit = socket_signal(x->x_socket, x->x_port);
        if (!didit){
            // force wakeup by closing the socket.
            // this is not nice and probably undefined behavior,
            // the MSDN docs explicitly forbid it!
            socket_close(x->x_socket);
        }
        lock.unlock();

        // wait for threads
        x->x_sendthread.join();
        x->x_receivethread.join();

        if (didit){
            socket_close(x->x_socket);
        }
    #endif
        // free memory
        t_endpoint *e = x->x_endpoints;
        while (e){
            t_endpoint *next = e->next;
            endpoint_free(e);
            e = next;
        }
        if (x->x_clients)
            freebytes(x->x_clients, sizeof(t_client) * x->x_numclients);
        if (x->x_peers)
            freebytes(x->x_peers, sizeof(t_peer) * x->x_numpeers);

        verbose(0, "released aoo node on port %d", x->x_port);

        freebytes(x, sizeof(*x));
    } else {
        bug("aoo_node_release: negative refcount!");
    }
}

void aoo_node_setup(void)
{
    aoo_node_class = class_new(gensym("aoo socket receiver"), 0, 0,
                                  sizeof(t_node), CLASS_PD, A_NULL);
}
