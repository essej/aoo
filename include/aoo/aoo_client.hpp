#pragma once

#include "aoo_client.h"

#include <memory>

struct AooSource;
struct AooSink;

struct AooClient {
public:
    class Deleter {
    public:
        void operator()(AooClient *obj){
            AooClient_free(obj);
        }
    };

    // smart pointer for AoO client instance
    using Ptr = std::unique_ptr<AooClient, Deleter>;

    // create a new AoO client instance
    static Ptr create(const void *address, AooAddrSize addrlen,
                      AooFlag flags, AooError *err) {
        return Ptr(AooClient_new(address, addrlen, flags, err));
    }

    //----------------- methods ------------------------------//

    // run the AOO client; this function blocks indefinitely.
    virtual AooError AOO_CALL run() = 0;

    // quit the AOO client from another thread
    virtual AooError AOO_CALL quit() = 0;

    // add AOO source
    virtual AooError AOO_CALL addSource(AooSource *source, AooId id) = 0;

    // remove AOO source
    virtual AooError AOO_CALL removeSource(AooSource *source) = 0;

    // add AOO sink
    virtual AooError AOO_CALL addSink(AooSink *sink, AooId id) = 0;

    // remove AOO sink
    virtual AooError AOO_CALL removeSink(AooSink *sink) = 0;

    // find peer by name and return its IP endpoint address
    // address: pointer to sockaddr_storage
    // addrlen: initialized with max. storage size, updated to actual size
    virtual AooError AOO_CALL getPeerByName(
            const AooChar *group, const AooChar *user,
            void *address, AooAddrSize *addrlen) = 0;

    // send a request to the AOO server (always thread safe)
    virtual AooError AOO_CALL sendRequest(
            AooNetRequestType request, void *data,
            AooNetCallback callback, void *user) = 0;

    // send a message to one or more peers
    // 'addr' + 'len' accept the following values:
    // a) 'struct sockaddr *' + 'socklen_t': send to a single peer
    // b) 'const AooChar *' (group name) + 0: send to all peers of a specific group
    // c) 'NULL' + 0: send to all peers
    // the 'flags' parameter allows for (future) additional settings
    virtual AooError AOO_CALL sendPeerMessage(
            const AooByte *data, AooInt32 size,
            const void *target, AooAddrSize len, AooFlag flags) = 0;

    // handle messages from peers (thread safe, called from a network thread)
    // 'addr' should be sockaddr *
    virtual AooError AOO_CALL handleMessage(
            const AooByte *data, AooInt32 size, const void *address, AooAddrSize addrlen) = 0;

    // send outgoing messages (threadsafe, called from a network thread)
    virtual AooError AOO_CALL send(AooSendFunc fn, void *user) = 0;

    // set event handler callback + mode
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    // check for pending events (always thread safe)
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    // poll events (threadsafe, but not reentrant)
    // will call the event handler function one or more times
    virtual AooError AOO_CALL pollEvents() = 0;

    // client controls (threadsafe, but not reentrant)
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    // ----------------------------------------------------------
    // type-safe convenience methods for frequently used controls

    // (empty)

    // ----------------------------------------------------------
    // type-safe convenience methods for frequently used requests

    // connect to AOO server (always thread safe)
    AooError connect(const AooChar *hostName, AooInt32 port,
                     const AooChar *userName, const AooChar *userPwd,
                     AooNetCallback cb, void *user)
    {
        AooNetRequestConnect data = { hostName, port, userName, userPwd };
        return sendRequest(kAooNetRequestConnect, &data, cb, user);
    }

    // disconnect from AOO server (always thread safe)
    AooError disconnect(AooNetCallback cb, void *user)
    {
        return sendRequest(kAooNetRequestDisconnect, NULL, cb, user);
    }

    // join an AOO group
    AooError joinGroup(const AooChar *groupName, const AooChar *groupPwd,
                       AooNetCallback cb, void *user)
    {
        AooNetRequestJoinGroup data = { groupName, groupPwd };
        return sendRequest(kAooNetRequestJoinGroup, &data, cb, user);
    }

    // leave an AOO group
    AooError leave_group(const AooChar *groupName, AooNetCallback cb, void *user)
    {
        AooNetRequestLeaveGroup data = { groupName };
        return sendRequest(kAooNetRequestLeaveGroup, &data, cb, user);
    }
protected:
    ~AooClient(){} // non-virtual!
};
