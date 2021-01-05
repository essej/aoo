/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "common/lockfree.hpp"
#include "common/sync.hpp"

#include <iostream>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>


#define USE_NETWORK_THREAD 1

#if !USE_NETWORK_THREAD
#define POLL_INTERVAL 1000 // microseconds
#endif

#define DEBUG_THREADS 0

using namespace aoo;

extern t_class *aoo_receive_class;

extern t_class *aoo_send_class;

extern t_class *aoo_client_class;

struct t_aoo_client;

int aoo_client_resolve_address(const t_aoo_client *x, const ip_address& addr,
                               aoo_id id, int argc, t_atom *argv);

void aoo_client_handle_event(t_aoo_client *x,
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
    using unique_lock = sync::unique_lock<sync::mutex>;
    using scoped_lock = sync::scoped_lock<sync::mutex>;

    t_node_proxy x_proxy; // we can't directly bind t_node_imp because of vtable
    t_symbol *x_bindsym;
    aoo::net::client::pointer x_client;
    t_pd * x_clientobj = nullptr;
    aoo::sync::mutex x_clientmutex;
    std::thread x_clientthread;
    int32_t x_refcount = 0;
    // socket
    int x_socket = -1;
    int x_port = 0;
    ip_address::ip_type x_type;
    // threading
    std::thread x_recvthread;
#if USE_NETWORK_THREAD
    std::thread x_iothread;
    aoo::sync::event x_event;
#endif
    std::atomic<bool> x_update{false};
    std::atomic<bool> x_quit{false};
#if USE_NETWORK_THREAD
    struct packet {
        aoo::ip_address address;
        std::vector<char> data;
    };
    aoo::lockfree::unbounded_mpsc_queue<packet> x_recvbuffer;
#if DEBUG_THREADS
    std::atomic<int32_t> x_recvbufferfill{0};
#endif
#endif // USE_NETWORK_THREAD
public:
    // public methods
    t_node_imp(t_symbol *s, int socket, const ip_address& addr);

    ~t_node_imp();

    void release(t_pd *obj, void *x) override;

    aoo::net::client * client() override { return x_client.get(); }

    ip_address::ip_type type() const override { return x_type; }

    int port() const override { return x_port; }

    void notify() override {
        x_update.store(true);
    #if USE_NETWORK_THREAD
        x_event.set();
    #endif
    }

    void lock() override {
        x_clientmutex.lock();
    }

    void unlock() override {
        x_clientmutex.unlock();
    }

    bool get_sink_arg(t_pd *x, int argc, const t_atom *argv,
                      ip_address& addr, aoo_id &id) const override {
        return get_endpoint_arg(x, argc, argv, addr, &id, "sink");
    }

    bool get_source_arg(t_pd *x, int argc, const t_atom *argv,
                        ip_address& addr, aoo_id &id) const override {
        return get_endpoint_arg(x, argc, argv, addr, &id, "source");
    }

    bool get_peer_arg(t_pd *x, int argc, const t_atom *argv,
                      ip_address& addr) const override {
        return get_endpoint_arg(x, argc, argv, addr, nullptr, "peer");
    }

    int resolve_endpoint(const ip_address &addr, aoo_id id, int argc, t_atom *argv) const;
private:
    friend class t_node;

    static int32_t send(void *user, const char *msg, int32_t n,
                        const void *addr, int32_t len, uint32_t flags);

#if USE_NETWORK_THREAD
    void perform_io();
#endif

    void receive_packets();

    bool add_object(t_pd *obj, void *x, aoo_id id);

    bool get_endpoint_arg(t_pd *x, int argc, const t_atom *argv,
                          ip_address& addr, int32_t *id, const char *what) const;
};

// private methods

bool t_node_imp::get_endpoint_arg(t_pd *x, int argc, const t_atom *argv,
                                  ip_address& addr, int32_t *id, const char *what) const
{
    if (argc < (2 + (id != nullptr))){
        pd_error(x, "%s: too few arguments for %s", classname(x), what);
        return false;
    }

    // first try peer (group|user)
    if (argv[1].a_type == A_SYMBOL){
        t_symbol *group = atom_getsymbol(argv);
        t_symbol *user = atom_getsymbol(argv + 1);
        // we can't use length_ptr() because socklen_t != int32_t on many platforms
        int32_t len = aoo::ip_address::max_length;
        if (x_client->find_peer(group->s_name, user->s_name,
                                addr.address_ptr(), len) == AOO_OK) {
            *addr.length_ptr() = len;
        } else {
            pd_error(x, "%s: couldn't find peer %s|%s",
                     classname(x), group->s_name, user->s_name);
            return false;
        }
    } else {
        // otherwise try host|port
        t_symbol *host = atom_getsymbol(argv);
        int port = atom_getfloat(argv + 1);
        auto result = ip_address::resolve(host->s_name, port, x_type);
        if (!result.empty()){
            addr = result.front(); // just pick the first one
        } else {
            pd_error(x, "%s: couldn't resolve hostname '%s' for %s",
                     classname(x), host->s_name, what);
            return false;
        }
    }

    if (id){
        if (argv[2].a_type == A_FLOAT){
            aoo_id i = argv[2].a_w.w_float;
            if (i >= 0){
                *id = i;
            } else {
                pd_error(x, "%s: bad ID '%d' for %s",
                         classname(x), i, what);
                return false;
            }
        } else {
            pd_error(x, "%s: bad ID '%s' for %s", classname(x),
                     atom_getsymbol(argv + 2)->s_name, what);
            return false;
        }
    }

    return true;
}

