/*
 * prototypes, structure definitions and macros for "audio over osc"
 *
 * Copyright (c) 2014 Winfried Ritsch <ritsch_at_algo.mur.at>
 *
 * This library is covered by the LGPL, read licences
 * at <http://www.gnu.org/licenses/>  for details
 *
 */

#ifndef __AOO_H__
#define __AOO_H__

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
/* #include <math.h> */

/* max UDP length should be enough */
#define AOO_MAX_MESSAGE_LEN 65536


typedef enum {
    AOO_VERBOSITY_NO = 0,
    AOO_VERBOSITY_INFO = 1,
    AOO_VERBOSITY_DETAIL = 2,
    AOO_VERBOSITY_DEBUG = 3}
aoo_verbosity_state;
extern int aoo_verbosity;

/* =================================== AoO general ============================= */
/* types */
#define AOO_SAMPLE_FORMAT_FLOAT
/* #define AOO_SAMPLE_FORMAT_DOUBLE, maybe sometime later as compile opt */

typedef int32_t    aoo_signed_t;
typedef uint32_t   aoo_unsigned_t;
typedef float      aoo_float_t;
typedef double     aoo_double_t;

#if defined(AOO_SAMPLE_FORMAT_FLOAT)
typedef aoo_float_t aoo_sample_t;
#elif defined(AOO_SAMPLE_FORMAT_DOUBLE)
typedef aoo_double_t aoo_sample_t;
#endif

/* === prototypes === */
int aoo_setup(void);         /* initialize lib */
int aoo_release(void);       /* release lib */

/* === sources === */
int aoo_source_new(void);

/* === drains  === */
int aoo_drain_new(int id);     /* setup new drain */
int aoo_drain_start(int id);   /* start processing */
int aoo_drain_perform(int id); /* start processing */
int aoo_drain_stop(int id);    /* stop processing */
int aoo_drain_free(int id);    /* free new drain */

#endif /* __AOO_H__ */
