/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

// AOONET is an embeddable UDP punch hole library for creating dynamic
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

#include "aoo_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define AOONET_MSG_SERVER "/server"
#define AOONET_MSG_SERVER_LEN 7

#define AOONET_MSG_CLIENT "/client"
#define AOONET_MSG_CLIENT_LEN 7

#define AOONET_MSG_PEER "/peer"
#define AOONET_MSG_PEER_LEN 5

#define AOONET_MSG_PING "/ping"
#define AOONET_MSG_PING_LEN 5

#define AOONET_MSG_LOGIN "/login"
#define AOONET_MSG_LOGIN_LEN 6

#define AOONET_MSG_REQUEST "/request"
#define AOONET_MSG_REQUEST_LEN 8

#define AOONET_MSG_REPLY "/reply"
#define AOONET_MSG_REPLY_LEN 6

#define AOONET_MSG_GROUP "/group"
#define AOONET_MSG_GROUP_LEN 6

#define AOONET_MSG_JOIN "/join"
#define AOONET_MSG_JOIN_LEN 5

#define AOONET_MSG_LEAVE "/leave"
#define AOONET_MSG_LEAVE_LEN 6

#define AOONET_MSG_SERVER_PING AOO_MSG_DOMAIN AOONET_MSG_SERVER AOONET_MSG_PING
#define AOONET_MSG_CLIENT_PING AOO_MSG_DOMAIN AOONET_MSG_CLIENT AOONET_MSG_PING
#define AOONET_MSG_PEER_PING AOO_MSG_DOMAIN AOONET_MSG_PEER AOONET_MSG_PING

#define AOONET_MSG_SERVER_LOGIN AOO_MSG_DOMAIN AOONET_MSG_SERVER AOONET_MSG_LOGIN
#define AOONET_MSG_CLIENT_LOGIN AOO_MSG_DOMAIN AOONET_MSG_CLIENT AOONET_MSG_LOGIN

#define AOONET_MSG_SERVER_REQUEST AOO_MSG_DOMAIN AOONET_MSG_SERVER AOONET_MSG_REQUEST
#define AOONET_MSG_CLIENT_REPLY AOO_MSG_DOMAIN AOONET_MSG_CLIENT AOONET_MSG_REPLY

#define AOONET_MSG_SERVER_GROUP_JOIN AOO_MSG_DOMAIN AOONET_MSG_SERVER AOONET_MSG_GROUP AOONET_MSG_JOIN
#define AOONET_MSG_CLIENT_GROUP_JOIN AOO_MSG_DOMAIN AOONET_MSG_CLIENT AOONET_MSG_GROUP AOONET_MSG_JOIN

#define AOONET_MSG_SERVER_GROUP_LEAVE AOO_MSG_DOMAIN AOONET_MSG_SERVER AOONET_MSG_GROUP AOONET_MSG_LEAVE
#define AOONET_MSG_CLIENT_GROUP_LEAVE AOO_MSG_DOMAIN AOONET_MSG_CLIENT AOONET_MSG_GROUP AOONET_MSG_LEAVE

#define AOONET_MSG_CLIENT_PEER_JOIN AOO_MSG_DOMAIN AOONET_MSG_CLIENT AOONET_MSG_PEER AOONET_MSG_JOIN
#define AOONET_MSG_CLIENT_PEER_LEAVE AOO_MSG_DOMAIN AOONET_MSG_CLIENT AOONET_MSG_PEER AOONET_MSG_LEAVE

typedef enum aoonet_type {
    AOO_TYPE_SERVER = 1000,
    AOO_TYPE_CLIENT,
    AOO_TYPE_PEER
} aoonet_type;

/*///////////////////////// AOO server /////////////////////////*/

#ifdef __cplusplus
namespace aoo {
namespace net {
    class iserver;
} // net
} // aoo
using aoonet_server = aoo::net::iserver;
#else
typedef struct aoonet_server aoonet_server;
#endif

// create a new AOO server instance, listening on the given port
AOO_API aoonet_server * aoonet_server_new(int port, int32_t *err);

// destroy AOO server instance
AOO_API void aoonet_server_free(aoonet_server *server);

// run the AOO server; this function blocks indefinitely.
AOO_API int32_t aoonet_server_run(aoonet_server *server);

// quit the AOO server from another thread
AOO_API int32_t aoonet_server_quit(aoonet_server *server);

// get number of pending events (always thread safe)
AOO_API int32_t aoonet_server_events_available(aoonet_server *server);

// handle events (threadsafe, but not reentrant)
// will call the event handler function one or more times
AOO_API int32_t aoonet_server_handle_events(aoonet_server *server,
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
using aoonet_client = aoo::net::iclient;
#else
typedef struct aoonet_client aoonet_client;
#endif

// create a new AOO client for the given UDP socket
AOO_API aoonet_client * aoonet_client_new(void *udpsocket, aoo_sendfn fn, int port);

// destroy AOO client
AOO_API void aoonet_client_free(aoonet_client *client);

// run the AOO client; this function blocks indefinitely.
AOO_API int32_t aoonet_client_run(aoonet_client *server);

// quit the AOO client from another thread
AOO_API int32_t aoonet_client_quit(aoonet_client *server);

// connect AOO client to a AOO server (always thread safe)
AOO_API int32_t aoonet_client_connect(aoonet_client *client, const char *host, int port,
                                      const char *username, const char *pwd);

// disconnect AOO client from AOO server (always thread safe)
AOO_API int32_t aoonet_client_disconnect(aoonet_client *client);

// join an AOO group
AOO_API int32_t aoonet_client_group_join(aoonet_client *client, const char *group, const char *pwd);

// leave an AOO group
AOO_API int32_t aoonet_client_group_leave(aoonet_client *client, const char *group);

// handle messages from peers (threadsafe, but not reentrant)
// 'addr' should be sockaddr *
AOO_API int32_t aoonet_client_handle_message(aoonet_client *client,
                                             const char *data, int32_t n, void *addr);

// send outgoing messages to peers (threadsafe, but not reentrant)
AOO_API int32_t aoonet_client_send(aoonet_client *client);

// get number of pending events (always thread safe)
AOO_API int32_t aoonet_client_events_available(aoonet_client *client);

// handle events (threadsafe, but not reentrant)
// will call the event handler function one or more times
AOO_API int32_t aoonet_client_handle_events(aoonet_client *client,
                                            aoo_eventhandler fn, void *user);

// LATER add API functions to set options and do additional peer communication (chat, OSC messages, etc.)

#ifdef __cplusplus
} // extern "C"
#endif
