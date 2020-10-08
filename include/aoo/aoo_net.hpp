/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "aoo/aoo_net.h"

#include <memory>

namespace aoo {
namespace net {

// NOTE: aoo::iserver and aoo::iclient don't define virtual destructors
// and have to be destroyed with their respective destroy() method.
// We provide a custom deleter and shared pointer to automate this task.
//
// The absence of a virtual destructor allows for ABI independent
// C++ interfaces on Windows (where the vtable layout is stable
// because of COM) and usually also on other platforms.
// (Compilers use different strategies for virtual destructors,
// some even put more than 1 entry in the vtable.)
// Also, we only use standard C types as function parameters
// and return types.
//
// In practice this means you only have to build 'aoo' once as a
// shared library and can then use its C++ interface in applications
// built with different compilers resp. compiler versions.
//
// If you want to be on the safe safe, use the C interface :-)

/*//////////////////////// AoO server ///////////////////////*/

class iserver {
public:
    class deleter {
    public:
        void operator()(iserver *x){
            destroy(x);
        }
    };
    // smart pointer for AoO source instance
    using pointer = std::unique_ptr<iserver, deleter>;

    // create a new AoO source instance
    static iserver * create(int port, int32_t *err);

    // destroy the AoO source instance
    static void destroy(iserver *server);

    // run the AOO server; this function blocks indefinitely.
    virtual int32_t run() = 0;

    // quit the AOO server from another thread
    virtual int32_t quit() = 0;

    // get number of pending events (always thread safe)
    virtual int32_t events_available() = 0;

    // handle events (threadsafe, but not reentrant)
    // will call the event handler function one or more times
    virtual int32_t handle_events(aoo_eventhandler fn, void *user) = 0;

    // LATER add methods to add/remove users and groups
    // and set/get server options, group options and user options
protected:
    ~iserver(){} // non-virtual!
};

inline iserver * iserver::create(int port, int32_t *err){
    return aoo_net_server_new(port, err);
}

inline void iserver::destroy(iserver *server){
    aoo_net_server_free(server);
}

/*//////////////////////// AoO client ///////////////////////*/

class iclient {
public:
    class deleter {
    public:
        void operator()(iclient *x){
            destroy(x);
        }
    };
    // smart pointer for AoO sink instance
    using pointer = std::unique_ptr<iclient, deleter>;

    // create a new AoO sink instance
    static iclient * create(void *socket, aoo_sendfn fn, int port);

    // destroy the AoO sink instance
    static void destroy(iclient *client);

    // run the AOO client; this function blocks indefinitely.
    virtual int32_t run() = 0;

    // quit the AOO client from another thread
    virtual int32_t quit() = 0;

    // send a request to the AOO server (always thread safe)
    virtual int32_t send_request(aoo_net_request_type request, void *data,
                                 aoo_net_callback callback, void *user) = 0;

    // handle messages from peers (thread safe, but not reentrant)
    // 'addr' should be sockaddr *
    virtual int32_t handle_message(const char *data, int32_t n,
                                   void *addr, int32_t len) = 0;

    // send outgoing messages to peers (thread safe, but not reentrant)
    virtual int32_t send() = 0;

    // get number of pending events (always thread safe)
    virtual int32_t events_available() = 0;

    // handle events (threadsafe, but not reentrant)
    // will call the event handler function one or more times
    virtual int32_t handle_events(aoo_eventhandler fn, void *user) = 0;

    // LATER add API functions to set options and do additional peer communication (chat, OSC messages, etc.)

    //------------------------------ requests ---------------------------//

    // connect to AOO server (always thread safe)
    int32_t connect(const char *host, int port,
                    const char *name, const char *pwd,
                    aoo_net_callback cb, void *user)
    {
        aoo_net_connect_request data =  { host, port, name, pwd };
        return send_request(AOO_NET_CONNECT_REQUEST, &data, cb, user);
    }

    // disconnect from AOO server (always thread safe)
    int32_t disconnect(aoo_net_callback cb, void *user)
    {
        return send_request(AOO_NET_DISCONNECT_REQUEST, NULL, cb, user);
    }

    // join an AOO group
    int32_t join_group(const char *group, const char *pwd,
                       aoo_net_callback cb, void *user)
    {
        aoo_net_group_request data = { group, pwd };
        return send_request(AOO_NET_GROUP_JOIN_REQUEST, &data, cb, user);
    }

    // leave an AOO group
    int32_t leave_group(const char *group, aoo_net_callback cb, void *user)
    {
        aoo_net_group_request data = { group, NULL };
        return send_request(AOO_NET_GROUP_LEAVE_REQUEST, &data, cb, user);
    }
protected:
    ~iclient(){} // non-virtual!
};

inline iclient * iclient::create(void *socket, aoo_sendfn fn, int port){
    return aoo_net_client_new(socket, fn, port);
}

inline void iclient::destroy(iclient *client){
    aoo_net_client_free(client);
}

} // net
} // aoo
