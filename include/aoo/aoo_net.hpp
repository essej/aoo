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
    // smart pointer for AoO server instance
    using pointer = std::unique_ptr<iserver, deleter>;

    // create a new AoO server instance
    static iserver * create(int port, uint32_t flags, aoo_error *err);

    // destroy the AoO server instance
    static void destroy(iserver *server);

    // run the AOO server; this function blocks indefinitely.
    virtual aoo_error run() = 0;

    // quit the AOO server from another thread
    virtual aoo_error quit() = 0;

    // set event handler callback + mode
    virtual aoo_error set_eventhandler(aoo_eventhandler fn,
                                       void *user, int32_t mode) = 0;

    // check for pending events (always thread safe)
    virtual aoo_bool events_available() = 0;

    // poll events (threadsafe, but not reentrant)
    // will call the event handler function one or more times
    virtual aoo_error poll_events() = 0;

    // LATER add methods to add/remove users and groups
    // and set/get server options, group options and user options
protected:
    ~iserver(){} // non-virtual!
};

inline iserver * iserver::create(int port, uint32_t flags, aoo_error *err){
    return aoo_net_server_new(port, flags, err);
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
    // smart pointer for AoO client instance
    using pointer = std::unique_ptr<iclient, deleter>;

    // create a new AoO client instance
    static iclient * create(const void *address, int32_t addrlen,
                            uint32_t flags);

    // destroy the AoO client instance
    static void destroy(iclient *client);

    // run the AOO client; this function blocks indefinitely.
    virtual aoo_error run() = 0;

    // quit the AOO client from another thread
    virtual aoo_error quit() = 0;

    // add AOO source
    virtual aoo_error add_source(aoo::isource *src, aoo_id id) = 0;

    // remove AOO source
    virtual aoo_error remove_source(aoo::isource *src) = 0;

    // add AOO sink
    virtual aoo_error add_sink(aoo::isink *src, aoo_id id) = 0;

    // remove AOO sink
    virtual aoo_error remove_sink(aoo::isink *src) = 0;

    // find peer and return its address
    // address: pointer to sockaddr_storage
    // addrlen: initialized with max. storage size, updated to actual size
    virtual aoo_error find_peer(const char *group, const char *user,
                                void *address, int32_t& addrlen) = 0;

    // send a request to the AOO server (always thread safe)
    virtual aoo_error send_request(aoo_net_request_type request, void *data,
                                   aoo_net_callback callback, void *user) = 0;

    // send a message to one or more peers
    // 'addr' + 'len' accept the following values:
    // a) 'struct sockaddr *' + 'socklen_t': send to a single peer
    // b) 'const char *' (group name) + 0: send to all peers of a specific group
    // c) 'NULL' + 0: send to all peers
    // the 'flags' parameter allows for (future) additional settings
    virtual aoo_error send_message(const char *data, int32_t n,
                                   const void *addr, int32_t len, int32_t flags) = 0;

    // handle messages from peers (thread safe, but not reentrant)
    // 'addr' should be sockaddr *
    virtual aoo_error handle_message(const char *data, int32_t n,
                                     const void *addr, int32_t len,
                                     aoo_sendfn fn, void *user) = 0;

    // update and send outgoing messages (threadsafe, called from the network thread)
    virtual aoo_error update(aoo_sendfn fn, void *user) = 0;

    // set event handler callback + mode
    virtual aoo_error set_eventhandler(aoo_eventhandler fn,
                                       void *user, int32_t mode) = 0;

    // check for pending events (always thread safe)
    virtual aoo_bool events_available() = 0;

    // poll events (threadsafe, but not reentrant)
    // will call the event handler function one or more times
    virtual aoo_error poll_events() = 0;

    // LATER add API functions to set options and do additional
    // peer communication (chat, OSC messages, etc.)

    //------------------------------ requests ---------------------------//

    // connect to AOO server (always thread safe)
    aoo_error connect(const char *host, int port,
                      const char *name, const char *pwd,
                      aoo_net_callback cb, void *user)
    {
        aoo_net_connect_request data =  { host, port, name, pwd };
        return send_request(AOO_NET_CONNECT_REQUEST, &data, cb, user);
    }

    // disconnect from AOO server (always thread safe)
    aoo_error disconnect(aoo_net_callback cb, void *user)
    {
        return send_request(AOO_NET_DISCONNECT_REQUEST, NULL, cb, user);
    }

    // join an AOO group
    aoo_error join_group(const char *group, const char *pwd,
                         aoo_net_callback cb, void *user)
    {
        aoo_net_group_request data = { group, pwd };
        return send_request(AOO_NET_GROUP_JOIN_REQUEST, &data, cb, user);
    }

    // leave an AOO group
    aoo_error leave_group(const char *group, aoo_net_callback cb, void *user)
    {
        aoo_net_group_request data = { group, NULL };
        return send_request(AOO_NET_GROUP_LEAVE_REQUEST, &data, cb, user);
    }
protected:
    ~iclient(){} // non-virtual!
};

inline iclient * iclient::create(const void *address, int32_t addrlen,
                                 uint32_t flags)
{
    return aoo_net_client_new(address, addrlen, flags);
}

inline void iclient::destroy(iclient *client){
    aoo_net_client_free(client);
}

} // net
} // aoo
