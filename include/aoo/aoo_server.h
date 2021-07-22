#pragma once

#include "aoo_net.h"
#include "aoo_events.h"
#include "aoo_controls.h"

//--------------------------------------------------------------//

typedef struct AooServer AooServer;

// create a new AOO server instance,
// listening on the given UDP/TCP port
AOO_API AooServer * AOO_CALL AooServer_new(
        AooInt32 port, AooFlag flags, AooError *err);

// destroy AOO server instance
AOO_API void AOO_CALL AooServer_free(AooServer *server);

// run the AOO server;
// this function blocks until AooServer_quit() is called.
AOO_API AooError AOO_CALL AooServer_run(AooServer *server);

// quit the AOO server from another thread
AOO_API AooError AOO_CALL AooServer_quit(AooServer *server);

// set event handler callback + mode
AOO_API AooError AOO_CALL AooServer_setEventHandler(
        AooServer *sink, AooEventHandler fn, void *user, AooEventMode mode);

// check for pending events (always thread safe)
AOO_API AooBool AOO_CALL AooServer_eventsAvailable(AooServer *server);

// poll events (threadsafe, but not reentrant).
// will call the event handler function one or more times.
// NOTE: the event handler must have been registered with kAooEventModePoll.
AOO_API AooError AOO_CALL AooServer_pollEvents(AooServer *server);

// control interface (always threadsafe)
AOO_API AooError AOO_CALL AooServer_control(
        AooServer *server, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

//--------------------------------------------------------------------//
// type-safe convenience functions for frequently used controls

// (empty)

//--------------------------------------------------------------------//
