/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

// AOO_NET is an embeddable UDP punch hole library for creating dynamic
// peer-2-peer networks over the public internet. It has been designed
// to seemlessly interoperate with the AOO streaming library.
//
// The implementation is largely based on the techniques described in the paper
// "Peer-to-Peer Communication Across Network Address Translators"
// by Ford, Srisuresh and Kegel (https://bford.info/pub/net/p2pnat/)
//
// It uses TCP over SLIP to reliable exchange meta information between peers.
//
// The UDP punch hole server runs on a public endpoint and manages the public
// and local IP endpoint addresses of all the clients.
// It can host multiple peer-2-peer networks which are organized as called groups.
//
// Each client connects to the server, logs in as a user, joins one ore more groups
// and in turn receives the public and local IP endpoint addresses from its peers.
//
// Currently, users and groups are automatically created on demand, but later
// we might add the possibility to create persistent users and groups on the server.
//
// Later we might add TCP connections between peers, so we can reliably exchange
// additional data, like chat messages or arbitrary OSC messages.
//
// Also we could support sending additional notifications from the server to all clients.

#pragma once

#include "aoo/aoo_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*///////////////////////// OSC ////////////////////////////////*/

#define AOO_NET_MSG_SERVER "/server"
#define AOO_NET_MSG_SERVER_LEN 7

#define AOO_NET_MSG_CLIENT "/client"
#define AOO_NET_MSG_CLIENT_LEN 7

#define AOO_NET_MSG_PEER "/peer"
#define AOO_NET_MSG_PEER_LEN 5

/*///////////////////////// requests/replies ///////////////////////////*/

typedef void (*aoo_net_callback)(void *user, int32_t result, const void *reply);

typedef struct aoo_net_error_reply {
    const char *errormsg;
    int32_t errorcode;
} aoo_net_error_reply;

typedef enum aoo_net_request_type {
    AOO_NET_CONNECT_REQUEST = 0,
    AOO_NET_DISCONNECT_REQUEST,
    AOO_NET_GROUP_JOIN_REQUEST,
    AOO_NET_GROUP_LEAVE_REQUEST
} aoo_net_request_type;

typedef struct aoo_net_connect_request {
    const char *host;
    int32_t port;
    const char *user_name;
    const char *user_pwd;
} aoo_net_connect_request;

typedef struct aoo_net_connect_reply {
    const void *public_address;
    const void *local_address;
    int32_t public_addrlen;
    int32_t local_addrlen;
    int32_t user_id;
} aoo_net_connect_reply;

typedef struct aoo_net_group_request {
    const char *group_name;
    const char *group_pwd;
} aoo_net_group_request;

/*///////////////////////// events ///////////////////////////*/

typedef enum aoo_net_event_type
{
    // generic events
    AOO_NET_ERROR_EVENT = 0,
    AOO_NET_PING_EVENT,
    // client events
    AOO_NET_DISCONNECT_EVENT,
    AOO_NET_PEER_JOIN_EVENT,
    AOO_NET_PEER_LEAVE_EVENT,
    AOO_NET_MESSAGE_EVENT,
    // server events
    AOO_NET_USER_JOIN_EVENT,
    AOO_NET_USER_LEAVE_EVENT,
    AOO_NET_GROUP_JOIN_EVENT,
    AOO_NET_GROUP_LEAVE_EVENT
} aoo_net_event_type;

typedef struct aoo_net_error_event
{
    int32_t type;
    int32_t errorcode;
    const char *errormsg;
} aoo_net_error_event;

#define AOO_NET_ENDPOINT_EVENT  \
    int32_t type;               \
    int32_t length;             \
    const void *address;        \

typedef struct aoo_net_ping_event
{
    AOO_NET_ENDPOINT_EVENT
    uint64_t tt1;
    uint64_t tt2;
    uint64_t tt3; // only for clients
} aoo_net_ping_event;

typedef struct aoo_net_peer_event
{
    AOO_NET_ENDPOINT_EVENT
    const char *group_name;
    const char *user_name;
    int32_t user_id;
} aoo_net_peer_event;

typedef aoo_net_peer_event aoo_net_group_event;

typedef struct aoo_net_message_event {
    AOO_NET_ENDPOINT_EVENT
    const char *data;
    int32_t size;
} aoo_net_peer_message_event;

typedef struct aoo_net_user_event
{
    AOO_NET_ENDPOINT_EVENT
    const char *user_name;
    int32_t user_id;
} aoo_net_user_event;

/*///////////////////////// AOO server /////////////////////////*/

#ifdef __cplusplus
namespace aoo {
namespace net {
    class iserver;
} // net
} // aoo
using aoo_net_server = aoo::net::iserver;
#else
typedef struct aoo_net_server aoo_net_server;
#endif

