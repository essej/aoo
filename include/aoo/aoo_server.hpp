#pragma once

#include "aoo_server.h"

#include <memory>

struct AooServer {
public:
    class Deleter {
    public:
        void operator()(AooServer *obj){
            AooServer_free(obj);
        }
    };

    // smart pointer for AoO server instance
    using Ptr = std::unique_ptr<AooServer, Deleter>;

    // create a new AoO server instance
    static Ptr create(int32_t port, AooFlag flags, AooError *err) {
        return Ptr(AooServer_new(port, flags, err));
    }

    //--------------------- methods --------------------------//

    // run the AOO server;
    // this function blocks until quit() is called.
    virtual AooError AOO_CALL run() = 0;

    // quit the AOO server from another thread
    virtual AooError AOO_CALL quit() = 0;

    // set event handler callback + mode
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    // check for pending events (always thread safe)
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    // poll events (threadsafe, but not reentrant).
    // will call the event handler function one or more times.
    // NOTE: the event handler must have been registered with kAooEventModePoll.
    virtual AooError AOO_CALL pollEvents() = 0;

    // server controls (threadsafe, but not reentrant)
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    // ----------------------------------------------------------
    // type-safe convenience methods for frequently used controls

    // (empty)
protected:
    ~AooServer(){} // non-virtual!
};
