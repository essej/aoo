/*
 * AoO - OSC interface
 *
 * Copyright (c) 2014 Winfried Ritsch <ritsch_at_algo.mur.at>
 *
 * This library is covered by the LGPL, read licences
 * at <http://www.gnu.org/licenses/>  for details
 *
 */

#ifndef __AOO_OSC_H__
#define __AOO_OSC_H__

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* #include <math.h> */

/* max UDP length should be enough */
#define AOO_MAX_MESSAGE_LEN 65536

/* Only following typetags supported by now
 *
 * OSC Type Tag Type of corresponding argument
 *  i               int32
 *  f               float32
 *  s               OSC-string
 *  b               OSC-blob
*/

/* from stdint.h types for OSC protocol */
typedef int32_t  	  osc_int;
typedef uint32_t  	  osc_len;
typedef uint32_t 	  osc_uint;
typedef unsigned char osc_byte;
typedef float         osc_float;
typedef char *        osc_string;
typedef void *	      osc_blob;

typedef union _OSC_TIMETAG {
    struct {
        uint32_t sec;
        uint32_t frac;
    } time;
    uint64_t val;
} osc_timetag;
#define AOO_TIMETAG_IMMIDIATELY ((osc_timetag) 0x0000000000000001)
#define AOO_TIMETAG_MAX         ((osc_timetag) 0xffffffffffffffff)

/*
 * ~ align sizeof literal strings to 4 bytes of len
 * ~ eg.: sizeof("123")=4, 4*((4+3)/4)=4*1=4, "1234"->8
 */
#define aoo_size4(s) (4*((sizeof(s)+3)/4))

/* ---  OSC messages drain --- */

#define AOO_OSC_BUNDLE "#bundle"  /* 8 bytes with implizit \0*/

#define AOO_OSC_MESSAGE    "/AoO"          /* 4 bytes without \0*/
#define AOO_OSC_DRAIN      "/AoO/drain/"   /* 11 bytes without \0*/

/* format */
#define AOO_OSC_FORMAT       "/format"   /* 6 bytes without \0*/
#define AOO_OSC_FORMAT_TT    ",iiis\0\0" /* 7 Bytes + \0 */
#define AOO_OSC_FORMAT_TT_TC ",iiisf\0"  /* 7 Bytes + \0 */

typedef struct _AOO_FORMAT {
    osc_string drain;
    osc_int samplerate;
    osc_int blocksize;
    osc_int overlap;
    osc_string mimetype;
    osc_float time_correction;
} aoo_format;

/* channel */
#define AOO_OSC_CHANNEL    "/channel/" /* 9 bytes without \0*/
#define AOO_OSC_CHANNEL_TT ",iiifb\0"  /* 7 Bytes + \0 */

typedef struct _AOO_CHANNEL {
    osc_int id;
    osc_int sequence;
    osc_int resolution;
    osc_float resampling;
    osc_int time_correction;
    osc_int datasize;
    osc_blob data;
} aoo_channel;


/* --- SEND ANNOUNCE hopefully on Broadcast to anounce me --- */

typedef struct _aoo_osc_announce {
    unsigned int ip[4];        /* udp info */
    unsigned int netmask[4];
    unsigned int gateway[4];
    unsigned int remoteip[4];
    unsigned int mac[6];
    unsigned int id;           /* id of device */
    unsigned int count;        /* sequence if some is missed */
    unsigned int drain;        /* drain id available */
    char name[16];             /* name to be identified */
} aoo_osc_announce_drain;

#define AOO_OSC_ANNOUNCE_ADR  "/announce\0\0\0" // 12 Bytes incl. terminating
#define AOO_OSC_ANNOUNCE_TT ",iiiiiiiiiiiiiiiiiiiiiiiiis" // 27 Bytes + \0
#define AOO_OSC_ANNOUNCE_SIZE aoo_size4(AOO_OSC_ANNOUNCE_DRAIN)

/* OSC parser */

typedef enum  {
    OSC_PARSE_NO_CHANNELS=-4,
    OSC_PARSE_MIME_UNKOWN=-3,
    OSC_PARSE_NO_FORMAT=-2,
    OSC_PARSE_NO_DRAIN=-1, 
    OSC_PARSE_NOT_VALID=0 
} aoo_parser_ret;

typedef struct _AOO_OSC_DRAIN {
    void *  data;
    osc_int drains;
    osc_string *drain;
    int channels;

/* needed as storage, since delegated */
    osc_timetag timestamp;
    aoo_format format;

    int (*process_channel)(aoo_format *,aoo_channel *);

    aoo_osc_announce_drain announce;
} aoo_osc_drain;

/* --- Prototypes --- */
aoo_osc_drain *aoo_osc_drain_new(osc_string drain,int channels,
                                 int (*process_channel)(aoo_format *,aoo_channel *));
void *aoo_osc_drain_free(aoo_osc_drain *osc);
aoo_parser_ret aoo_osc_parse(aoo_osc_drain *osc,unsigned int datalen, void *data);
void aoo_osc_send(osc_string drain,unsigned int channels,aoo_format format,
                  unsigned int samples, void *audiodata);

/* void aooOSCAnnounceDrain(); /* announce message send */




#endif /* __AOO_OSC_H__ */
