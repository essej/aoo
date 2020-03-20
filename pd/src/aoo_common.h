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

// setup function
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#elif __GNUC__ >= 4
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

uint64_t aoo_pd_osctime(int n, t_float sr);

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
