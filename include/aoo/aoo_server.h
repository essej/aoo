/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C interface for AOO server
 */

#pragma once

#include "aoo_net.h"
#include "aoo_events.h"
#include "aoo_controls.h"

//--------------------------------------------------------------//

typedef struct AooServer AooServer;

/** \brief create a new AOO source instance
 *
 * \param port the listening port (TCP + UDP)
 * \param flags optional flags
 * \param[out] err error code on failure
 * \return new AooServer instance on success; `NULL` on failure
 */
AOO_API AooServer * AOO_CALL AooServer_new(
        AooInt32 port, AooFlag flags, AooError *err);

/** \brief destroy AOO server instance */
AOO_API void AOO_CALL AooServer_free(AooServer *server);

/** \brief run the AOO server
 *
 * This function blocks until AooServer_quit() is called.
 */
AOO_API AooError AOO_CALL AooServer_run(AooServer *server);

/** \brief quit the AOO server from another thread */
AOO_API AooError AOO_CALL AooServer_quit(AooServer *server);

/** \brief set event handler function and event handling mode
 *
 * \warning Not threadsafe - only call in the beginning! */
AOO_API AooError AOO_CALL AooServer_setEventHandler(
        AooServer *sink, AooEventHandler fn, void *user, AooEventMode mode);

/** \brief check for pending events
 *
 * \note Threadsafe and RT-safe */
AOO_API AooBool AOO_CALL AooServer_eventsAvailable(AooServer *server);

/** \brief poll events
 *
 * \note Threadsafe and RT-safe, but not reentrant.
 *
 * This function will call the registered event handler one or more times.
 * \attention The event handler must have been registered with #kAooEventModePoll.
 */
AOO_API AooError AOO_CALL AooServer_pollEvents(AooServer *server);

/** \brief control interface
 *
 * used internally by helper functions for specific controls */
AOO_API AooError AOO_CALL AooServer_control(
        AooServer *server, AooCtl ctl, AooIntPtr index, void *data, AooSize size);

/*--------------------------------------------*/
/*         type-safe control functions        */
/*--------------------------------------------*/

/* (empty) */

/*----------------------------------------------------------------------*/
