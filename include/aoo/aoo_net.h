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

#define AOO_NET_MSG_PING "/ping"
#define AOO_NET_MSG_PING_LEN 5

#define AOO_NET_MSG_LOGIN "/login"
#define AOO_NET_MSG_LOGIN_LEN 6

#define AOO_NET_MSG_REQUEST "/request"
#define AOO_NET_MSG_REQUEST_LEN 8

#define AOO_NET_MSG_REPLY "/reply"
#define AOO_NET_MSG_REPLY_LEN 6

#define AOO_NET_MSG_GROUP "/group"
#define AOO_NET_MSG_GROUP_LEN 6

#define AOO_NET_MSG_JOIN "/join"
#define AOO_NET_MSG_JOIN_LEN 5

#define AOO_NET_MSG_LEAVE "/leave"
#define AOO_NET_MSG_LEAVE_LEN 6

/*///////////////////////// AOO events///////////////////////////*/

typedef enum aoo_net_event_type
{
    // client events
    AOO_NET_CLIENT_ERROR_EVENT = 0,
    AOO_NET_CLIENT_PING_EVENT,
    AOO_NET_CLIENT_CONNECT_EVENT,
    AOO_NET_CLIENT_DISCONNECT_EVENT,
    AOO_NET_CLIENT_GROUP_JOIN_EVENT,
    AOO_NET_CLIENT_GROUP_LEAVE_EVENT,
    AOO_NET_CLIENT_PEER_JOIN_EVENT,
    AOO_NET_CLIENT_PEER_LEAVE_EVENT,
    // server events
    AOO_NET_SERVER_ERROR_EVENT = 1000,
    AOO_NET_SERVER_PING_EVENT,
    AOO_NET_SERVER_USER_ADD_EVENT,
    AOO_NET_SERVER_USER_REMOVE_EVENT,
    AOO_NET_SERVER_USER_JOIN_EVENT,
    AOO_NET_SERVER_USER_LEAVE_EVENT,
    AOO_NET_SERVER_GROUP_ADD_EVENT,
    AOO_NET_SERVER_GROUP_REMOVE_EVENT,
    AOO_NET_SERVER_GROUP_JOIN_EVENT,
    AOO_NET_SERVER_GROUP_LEAVE_EVENT
} aoo_net_event_type;

#define AOO_NET_REPLY_EVENT  \
    int32_t type;           \
    int32_t result;         \
    const char *errormsg;   \

typedef struct aoo_net_reply_event
{
    AOO_NET_REPLY_EVENT
} aoo_net_reply_event;

#define aoo_net_server_event aoo_net_reply_event

typedef struct aoo_net_server_user_event
{
    int32_t type;
    const char *name;
} aoo_net_server_user_event;

typedef struct aoo_net_server_group_event
{
    int32_t type;
    const char *group;
    const char *user;
} aoo_net_server_group_event;

#define aoo_net_client_event aoo_net_reply_event

typedef struct aoo_net_client_group_event
{
    AOO_NET_REPLY_EVENT
    const char *name;
} aoo_net_client_group_event;

typedef struct aoo_net_client_peer_event
{
    AOO_NET_REPLY_EVENT
    const char *group;
    const char *user;
    void *address;
    int32_t length;
} aoo_net_client_peer_event;


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

// handle events (threadsafe, but not reentrant)
// will call the event handler function one or more times
AOO_API int32_t aoo_net_server_handle_events(aoo_net_server *server,
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
AOO_API aoo_net_client * aoo_net_client_new(void *udpsocket, aoo_sendfn fn, int port);

// destroy AOO client
AOO_API void aoo_net_client_free(aoo_net_client *client);

// run the AOO client; this function blocks indefinitely.
AOO_API int32_t aoo_net_client_run(aoo_net_client *server);

// quit the AOO client from another thread
AOO_API int32_t aoo_net_client_quit(aoo_net_client *server);

// connect AOO client to a AOO server (always thread safe)
AOO_API int32_t aoo_net_client_connect(aoo_net_client *client, const char *host, int port,
                                      const char *username, const char *pwd);

// disconnect AOO client from AOO server (always thread safe)
AOO_API int32_t aoo_net_client_disconnect(aoo_net_client *client);

// join an AOO group
AOO_API int32_t aoo_net_client_group_join(aoo_net_client *client, const char *group, const char *pwd);

// leave an AOO group
AOO_API int32_t aoo_net_client_group_leave(aoo_net_client *client, const char *group);

// handle messages from peers (threadsafe, but not reentrant)
// 'addr' should be sockaddr *
AOO_API int32_t aoo_net_client_handle_message(aoo_net_client *client, const char *data, int32_t n,
                                             void *addr, int32_t len);

// send outgoing messages to peers (threadsafe, but not reentrant)
AOO_API int32_t aoo_net_client_send(aoo_net_client *client);

// get number of pending events (always thread safe)
AOO_API int32_t aoo_net_client_events_available(aoo_net_client *client);

// handle events (threadsafe, but not reentrant)
// will call the event handler function one or more times
AOO_API int32_t aoo_net_client_handle_events(aoo_net_client *client,
                                            aoo_eventhandler fn, void *user);

// LATER add API functions to set options and do additional peer communication (chat, OSC messages, etc.)

#ifdef __cplusplus
} // extern "C"
#endif