// create a new AOO server instance, listening on the given port
AOO_API aoo_net_server * aoo_net_server_new(int port, int32_t *err);

// destroy AOO server instance
AOO_API void aoo_net_server_free(aoo_net_server *server);

// run the AOO server; this function blocks indefinitely.
AOO_API int32_t aoo_net_server_run(aoo_net_server *server);

// quit the AOO server from another thread
AOO_API int32_t aoo_net_server_quit(aoo_net_server *server);

// get number of pending events (always thread safe)
AOO_API int32_t aoo_net_server_events_available(aoo_net_server *server);

// poll events (threadsafe, but not reentrant)
// will call the event handler function one or more times
AOO_API int32_t aoo_net_server_poll_events(aoo_net_server *server,
                                           aoo_eventhandler fn, void *user);

// LATER add methods to add/remove users and groups
// and set/get server options, group options and user options

/*///////////////////////// AOO client /////////////////////////*/

#ifdef __cplusplus
namespace aoo {
namespace net {
    class iclient;
} // net
} // aoo
using aoo_net_client = aoo::net::iclient;
#else
typedef struct aoo_net_client aoo_net_client;
#endif

// create a new AOO client for the given UDP socket
AOO_API aoo_net_client * aoo_net_client_new(int socket);

// destroy AOO client
AOO_API void aoo_net_client_free(aoo_net_client *client);

// run the AOO client; this function blocks indefinitely.
AOO_API int32_t aoo_net_client_run(aoo_net_client *client);

// quit the AOO client from another thread
AOO_API int32_t aoo_net_client_quit(aoo_net_client *client);

// send a request to the AOO server (always thread safe)
AOO_API int32_t aoo_net_client_request(aoo_net_client *client,
                                       aoo_net_request_type request, void *data,
                                       aoo_net_callback callback, void *user);

// send a message to one or more peers
// 'addr' + 'len' accept the following values:
// a) 'struct sockaddr *' + 'socklen_t': send to a single peer
// b) 'const char *' (group name) + 0: send to all peers of a specific group
// c) 'NULL' + 0: send to all peers
// the 'flags' parameter allows for (future) additional settings
AOO_API int32_t aoo_net_client_send_message(aoo_net_client *client,
                                            const char *data, int32_t n,
                                            const void *addr, int32_t len, int32_t flags);

// handle messages from peers (threadsafe, but not reentrant)
// 'addr' should be sockaddr *
AOO_API int32_t aoo_net_client_handle_message(aoo_net_client *client,
                                              const char *data, int32_t n,
                                              const void *addr, int32_t len);

// send outgoing messages to peers (threadsafe, but not reentrant)
AOO_API int32_t aoo_net_client_send(aoo_net_client *client);

// get number of pending events (always thread safe)
AOO_API int32_t aoo_net_client_events_available(aoo_net_client *client);

// handle events (threadsafe, but not reentrant)
// will call the event handler function one or more times
AOO_API int32_t aoo_net_client_poll_events(aoo_net_client *client,
                                           aoo_eventhandler fn, void *user);

// LATER add API functions to set options and do additional
// peer communication (chat, OSC messages, etc.)

// wrapper functions for frequently used requests

// connect to AOO server (always thread safe)
static inline int32_t aoo_net_client_connect(aoo_net_client *client,
                                             const char *host, int port,
                                             const char *name, const char *pwd,
                                             aoo_net_callback cb, void *user)
{
    aoo_net_connect_request data =  { host, port, name, pwd };
    return aoo_net_client_request(client, AOO_NET_CONNECT_REQUEST, &data, cb, user);
}

// disconnect from AOO server (always thread safe)
static inline int32_t aoo_net_client_disconnect(aoo_net_client *client,
                                                aoo_net_callback cb, void *user)
{
    return aoo_net_client_request(client, AOO_NET_DISCONNECT_REQUEST, NULL, cb, user);
}

// join an AOO group
static inline int32_t aoo_net_client_group_join(aoo_net_client *client,
                                                const char *group, const char *pwd,
                                                aoo_net_callback cb, void *user)
{
    aoo_net_group_request data = { group, pwd };
    return aoo_net_client_request(client, AOO_NET_GROUP_JOIN_REQUEST, &data, cb, user);
}

// leave an AOO group
static inline int32_t aoo_net_client_group_leave(aoo_net_client *client, const char *group,
                                                 aoo_net_callback cb, void *user)
{
    aoo_net_group_request data = { group, NULL };
    return aoo_net_client_request(client, AOO_NET_GROUP_LEAVE_REQUEST, &data, cb, user);
}

#ifdef __cplusplus
} // extern "C"
#endif
