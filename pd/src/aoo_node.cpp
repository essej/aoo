/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#include "aoo_common.hpp"

#include "common/sync.hpp"
#include "common/time.hpp"
#include "common/utils.hpp"

#include <iostream>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <thread>
#include <atomic>

#define NETWORK_THREAD_POLL 0

#if NETWORK_THREAD_POLL
#define POLL_INTERVAL 0.001 // seconds
#endif

#define DEBUG_THREADS 0

extern t_class *aoo_receive_class;

extern t_class *aoo_send_class;

extern t_class *aoo_client_class;

struct t_aoo_client;

void aoo_client_handle_event(t_aoo_client *x, const AooEvent *event, int32_t level);

bool aoo_client_find_peer(t_aoo_client *x, const aoo::ip_address& addr,
                          t_symbol *& group, t_symbol *& user);

bool aoo_client_find_peer(t_aoo_client *x, t_symbol * group, t_symbol * user,
                          aoo::ip_address& addr);

/*////////////////////// aoo node //////////////////*/

static t_class *node_proxy_class;

class t_node_imp;

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
    AooClient::Ptr x_client;
    t_pd * x_clientobj = nullptr;
    int32_t x_refcount = 0;
    int x_port = 0;
    aoo::ip_address::ip_type x_type = aoo::ip_address::Unspec;
    bool x_ipv4mapped = true;
    // threading
    std::thread x_clientthread;
#if NETWORK_THREAD_POLL
    std::thread x_iothread;
    std::atomic<bool> x_quit{false};
#else
    std::thread x_sendthread;
    std::thread x_recvthread;
#endif
public:
    // public methods
    t_node_imp(t_symbol *s, int port);

    ~t_node_imp();

    void release(t_pd *obj, void *x) override;

    AooClient * client() override { return x_client.get(); }

    int port() const override { return x_port; }

    void notify() override {
        x_client->notify();
    }

    bool resolve(t_symbol *host, int port, aoo::ip_address& addr) const override;

    bool find_peer(const aoo::ip_address& addr,
                   t_symbol *& group, t_symbol *& user) const override;

    bool find_peer(t_symbol * group, t_symbol * user,
                   aoo::ip_address& addr) const override;

    int serialize_endpoint(const aoo::ip_address &addr, AooId id,
                           int argc, t_atom *argv) const override;

    bool add_object(t_pd *obj, void *x, AooId id);
private:
    void run_client();

#if NETWORK_THREAD_POLL
    void perform_io();
#else
    void send();

    void receive();
#endif
};

bool t_node_imp::resolve(t_symbol *host, int port, aoo::ip_address& addr) const {
    try {
        auto result = aoo::ip_address::resolve(host->s_name, port, x_type, x_ipv4mapped);
        assert(!result.empty());
        addr = result.front();
        return true;
    } catch (const aoo::resolve_error& e) {
        pd_error(nullptr, "%s", e.what());
        return false;
    }
}

bool t_node_imp::find_peer(const aoo::ip_address& addr,
                           t_symbol *& group, t_symbol *& user) const {
    if (x_clientobj &&
            aoo_client_find_peer(
                (t_aoo_client *)x_clientobj, addr, group, user)) {
        return true;
    } else {
        return false;
    }
}

bool t_node_imp::find_peer(t_symbol * group, t_symbol * user,
                           aoo::ip_address& addr) const {
    if (x_clientobj &&
            aoo_client_find_peer(
                (t_aoo_client *)x_clientobj, group, user, addr)) {
        return true;
    } else {
        return false;
    }
}

int t_node_imp::serialize_endpoint(const aoo::ip_address &addr, AooId id,
                                   int argc, t_atom *argv) const {
    if (argc < 3 || !addr.valid()) {
        LOG_DEBUG("serialize_endpoint: invalid address");
        return 0;
    }
    t_symbol *group, *user;
    if (find_peer(addr, group, user)) {
        // group name, user name, id
        SETSYMBOL(argv, group);
        SETSYMBOL(argv + 1, user);
        SETFLOAT(argv + 2, id);
    } else {
        // ip string, port number, id
        SETSYMBOL(argv, gensym(addr.name()));
        SETFLOAT(argv + 1, addr.port());
        SETFLOAT(argv + 2, id);
    }
    return 3;
}

