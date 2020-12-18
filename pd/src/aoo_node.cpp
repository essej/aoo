/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "common/sync.hpp"

#include <vector>
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

extern t_class *aoo_receive_class;

extern t_class *aoo_send_class;

extern t_class *aoo_client_class;

/*////////////////////// aoo node //////////////////*/

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

struct t_node final : public i_node
{
    t_node(t_symbol *s, int socket, int port);
    ~t_node();

    t_node_proxy x_proxy; // we can't directly bind t_node because of vtable
    t_symbol *x_bindsym;
    aoo::net::iclient::pointer x_client;
    t_pd * x_clientobj = nullptr;
    aoo::shared_mutex x_clientmutex;
    std::thread x_clientthread;
    int32_t x_refcount = 0;
    // socket
    int x_socket = -1;
    int x_port = 0;
    ip_address::ip_type x_type;
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
    void release(t_pd *obj, void *x) override;

    aoo::net::iclient * client() override { return x_client.get(); }

    ip_address::ip_type type() const override { return x_type; }

    int port() const override { return x_port; }

    void notify() override;

    void lock() override {
        x_clientmutex.lock();
    }

    void unlock() override {
        x_clientmutex.unlock();
    }
private:
    friend class i_node;

    bool add_object(t_pd *obj, void *x, aoo_id id);

    void do_send();

    void do_receive();
};

// public methods

void t_node::notify()
{
#if !AOO_NODE_POLL
    x_condition.notify_all();
#endif
}

// private methods

bool t_node::add_object(t_pd *obj, void *x, aoo_id id)
{
    aoo::scoped_lock lock(x_clientmutex);
    if (pd_class(obj) == aoo_client_class){
        // aoo_client
        if (!x_clientobj){
            x_clientobj = obj;
            // start thread lazily
            if (!x_clientthread.joinable()){
                x_clientthread = std::thread([this](){
                   x_client->run();
                });
            }
        } else {
            pd_error(obj, "%s on port %d already exists!",
                     classname(obj), x_port);
            return false;
        }
    } else  if (pd_class(obj) == aoo_send_class){
        if (x_client->add_source((aoo::isource *)x, id) != AOO_ERROR_OK){
            pd_error(obj, "%s with ID %d on port %d already exists!",
                     classname(obj), id, x_port);
        }
    } else if (pd_class(obj) == aoo_receive_class){
        if (x_client->add_sink((aoo::isink *)x, id) != AOO_ERROR_OK){
            pd_error(obj, "%s with ID %d on port %d already exists!",
                     classname(obj), id, x_port);
        }
    } else {
        bug("t_node: bad client");
        return false;
    }
    x_refcount++;
    return true;
}

void t_node::do_send()
{
    auto fn = [](void *user, const char *msg, int32_t n,
            const void *addr, int32_t len, uint32_t flags){
        auto x = (t_node *)user;
        ip_address dest((sockaddr *)addr, len);
        return socket_sendto(x->x_socket, msg, n, dest);
    };

    aoo::shared_scoped_lock lock(x_clientmutex);
    x_client->send(fn, this);
}

void t_node::do_receive()
{
    ip_address addr;
    char buf[AOO_MAXPACKETSIZE];
    int nbytes = socket_receive(x_socket, buf, AOO_MAXPACKETSIZE,
                                &addr, AOO_POLL_INTERVAL);
    if (nbytes > 0){
        aoo::shared_scoped_lock l(x_clientmutex);
        x_client->handle_message(buf, nbytes, addr.address(), addr.length());
        notify(); // !
    } else if (nbytes == 0){
        // timeout -> update client
        aoo::shared_scoped_lock lock(x_clientmutex);
        x_client->handle_message(nullptr, 0, nullptr, 0);
        notify(); // !
    } else {
        // ignore errors when quitting
        if (!x_quit){
            socket_error_print("recv");
        }
    }
}

i_node * i_node::get(t_pd *obj, int port, void *x, aoo_id id)
{
    t_node *node = nullptr;
    // make bind symbol for port number
    char buf[64];
    snprintf(buf, sizeof(buf), "aoo_node %d", port);
    t_symbol *s = gensym(buf);
    // find or create node
    auto y = (t_node_proxy *)pd_findbyclass(s, node_proxy_class);
    if (y){
        node = y->x_node;
    } else {
        // first create socket and bind to given port
        int sock = socket_udp(port);
        if (sock < 0){
            pd_error(obj, "%s: couldn't bind to port %d", classname(obj), port);
            return 0;
        }

        // increase send buffer size to 65 kB
        socket_setsendbufsize(sock, 2 << 15);
        // increase receive buffer size to 2 MB
        socket_setrecvbufsize(sock, 2 << 20);

        // finally create aoo node instance
        node = new t_node(s, sock, port);
    }

    if (!node->add_object(obj, x, id)){
        // never fails for new t_node!
        return nullptr;
    }

    return node;
}

t_node::t_node(t_symbol *s, int socket, int port)
    : x_proxy(this), x_bindsym(s),
      x_socket(socket), x_port(port)
{
    x_type = socket_family(socket);

    x_client.reset(aoo::net::iclient::create(socket, 0));

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

void t_node::release(t_pd *obj, void *x)
{
    aoo::unique_lock lock(x_clientmutex);
    if (pd_class(obj) == aoo_client_class){
        // client
        x_clientobj = nullptr;
    } else if (pd_class(obj) == aoo_send_class){
        x_client->remove_source((aoo::isource *)x);
    } else if (pd_class(obj) == aoo_receive_class){
        x_client->remove_sink((aoo::isink *)x);
    } else {
        bug("t_node::release");
        return;
    }
    lock.unlock(); // !

    if (--x_refcount == 0){
        // last instance
        delete this;
    } else if (x_refcount < 0){
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
    aoo::unique_lock lock(x_clientmutex);
    int didit = socket_signal(x_socket);
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

    if (x_clientthread.joinable()){
        x_client->quit();
        x_clientthread.join();
    }

    verbose(0, "released aoo node on port %d", x_port);
}

void aoo_node_setup(void)
{
    node_proxy_class = class_new(gensym("aoo node proxy"), 0, 0,
                                 sizeof(t_node_proxy), CLASS_PD, A_NULL);
}
