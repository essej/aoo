/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C++ interface for AOO client
 */

#pragma once

#include "aoo_client.h"

#include <memory>

struct AooSource;
struct AooSink;

/** \brief AOO client interface */
struct AooClient {
public:
    /** \brief custom deleter for AooClient */
    class Deleter {
    public:
        void operator()(AooClient *obj){
            AooClient_free(obj);
        }
    };

    /** \brief smart pointer for AOO client instance */
    using Ptr = std::unique_ptr<AooClient, Deleter>;

    /** \brief create a new managed AOO client instance
     *
     * \copydetails AooClient_new()
     */
    static Ptr create(const void *address, AooAddrSize addrlen,
                      AooFlag flags, AooError *err) {
        return Ptr(AooClient_new(address, addrlen, flags, err));
    }

    /*------------------ methods -------------------------------*/

    /** \copydoc AooClient_run() */
    virtual AooError AOO_CALL run() = 0;

    /** \copydoc AooClient_quit() */
    virtual AooError AOO_CALL quit() = 0;

    /** \copydoc AooClient_addSource() */
    virtual AooError AOO_CALL addSource(AooSource *source, AooId id) = 0;

    /** \copydoc AooClient_removeSource() */
    virtual AooError AOO_CALL removeSource(AooSource *source) = 0;

    /** \copydoc AooClient_addSink() */
    virtual AooError AOO_CALL addSink(AooSink *sink, AooId id) = 0;

    /** \copydoc AooClient_removeSink() */
    virtual AooError AOO_CALL removeSink(AooSink *sink) = 0;

    /** \copydoc AooClient_getPeerByName() */
    virtual AooError AOO_CALL getPeerByName(
            const AooChar *group, const AooChar *user,
            void *address, AooAddrSize *addrlen) = 0;

    /** \copydoc AooClient_sendRequest() */
    virtual AooError AOO_CALL sendRequest(
            AooNetRequestType request, void *data,
            AooNetCallback callback, void *user) = 0;

    /** \copydoc AooClient_sendPeerMessage() */
    virtual AooError AOO_CALL sendPeerMessage(
            const AooByte *data, AooInt32 size,
            const void *target, AooAddrSize len, AooFlag flags) = 0;

    /** \copydoc AooClient_handleMessage() */
    virtual AooError AOO_CALL handleMessage(
            const AooByte *data, AooInt32 size, const void *address, AooAddrSize addrlen) = 0;

    /** \copydoc AooClient_send() */
    virtual AooError AOO_CALL send(AooSendFunc fn, void *user) = 0;

    /** \copydoc AooClient_setEventHandler() */
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    /** \copydoc AooClient_eventsAvailable() */
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    /** \copydoc AooClient_pollEvents() */
    virtual AooError AOO_CALL pollEvents() = 0;

    /** \copydoc AooClient_control */
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    /*--------------------------------------------*/
    /*         type-safe control functions        */
    /*--------------------------------------------*/

    /* (empty) */

    /*--------------------------------------------*/
    /*         type-safe request functions        */
    /*--------------------------------------------*/

    /** \copydoc AooClient_connect() */
    AooError connect(const AooChar *hostName, AooInt32 port,
                     const AooChar *userName, const AooChar *userPwd,
                     AooNetCallback cb, void *user)
    {
        AooNetRequestConnect data = { hostName, port, userName, userPwd };
        return sendRequest(kAooNetRequestConnect, &data, cb, user);
    }

    /** \copydoc AooClient_disconnect() */
    AooError disconnect(AooNetCallback cb, void *user)
    {
        return sendRequest(kAooNetRequestDisconnect, NULL, cb, user);
    }

    /** \copydoc AooClient_joinGroup() */
    AooError joinGroup(const AooChar *groupName, const AooChar *groupPwd,
                       AooNetCallback cb, void *user)
    {
        AooNetRequestJoinGroup data = { groupName, groupPwd };
        return sendRequest(kAooNetRequestJoinGroup, &data, cb, user);
    }

    /** \copydoc AooClient_leaveGroup() */
    AooError leaveGroup(const AooChar *groupName, AooNetCallback cb, void *user)
    {
        AooNetRequestLeaveGroup data = { groupName };
        return sendRequest(kAooNetRequestLeaveGroup, &data, cb, user);
    }
protected:
    ~AooClient(){} // non-virtual!
};