// private methods

bool t_node_imp::add_object(t_pd *obj, void *x, AooId id)
{
    if (pd_class(obj) == aoo_client_class){
        // aoo_client
        if (!x_clientobj){
            x_clientobj = obj;
            // start thread lazily
            if (!x_clientthread.joinable()){
                x_clientthread = std::thread([this](){
                    aoo::sync::set_low_realtime_priority();
                    run_client();
                });
            }
        } else {
            pd_error(obj, "%s on port %d already exists!",
                     classname(obj), port());
            return false;
        }
    } else  if (pd_class(obj) == aoo_send_class){
        if (x_client->addSource((AooSource *)x) != kAooOk){
            pd_error(obj, "%s with ID %d on port %d already exists!",
                     classname(obj), id, port());
        }
    } else if (pd_class(obj) == aoo_receive_class){
        if (x_client->addSink((AooSink *)x) != kAooOk){
            pd_error(obj, "%s with ID %d on port %d already exists!",
                     classname(obj), id, port());
        }
    } else {
        bug("t_node_imp: bad client");
        return false;
    }
    x_refcount++;
    return true;
}

void t_node_imp::run_client() {
    auto err = x_client->run(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        sys_lock();
        pd_error(x_clientobj, "%s: TCP error: %s",
                 (x_clientobj ? "aoo_client" : "aoo"), msg.c_str());
        // TODO: handle error
        sys_unlock();
    }
}

#if NETWORK_THREAD_POLL
void t_node_imp::perform_io() {
    auto check_error = [this](auto err) {
        if (err != kAooOk && err != kAooErrorWouldBlock) {
            std::string msg;
            if (err == kAooErrorSocket) {
                msg = aoo::socket_strerror(aoo::socket_errno());
            } else {
                msg = aoo_strerror(err);
            }
            sys_lock();
            pd_error(x_clientobj, "%s: UDP error: %s",
                     (x_clientobj ? "aoo_client" : "aoo"), msg.c_str());
            // TODO: handle error
            sys_unlock();
            return false;
        } else {
            return true;
        }
    };

    while (!x_quit.load(std::memory_order_relaxed)) {
        auto t1 = aoo::time_tag::now();

        auto err1 = x_client->receive(0);
        if (!check_error(err1)) {
            break;
        }

        auto err2 = x_client->send(0);
        if (!check_error(err2)) {
            break;
        }

        if (err1 == kAooErrorWouldBlock && err2 == kAooErrorWouldBlock) {
            // sleep
            auto t2 = aoo::time_tag::now();
            auto remaining = aoo::time_tag::duration(t1, t2) - POLL_INTERVAL;
            auto dur = std::chrono::duration<double>(remaining);
            std::this_thread::sleep_for(dur);
        }
    }
}
#else
void t_node_imp::send() {
    auto err = x_client->send(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        sys_lock();
        pd_error(x_clientobj, "%s: UDP send error: %s",
                 (x_clientobj ? "aoo_client" : "aoo"), msg.c_str());
        // TODO: handle error
        sys_unlock();
    }
}

void t_node_imp::receive() {
    auto err = x_client->receive(kAooInfinite);
    if (err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        sys_lock();
        pd_error(x_clientobj, "%s: UDP receive error: %s",
                 (x_clientobj ? "aoo_client" : "aoo"), msg.c_str());
        // TODO: handle error
        sys_unlock();
    }
}
#endif

t_node * t_node::get(t_pd *obj, int port, void *x, AooId id)
{
    t_node_imp *node = nullptr;
    // make bind symbol for port number
    char buf[64];
    snprintf(buf, sizeof(buf), "aoo_node %d", port);
    t_symbol *s = gensym(buf);
    // find or create node
    auto y = (t_node_proxy *)pd_findbyclass(s, node_proxy_class);
    if (y) {
        node = y->x_node;
    } else {
        try {
            // finally create aoo node instance
            node = new t_node_imp(s, port);
        } catch (const std::exception& e) {
            pd_error(obj, "%s: %s", classname(obj), e.what());
            return nullptr;
        }
    }

    if (!node->add_object(obj, x, id)){
        // never fails for new t_node_imp!
        return nullptr;
    }

    return node;
}

