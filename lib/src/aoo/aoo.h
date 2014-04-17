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

int aoo_setup(void);         /* initialize lib */
/*int aoo_release(void);         /* release lib */

/* drains  interface */
int aoo_drain_new(int id);     /* setup new drain */
int aoo_drain_start(int id);   /* start processing */
int aoo_drain_perform(int id); /* start processing */
int aoo_drain_stop(int id);    /* stop processing */
int aoo_drain_free(int id);    /* free new drain */

#endif /* __AOO_H__ */
