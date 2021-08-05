#pragma once

#include "aoo_net.h"
#include "aoo_events.h"
#include "aoo_controls.h"

typedef struct AooClient AooClient;

// create a new AOO client for the given local IP + port
AOO_API AooClient * AOO_CALL AooClient_new(
        const void *address, AooAddrSize addrlen,
        AooFlag flags, AooError *err);

// destroy AOO client
AOO_API void AOO_CALL AooClient_free(AooClient *client);

// run the AOO client; this function blocks until AooClient_quit() is called
AOO_API AooError AOO_CALL AooClient_run(AooClient *client);

// quit the AOO client from another thread
AOO_API AooError AOO_CALL AooClient_quit(AooClient *client);

// add AOO source
AOO_API AooError AOO_CALL AooClient_addSource(
        AooClient *client, struct AooSource *source, AooId id);

// remove AOO source
AOO_API AooError AOO_CALL AooClient_removeSource(
        AooClient *client, struct AooSource *source);

// add AOO sink
AOO_API AooError AOO_CALL AooClient_addSink(
        AooClient *client, struct AooSink *sink, AooId id);

// remove AOO sink
AOO_API AooError AOO_CALL AooClient_removeSink(
        AooClient *client, struct AooSink *sink);

// find peer of the given user + group name and return its IP endpoint address
// address: pointer to sockaddr_storage
// addrlen: initialized with max. storage size, updated to actual size
AOO_API AooError AOO_CALL AooClient_getPeerbyName(
        AooClient *client, const AooChar *group, const AooChar *user,
        void *address, AooAddrSize *addrlen);

// send a request to the AOO server (always thread safe)
AOO_API AooError AOO_CALL AooClient_sendRequest(
        AooClient *client, AooNetRequestType request, void *data,
        AooNetCallback callback, void *user);

// send a message to one or more peers
// 'target' + 'addrlen' accept the following values:
// a) 'struct sockaddr *' + 'socklen_t': send to a single peer
// b) 'const AooChar *' (group name) + 0: send to all peers of a specific group
// c) 'NULL' + 0: send to all peers
// 'flag' contains one or more values from AooNetMessageFlags

AOO_API AooError AOO_CALL AooClient_sendPeerMessage(
        AooClient *client, const AooByte *data, AooInt32 size,
        const void *target, AooAddrSize addrlen, AooFlag flags);

// handle messages from peers (threadsafe, called from a network thread)
// 'address' should be sockaddr *
AOO_API AooError AOO_CALL AooClient_handleMessage(
        AooClient *client, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen);

// send outgoing messages (threadsafe, called from a network thread)
AOO_API AooError AOO_CALL AooClient_send(
        AooClient *client, AooSendFunc fn, void *user);

// set event handler callback + mode
AOO_API AooError AOO_CALL AooClient_setEventHandler(
        AooClient *sink, AooEventHandler fn, void *user, AooEventMode mode);

// check for pending events (always thread safe)
AOO_API AooBool AOO_CALL AooClient_eventsAvailable(AooClient *client);

// poll events (threadsafe, but not reentrant).
// will call the event handler function one or more times.
// NOTE: the event handler must have been registered with kAooEventModePoll.
AOO_API AooError AOO_CALL AooClient_pollEvents(AooClient *client);

// client controls (always threadsafe)
AOO_API AooError AOO_CALL AooClient_control(
        AooClient *client, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

// ------------------------------------------------------------
// type-safe convenience functions for frequently used controls

// (empty)

// ------------------------------------------------------------
// type-safe convenience functions for frequently used requests

// connect to AOO server (always thread safe)
static inline AooError AooClient_connect(
        AooClient *client, const AooChar *hostName, AooInt32 port,
        const AooChar *userName, const AooChar *userPwd, AooNetCallback cb, void *user)
{
    AooNetRequestConnect data = { hostName, port, userName, userPwd };
    return AooClient_sendRequest(client, kAooNetRequestConnect, &data, cb, user);
}

// disconnect from AOO server (always thread safe)
static inline AooError AooClient_disconnect(
        AooClient *client, AooNetCallback cb, void *user)
{
    return AooClient_sendRequest(client, kAooNetRequestDisconnect, NULL, cb, user);
}

// join an AOO group
static inline AooError AooClient_joinGroup(
        AooClient *client, const AooChar *groupName, const AooChar *groupPwd,
        AooNetCallback cb, void *user)
{
    AooNetRequestJoinGroup data = { groupName, groupPwd, 0 };
    return AooClient_sendRequest(client, kAooNetRequestJoinGroup, &data, cb, user);
}

// leave an AOO group
static inline AooError AooClient_leaveGroup(
        AooClient *client, const AooChar *groupName, AooNetCallback cb, void *user)
{
    AooNetRequestLeaveGroup data = { groupName, 0, 0 };
    return AooClient_sendRequest(client, kAooNetRequestLeaveGroup, &data, cb, user);
}