t_node_imp::t_node_imp(t_symbol *s, int port)
    : x_proxy(this), x_bindsym(s), x_port(port)
{
    LOG_DEBUG("create AooClient on port " << port);
    auto client = AooClient::create();

    client->setEventHandler([](void *user, const AooEvent *event, AooThreadLevel level) {
        auto x = static_cast<t_node_imp *>(user);
        if (x->x_clientobj) {
            aoo_client_handle_event((t_aoo_client *)x->x_clientobj, event, level);
        }
    }, this, kAooEventModePoll);

    AooClientSettings settings;
    settings.portNumber = port;
    if (auto err = client->setup(settings); err != kAooOk) {
        std::string msg;
        if (err == kAooErrorSocket) {
            msg = aoo::socket::strerror(aoo::socket::get_last_error());
        } else {
            msg = aoo_strerror(err);
        }
        throw std::runtime_error("could not create node: " + msg);
    }
    // get socket type
    if (settings.socketType & kAooSocketIPv6) {
        x_type = aoo::ip_address::IPv6;
    } else {
        x_type = aoo::ip_address::IPv4;
    }
    x_ipv4mapped = settings.socketType & kAooSocketIPv4Mapped;

    // success
    x_client = std::move(client);

    pd_bind(&x_proxy.x_pd, x_bindsym);

#if NETWORK_THREAD_POLL
    // start network I/O thread
    LOG_DEBUG("start network thread");
    x_iothread = std::thread([this, pd=pd_this]() {
    #ifdef PDINSTANCE
        pd_setinstance(pd);
    #endif
        aoo::sync::set_low_realtime_priority();
        perform_io();
    });
#else
    // start send thread
    LOG_DEBUG("start network send thread");
    x_sendthread = std::thread([this, pd=pd_this]() {
    #ifdef PDINSTANCE
        pd_setinstance(pd);
    #endif
        aoo::sync::set_low_realtime_priority();
        send();
    });

    // start receive thread
    LOG_DEBUG("start network receive thread");
    x_recvthread = std::thread([this, pd=pd_this]() {
    #ifdef PDINSTANCE
        pd_setinstance(pd);
    #endif
        aoo::sync::set_low_realtime_priority();
        receive();
    });
#endif

    logpost(nullptr, PD_DEBUG, "aoo: new node on port %d", port);
}

void t_node_imp::release(t_pd *obj, void *x)
{
    if (pd_class(obj) == aoo_client_class){
        // client
        x_clientobj = nullptr;
    } else if (pd_class(obj) == aoo_send_class){
        x_client->removeSource((AooSource *)x);
    } else if (pd_class(obj) == aoo_receive_class){
        x_client->removeSink((AooSink *)x);
    } else {
        bug("t_node_imp::release");
        return;
    }

    if (--x_refcount == 0){
        // last instance
        delete this;
    } else if (x_refcount < 0){
        bug("t_node_imp::release: negative refcount!");
    }
}

t_node_imp::~t_node_imp()
{
    LOG_DEBUG("destroy AooClient on port " << x_port);
    pd_unbind(&x_proxy.x_pd, x_bindsym);

    // stop the client and join threads
    x_client->stop();
    if (x_clientthread.joinable()){
        x_clientthread.join();
    }
#if NETWORK_THREAD_POLL
    LOG_DEBUG("join network thread");
    // notify and join I/O thread
    x_quit.store(true);
    if (x_iothread.joinable()) {
        x_iothread.join();
    }
#else
    LOG_DEBUG("join network threads");
    // join both threads
    if (x_sendthread.joinable()) {
        x_sendthread.join();
    }
    if (x_recvthread.joinable()) {
        x_recvthread.join();
    }
#endif

    logpost(nullptr, PD_DEBUG, "aoo: released node on port %d", x_port);
}

void aoo_node_setup(void)
{
    node_proxy_class = class_new(gensym("aoo node proxy"), 0, 0,
                                 sizeof(t_node_proxy), CLASS_PD, A_NULL);
}
