#pragma once

#include "m_pd.h"

#include "aoo_net.h"

// hack for pd-lib-builder
#ifdef AOO_BUILD
#undef AOO_BUILD
#endif

#include "aoo/aoo.h"
#include "aoo/aoo_pcm.h"
#include "aoo/aoo_opus.h"

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

/*///////////////////////////// aoo_server /////////////////////////////*/

typedef struct _aoo_server t_aoo_server;

t_aoo_server *aoo_server_addclient(t_pd *client, int32_t id, int port);

void aoo_server_removeclient(t_aoo_server *server, t_pd *client, int32_t id);

t_endpoint * aoo_server_getendpoint(t_aoo_server *server,
                                    const struct sockaddr_storage *sa, socklen_t len);

int aoo_server_port(t_aoo_server *);

void aoo_server_notify(t_aoo_server *x);

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

void aoo_lock_unlock_shared(aoo_lock *x);

/*///////////////////////////// helper functions ///////////////////////////////*/

int aoo_getsinkarg(void *x, int argc, t_atom *argv,
                        struct sockaddr_storage *sa, socklen_t *len, int32_t *id);

int aoo_getsourcearg(void *x, int argc, t_atom *argv,
                        struct sockaddr_storage *sa, socklen_t *len, int32_t *id);

int aoo_parseresend(void *x, int argc, const t_atom *argv,
                    int32_t *limit, int32_t *interval,
                    int32_t *maxnumframes);

void aoo_defaultformat(aoo_format_storage *f, int nchannels);

int aoo_parseformat(void *x, aoo_format_storage *f, int argc, t_atom *argv);

int aoo_printformat(const aoo_format_storage *f, int argc, t_atom *argv);
