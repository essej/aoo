/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C interface for AOO client
 */

#pragma once

#include "aoo_net.h"
#include "aoo_events.h"
#include "aoo_controls.h"

typedef struct AooClient AooClient;

/** \brief create a new AOO source instance
 *
 * \param address local UDP socket address
 * \param addrlen socket address length
 * \param flags optional flags
 * \param[out] err error code on failure
 * \return new AooClient instance on success; `NULL` on failure
 */
AOO_API AooClient * AOO_CALL AooClient_new(
        const void *address, AooAddrSize addrlen,
        AooFlag flags, AooError *err);

/** \brief destroy AOO client */
AOO_API void AOO_CALL AooClient_free(AooClient *client);

/** \brief run the AOO client
 *
 * This function blocks until AooClient_quit() is called.
 */
AOO_API AooError AOO_CALL AooClient_run(AooClient *client);

/** \brief quit the AOO client from another thread */
AOO_API AooError AOO_CALL AooClient_quit(AooClient *client);

/** \brief add AOO source
 *
 * \param client the AOO client
 * \param source the AOO source
 * \param id the AOO source ID
 */
AOO_API AooError AOO_CALL AooClient_addSource(
        AooClient *client, struct AooSource *source, AooId id);

/** \brief remove AOO source */
AOO_API AooError AOO_CALL AooClient_removeSource(
        AooClient *client, struct AooSource *source);

/** \brief add AOO sink
 *
 * \param client the AOO client
 * \param sink the AOO sink
 * \param id the AOO sink ID
 */
AOO_API AooError AOO_CALL AooClient_addSink(
        AooClient *client, struct AooSink *sink, AooId id);

/** \brief remove AOO sink */
AOO_API AooError AOO_CALL AooClient_removeSink(
        AooClient *client, struct AooSink *sink);

/** \brief find peer by name
 *
 * Find peer of the given user + group name and return its IP endpoint address
 * \param client the AOO client
 * \param group the group name
 * \param user the user name
 * \param address socket address storage, i.e. pointer to `sockaddr_storage` struct
 * \param addrlen socket address storage length;
 *        initialized with max. storage size, updated to actual size
 */
AOO_API AooError AOO_CALL AooClient_getPeerByName(
        AooClient *client, const AooChar *group, const AooChar *user,
        void *address, AooAddrSize *addrlen);

/** \brief send a request to the AOO server
 *
 * \note Threadsafe
 *
 * \param client the AOO client
 * \param request the request type
 * \param data (optional) request data
 * \param callback function to be called back when response has arrived
 * \param user user data passed to callback function
 */
AOO_API AooError AOO_CALL AooClient_sendRequest(
        AooClient *client, AooNetRequestType request, void *data,
        AooNetCallback callback, void *user);

/** \brief send a message to one or more peers
 *
 * `target` + `addrlen` accept the following values:
 * a) `struct sockaddr *` + `socklen_t`: send to a single peer
 * b) `const AooChar *` (group name) + 0: send to all peers of a specific group
 * c) `NULL' + 0: send to all peers
 *
 * \param client the AOO client
 * \param data the message data
 * \param size the message size in bytes
 * \param target the message target (see above)
 * \param addrlen the socket address length
 * \param flags contains one or more values from AooNetMessageFlags
 */
AOO_API AooError AOO_CALL AooClient_sendPeerMessage(
        AooClient *client, const AooByte *data, AooInt32 size,
        const void *target, AooAddrSize addrlen, AooFlag flags);

/** \brief handle messages from peers
 *
 * \note Threadsafe, but not reentrant; call on the network thread
 *
 * \param client the AOO client
 * \param data the message data
 * \param size the message size
 * \param address the remote socket address
 * \param addrlen the socket address length
 */
AOO_API AooError AOO_CALL AooClient_handleMessage(
        AooClient *client, const AooByte *data, AooInt32 size,
        const void *address, AooAddrSize addrlen);

/** \brief send outgoing messages
 *
 * \note Threadsafe; call on the network thread
 *
 * \param sink the AOO sink
 * \param fn the send function
 * \param user the user data (passed to the send function)
 */
AOO_API AooError AOO_CALL AooClient_send(
        AooClient *client, AooSendFunc fn, void *user);

/** \brief set event handler function and event handling mode
 *
 * \warning Not threadsafe - only call in the beginning! */
AOO_API AooError AOO_CALL AooClient_setEventHandler(
        AooClient *sink, AooEventHandler fn, void *user, AooEventMode mode);

/** \brief check for pending events
 *
 * \note Threadsafe and RT-safe */
AOO_API AooBool AOO_CALL AooClient_eventsAvailable(AooClient *client);

/** \brief poll events
 *
 * \note Threadsafe and RT-safe, but not reentrant.
 *
 * This function will call the registered event handler one or more times.
 * \attention The event handler must have been registered with #kAooEventModePoll.
 */
AOO_API AooError AOO_CALL AooClient_pollEvents(AooClient *client);

/** \brief control interface
 *
 * used internally by helper functions for specific controls */
AOO_API AooError AOO_CALL AooClient_control(
        AooClient *client, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

/*--------------------------------------------*/
/*         type-safe control functions        */
/*--------------------------------------------*/

/* (empty) */

/*--------------------------------------------*/
/*         type-safe request functions        */
/*--------------------------------------------*/

/** \brief connect to AOO server
 *
 * \note Threadsafe and RT-safe
 *
 * \param client the AOO client
 * \param hostName the AOO server host name
 * \param port the AOO server port
 * \param userName the user name
 * \param userPwd the user password
 * \param cb callback function for server reply
 * \param user user data passed to callback function
 */
AOO_INLINE AooError AooClient_connect(
        AooClient *client, const AooChar *hostName, AooInt32 port,
        const AooChar *userName, const AooChar *userPwd, AooNetCallback cb, void *user)
{
    AooNetRequestConnect data = { hostName, port, userName, userPwd };
    return AooClient_sendRequest(client, kAooNetRequestConnect, &data, cb, user);
}

/** \brief disconnect from AOO server
 *
 * \note Threadsafe and RT-safe
 *
 * \param client the AOO client
 * \param cb callback function for server reply
 * \param user user data passed to callback function
 */
AOO_INLINE AooError AooClient_disconnect(
        AooClient *client, AooNetCallback cb, void *user)
{
    return AooClient_sendRequest(client, kAooNetRequestDisconnect, NULL, cb, user);
}

/** \brief join a group on the server
 *
 * \note Threadsafe and RT-safe
 *
 * \param client the AOO client
 * \param groupName the group name
 * \param groupPwd the group password
 * \param cb function to be called with server reply
 * \param user user data passed to callback function
 */
AOO_INLINE AooError AooClient_joinGroup(
        AooClient *client, const AooChar *groupName, const AooChar *groupPwd,
        AooNetCallback cb, void *user)
{
    AooNetRequestJoinGroup data = { groupName, groupPwd, 0 };
    return AooClient_sendRequest(client, kAooNetRequestJoinGroup, &data, cb, user);
}

/** \brief leave a group
 *
 * \note Threadsafe and RT-safe
 *
 * \param client the AOO client
 * \param groupName the group name
 * \param cb function to be called with server reply
 * \param user user data passed to callback function
 */
AOO_INLINE AooError AooClient_leaveGroup(
        AooClient *client, const AooChar *groupName, AooNetCallback cb, void *user)
{
    AooNetRequestLeaveGroup data = { groupName, 0, 0 };
    return AooClient_sendRequest(client, kAooNetRequestLeaveGroup, &data, cb, user);
}
