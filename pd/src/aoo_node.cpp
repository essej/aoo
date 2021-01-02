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

#define AOO_POLL_INTERVAL 1000 // microseconds

using namespace aoo;

extern t_class *aoo_receive_class;

extern t_class *aoo_send_class;

extern t_class *aoo_client_class;

void aoo_client_handle_event(struct t_aoo_client *x,
                             const aoo_event *event, int32_t level);

/*////////////////////// aoo node //////////////////*/

static t_class *node_proxy_class;

struct t_node_imp;

struct t_node_proxy
{
    t_node_proxy(t_node_imp *node){
        x_pd = node_proxy_class;
        x_node = node;
    }

    t_pd x_pd;
    t_node_imp *x_node;
};

class t_node_imp final : public t_node
{
    t_node_proxy x_proxy; // we can't directly bind t_node_imp because of vtable
    t_symbol *x_bindsym;
    aoo::net::client::pointer x_client;
    t_pd * x_clientobj = nullptr;
    std::mutex x_clientmutex;
    std::thread x_clientthread;
    int32_t x_refcount = 0;
    // socket
    int x_socket = -1;
    int x_port = 0;
    ip_address::ip_type x_type;
    // threading
    std::thread x_thread;
    std::atomic<bool> x_event{false};
    std::atomic<bool> x_quit{false};
public:
    // public methods
    t_node_imp(t_symbol *s, int socket, const ip_address& addr);

    ~t_node_imp();

    void release(t_pd *obj, void *x) override;

    aoo::net::client * client() override { return x_client.get(); }

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
    friend class t_node;

    static int32_t send(void *user, const char *msg, int32_t n,
                        const void *addr, int32_t len, uint32_t flags);

    void perform_network_io();

    bool add_object(t_pd *obj, void *x, aoo_id id);
};

// public methods

void t_node_imp::notify()
{
    x_event.store(true);
}

// private methods

bool t_node_imp::add_object(t_pd *obj, void *x, aoo_id id)
{
    std::lock_guard<std::mutex> lock(x_clientmutex);
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
            // set event handler
            x_client->set_eventhandler((aoo_eventhandler)aoo_client_handle_event,
                                       obj, AOO_EVENT_POLL);
        } else {
            pd_error(obj, "%s on port %d already exists!",
                     classname(obj), x_port);
            return false;
        }
    } else  if (pd_class(obj) == aoo_send_class){
        if (x_client->add_source((aoo::source *)x, id) != AOO_OK){
            pd_error(obj, "%s with ID %d on port %d already exists!",
                     classname(obj), id, x_port);
        }
    } else if (pd_class(obj) == aoo_receive_class){
        if (x_client->add_sink((aoo::sink *)x, id) != AOO_OK){
            pd_error(obj, "%s with ID %d on port %d already exists!",
                     classname(obj), id, x_port);
        }
    } else {
        bug("t_node_imp: bad client");
        return false;
    }
    x_refcount++;
    return true;
}

void t_node_imp::perform_network_io(){
    while (!x_quit){
        ip_address addr;
        char buf[AOO_MAXPACKETSIZE];
        int nbytes = socket_receive(x_socket, buf, AOO_MAXPACKETSIZE,
                                    &addr, AOO_POLL_INTERVAL);
        if (nbytes > 0){
            std::lock_guard<std::mutex> lock(x_clientmutex);
            x_client->handle_message(buf, nbytes, addr.address(), addr.length(),
                                     send, this);
        } else if (nbytes == 0){
            // timeout -> update client
            std::lock_guard<std::mutex> lock(x_clientmutex);
            x_client->update(send, this);
        } else {
            // ignore errors when quitting
            if (!x_quit){
                socket_error_print("recv");
            }
        }

        if (x_event.exchange(false, std::memory_order_acquire)){
            std::lock_guard<std::mutex> lock(x_clientmutex);
            x_client->update(send, this);
        }
    }
}

int32_t t_node_imp::send(void *user, const char *msg, int32_t n,
                         const void *addr, int32_t len, uint32_t flags)
{
    auto x = (t_node_imp *)user;
    ip_address dest((sockaddr *)addr, len);
    return socket_sendto(x->x_socket, msg, n, dest);
}

t_node * t_node::get(t_pd *obj, int port, void *x, aoo_id id)
{
    t_node_imp *node = nullptr;
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
            return nullptr;
        }

        ip_address addr;
        if (socket_address(sock, addr) != 0){
            pd_error(obj, "%s: couldn't get socket address", classname(obj));
            socket_close(sock);
            return nullptr;
        }

        // increase send buffer size to 65 kB
        socket_setsendbufsize(sock, 2 << 15);
        // increase receive buffer size to 2 MB
        socket_setrecvbufsize(sock, 2 << 20);

        // finally create aoo node instance
        node = new t_node_imp(s, sock, addr);
    }

    if (!node->add_object(obj, x, id)){
        // never fails for new t_node_imp!
        return nullptr;
    }

    return node;
}

t_node_imp::t_node_imp(t_symbol *s, int socket, const ip_address& addr)
    : x_proxy(this), x_bindsym(s),
      x_socket(socket), x_port(addr.port()), x_type(addr.type())
{
    x_client.reset(aoo::net::client::create(addr.address(), addr.length(), 0));

    pd_bind(&x_proxy.x_pd, x_bindsym);

    // start network thread
    x_thread = std::thread([this](){
        lower_thread_priority();

        perform_network_io();
    });

    verbose(0, "new aoo node on port %d", x_port);
}

void t_node_imp::release(t_pd *obj, void *x)
{
    std::unique_lock<std::mutex> lock(x_clientmutex);
    if (pd_class(obj) == aoo_client_class){
        // client
        x_clientobj = nullptr;
        x_client->set_eventhandler(nullptr, nullptr, AOO_EVENT_NONE);
    } else if (pd_class(obj) == aoo_send_class){
        x_client->remove_source((aoo::source *)x);
    } else if (pd_class(obj) == aoo_receive_class){
        x_client->remove_sink((aoo::sink *)x);
    } else {
        bug("t_node_imp::release");
        return;
    }
    lock.unlock(); // !

    if (--x_refcount == 0){
        // last instance
        delete this;
    } else if (x_refcount < 0){
        bug("t_node_imp::release: negative refcount!");
    }
}

t_node_imp::~t_node_imp()
{
    pd_unbind(&x_proxy.x_pd, x_bindsym);

    // tell the network thread that we're done
    x_quit = true;
    socket_signal(x_socket);

    x_thread.join();

    socket_close(x_socket);

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
