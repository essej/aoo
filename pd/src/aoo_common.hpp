/* Copyright (c) 2010-Now Christof Ressi, Winfried Ritsch and others. 
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

#pragma once

#include "m_pd.h"

#include "aoo/aoo.h"

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

struct t_node;

int aoo_node_socket(t_node *node);

int aoo_node_port(t_node *node);

int32_t aoo_node_sendto(t_node *node, const char *buf, int32_t size,
                        const struct sockaddr *addr);

t_node * aoo_node_add(int port, t_pd *obj, int32_t id);

void aoo_node_release(t_node *node, t_pd *obj, int32_t id);

t_endpoint * aoo_node_endpoint(t_node * node,
                               const struct sockaddr_storage *sa, socklen_t len);

t_endpoint * aoo_node_find_peer(t_node *node, t_symbol *group, t_symbol *user);

void aoo_node_add_peer(t_node *node, t_symbol *group, t_symbol *user,
                       const struct sockaddr *sa, socklen_t len);

void aoo_node_remove_peer(t_node *node, t_symbol *group, t_symbol *user);

void aoo_node_remove_group(t_node *node, t_symbol *group);

/*///////////////////////////// aoo_lock /////////////////////////////*/

#ifdef _WIN32
typedef void * aoo_lock;
#else
typedef pthread_rwlock_t aoo_lock;
#endif

void aoo_lock_init(aoo_lock *x);

void aoo_lock_destroy(aoo_lock *x);

void aoo_lock_lock(aoo_lock *x);

void aoo_lock_lock_shared(aoo_lock *x);

void aoo_lock_unlock(aoo_lock *x);
void aoo_node_remove_all_peers(t_node *node);

void aoo_lock_unlock_shared(aoo_lock *x);
void aoo_node_notify(t_node *node);

/*///////////////////////////// helper functions ///////////////////////////////*/

int aoo_endpoint_to_atoms(const t_endpoint *e, int32_t id, t_atom *argv);

int aoo_getsinkarg(void *x, t_node *node, int argc, t_atom *argv,
                   struct sockaddr_storage *sa, socklen_t *len, int32_t *id);

int aoo_getsourcearg(void *x, t_node *node, int argc, t_atom *argv,
                     struct sockaddr_storage *sa, socklen_t *len, int32_t *id);

void aoo_format_makedefault(aoo_format_storage *f, int nchannels);

int aoo_format_parse(void *x, aoo_format_storage *f, int argc, t_atom *argv);

int aoo_format_toatoms(const aoo_format *f, int argc, t_atom *argv);