int t_node_imp::resolve_endpoint(const ip_address &addr, aoo_id id,
                                  int argc, t_atom *argv) const {
    if (x_clientobj){
        return aoo_client_resolve_address((t_aoo_client *)x_clientobj,
                                          addr, id, argc, argv);
    } else {
        return endpoint_to_atoms(addr, id, argc, argv);
    }
}

bool t_node_imp::add_object(t_pd *obj, void *x, aoo_id id)
{
    scoped_lock lock(x_clientmutex);
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

#if USE_NETWORK_THREAD
void t_node_imp::receive_packets(){
    while (!x_quit){
        ip_address addr;
        char buf[AOO_MAXPACKETSIZE];
        int nbytes = socket_receive(x_socket, buf, AOO_MAXPACKETSIZE, &addr, -1);
        if (nbytes > 0){
            // add packet to queue
            x_recvbuffer.produce([&](packet& p){
                p.address = addr;
                p.data.assign(buf, buf + nbytes);
            });
        #if DEBUG_THREADS
            x_recvbufferfill.fetch_add(1, std::memory_order_relaxed);
        #endif
            x_event.set();
        } else if (nbytes < 0) {
            // ignore errors when quitting
            if (!x_quit){
                socket_error_print("recv");
            }
            break;
        }
        // ignore empty packets (used for quit signalling)
    #if DEBUG_THREADS
        std::cout << "receive_packets: waiting" << std::endl;
    #endif
    }
}

void t_node_imp::perform_io(){
    while (!x_quit){
        x_event.wait();

        const int32_t throttle = 10;
        int32_t count = 0;

        while (!x_recvbuffer.empty()){
            unique_lock lock(x_clientmutex);
        #if DEBUG_THREADS
            std::cout << "perform_io: handle_message" << std::endl;
        #endif
            x_recvbuffer.consume([&](const packet& p){
                x_client->handle_message(p.data.data(), p.data.size(),
                                         p.address.address(), p.address.length(),
                                         send, this);
            });
        #if DEBUG_THREADS
            auto fill = x_recvbufferfill.fetch_sub(1, std::memory_order_relaxed) - 1;
            std::cerr << "receive buffer fill: " << fill << std::endl;
        #endif
            // in case the receive buffer is never empty
            if (++count >= throttle){
                // relinquish client lock in case we're
                // blocking on get() or release()
                lock.unlock();
            #if DEBUG_THREADS
                std::cout << "perform_io: throttle" << std::endl;
            #endif
                lock.lock();

                x_client->update(send, this);

                count = 0;
            }
        }

        if (x_update.exchange(false, std::memory_order_acquire)){
            scoped_lock lock(x_clientmutex);
        #if DEBUG_THREADS
            std::cout << "perform_io: update" << std::endl;
        #endif
            x_client->update(send, this);
        }
    }
}
#else
void t_node_imp::receive_packets(){
    while (!x_quit){
        ip_address addr;
        char buf[AOO_MAXPACKETSIZE];
        int nbytes = socket_receive(x_socket, buf, AOO_MAXPACKETSIZE,
                                    &addr, POLL_INTERVAL);
        if (nbytes > 0){
            scoped_lock lock(x_clientmutex);
        #if DEBUG_THREADS
            std::cout << "receive_packets: handle_message" << std::endl;
        #endif
            x_client->handle_message(buf, nbytes, addr.address(), addr.length(),
                                     send, this);
        } else if (nbytes < 0) {
            // ignore errors when quitting
            if (!x_quit){
                socket_error_print("recv");
            }
            return;
        }

        if (x_update.exchange(false, std::memory_order_acquire)){
            scoped_lock lock(x_clientmutex);
        #if DEBUG_THREADS
            std::cout << "receive_packets: update" << std::endl;
        #endif
            x_client->update(send, this);
        }
    }
}
#endif

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

        // increase socket buffers
        const int sendbufsize = 1 << 16; // 65 KB
    #if USE_NETWORK_THREAD
        const int recvbufsize = 1 << 16; // 65 KB
    #else
        const int recvbufsize = 1 << 20; // 1 MB
    #endif
        socket_setsendbufsize(sock, sendbufsize);
        socket_setrecvbufsize(sock, recvbufsize);

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

    // start receive thread
    x_recvthread = std::thread([this](){
        sync::lower_thread_priority();
        receive_packets();
    });

#if USE_NETWORK_THREAD
    // start network thread
    x_iothread = std::thread([this](){
        sync::lower_thread_priority();
        perform_io();
    });
#endif

    verbose(0, "new aoo node on port %d", x_port);
}

void t_node_imp::release(t_pd *obj, void *x)
{
    unique_lock lock(x_clientmutex);
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

    // ask threads to quit
    x_quit = true;

#if USE_NETWORK_THREAD
    x_event.set(); // wake perform_io()
#endif
    socket_signal(x_socket); // wake receive_packets()

    x_recvthread.join();
#if USE_NETWORK_THREAD
    x_iothread.join();
#endif

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
