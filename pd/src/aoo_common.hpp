/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "m_pd.h"

#include "aoo/aoo.hpp"

#include "aoo_net.hpp"

#ifndef _WIN32
#include <pthread.h>
#endif

// setup function
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#elif __GNUC__ >= 4
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

#define classname(x) class_getname(*(t_pd *)x)

/*///////////////////////////// aoo_node /////////////////////////////*/

struct i_node {
    virtual ~i_node(){}

    virtual int socket() const = 0;
    virtual int port() const = 0;
    virtual int sendto(const char *buf, int32_t size,
                       const struct sockaddr *addr) = 0;
    virtual t_endpoint *endpoint(const sockaddr_storage *sa,
                                 socklen_t len) = 0;
    virtual t_endpoint * find_peer(t_symbol *group, t_symbol *user) = 0;
    virtual void add_peer(t_symbol *group, t_symbol *user,
                          const sockaddr *sa, socklen_t len) = 0;
    virtual void remove_peer(t_symbol *group, t_symbol *user) = 0;
    virtual void remove_group(t_symbol *group) = 0;
    virtual void remove_all_peers() = 0;
    virtual void notify() = 0;

    static i_node * get(int port, t_pd *obj, int32_t id);
    virtual void release(t_pd *obj, int32_t id);
};

/*///////////////////////////// helper functions ///////////////////////////////*/

int aoo_getsinkarg(void *x, i_node *node, int argc, t_atom *argv,
                   struct sockaddr_storage *sa, socklen_t *len, int32_t *id);

int aoo_getsourcearg(void *x, i_node *node, int argc, t_atom *argv,
                     struct sockaddr_storage *sa, socklen_t *len, int32_t *id);

void aoo_format_makedefault(aoo_format_storage *f, int nchannels);

int aoo_format_parse(void *x, aoo_format_storage *f, int argc, t_atom *argv);

int aoo_format_toatoms(const aoo_format *f, int argc, t_atom *argv);
