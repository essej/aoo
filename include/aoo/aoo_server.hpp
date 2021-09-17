/* Copyright (c) 2021 Christof Ressi
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/** \file
 * \brief C++ interface for AOO server
 */

#pragma once

#include "aoo_server.h"

#include <memory>

/** \brief AOO server interface */
struct AooServer {
public:
    /** \brief custom deleter for AooServer */
    class Deleter {
    public:
        void operator()(AooServer *obj){
            AooServer_free(obj);
        }
    };

    /** \brief smart pointer for AOO server instance */
    using Ptr = std::unique_ptr<AooServer, Deleter>;

    /** \brief create a new managed AOO server instance
     *
     * \copydetails AooServer_new()
     */
    static Ptr create(int32_t port, AooFlag flags, AooError *err) {
        return Ptr(AooServer_new(port, flags, err));
    }

    /*---------------------- methods ---------------------------*/

    /** \copydoc AooServer_run() */
    virtual AooError AOO_CALL run() = 0;

    /** \copydoc AooServer_quit() */
    virtual AooError AOO_CALL quit() = 0;

    /** \copydoc AooServer_setEventHandler() */
    virtual AooError AOO_CALL setEventHandler(
            AooEventHandler fn, void *user, AooEventMode mode) = 0;

    /** \copydoc AooServer_eventsAvailable() */
    virtual AooBool AOO_CALL eventsAvailable() = 0;

    /** \copydoc AooServer_pollEvents() */
    virtual AooError AOO_CALL pollEvents() = 0;

    /** \copydoc AooServer_control() */
    virtual AooError AOO_CALL control(
            AooCtl ctl, AooIntPtr index, void *data, AooSize size) = 0;

    /*--------------------------------------------*/
    /*         type-safe control functions        */
    /*--------------------------------------------*/

    /* (empty) */
protected:
    ~AooServer(){} // non-virtual!
};
