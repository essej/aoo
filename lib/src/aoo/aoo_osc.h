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


/* ================================ OSC ================================= */

/* max UDP length should be enough */
#define AOO_MAX_BUNDLE_LEN 65536

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
typedef char*         osc_string;
typedef uint32_t      osc_data; /* OSC is always 4bytes aligned */
typedef void* 	      osc_blob;

typedef union _OSC_TIMETAG {
    struct {
        uint32_t sec;
        uint32_t frac;
    } time;
    uint64_t val;
} osc_timetag;
#define TIMETAG_NO          ((osc_timetag) 0x0000000000000000)
#define TIMETAG_IMMIDIATELY ((osc_timetag) 0x0000000000000001)
#define TIMETAG_MAX         ((osc_timetag) 0xffffffffffffffff)
/*
 * ~ align sizeof literal strings to 4 bytes of len
 * ~ eg.: sizeof("123")=4, 4*((4+3)/4)=4*1=4, "1234"->8
 */
#define aoo_size4(s) (4*((sizeof(s)+3)/4))

/* ---  OSC messages drain --- */
/* try to make all 4 bytes aligned for faster copy and check */

#define OSC_BUNDLE "#bundle"  /* 8 bytes with implizit \0*/

/* =================================== AoO general ============================= */

#define AOO_DOMAIN "/AoO"       /* 4 bytes without \0*/

/* shortened for efficiency */
/* #define AOO_DRAIN  "/AoO/drain/"   *//* 11 bytes without \0*/
#define AOO_DRAIN  "/AoO/dr/"   /* 8 bytes without \0*/

/* drain string is 4 bytes, so range is 0-9999 */
#define osc_drain_set_string(s,n)     sprintf((s),"%04u",(n))

/* format is last in address so needs one \0 */
#define AOO_FORMAT       "/format"   /* 8 bytes with \0*/
#define AOO_FORMAT_TT    ",iiis\0\0" /* 7 Bytes + \0 */
#define AOO_FORMAT_TT_TC ",iiisf\0"  /* 7 Bytes + \0 */

/* format message data
   order of data in OSC message, do not change */
typedef struct _AOO_FORMAT_PARAMETER {
    osc_int samplerate;
    osc_int blocksize;
    osc_int overlap;
    osc_string mimetype;
    osc_float time_correction;
} aoo_format_parameter;

/* channel */
/* shortened for efficiency */
/* #define AOO_CHANNEL    "/channel/" */ /* 9 bytes without \0*/
#define AOO_CHANNEL    "/ch/" /* 4 bytes without \0*/
#define AOO_CHANNEL_TT ",iiifb\0"  /* 7 Bytes + \0 */
/* channel string is 4 bytes, so range is 0-9999 */
#define aoo_channel_set_string(s,n)  sprintf((s),"%04u",(n))

/* channel data without blob data, but blobsize
   order of data in OSC message, do not change */
typedef struct _AOO_CHANNEL_PARAMETER {
    osc_int id;
    osc_int sequence;
    osc_int resolution;
    osc_int resampling;
    osc_int blobsize;
} aoo_channel_parameter;

/** Notes on Resolution of samples: enum aoo_osc_resolution
    - the one used for internal processing but for transmission
    - integer are supported to allow smaller transmission sizes
    - 24Bit means 24bit alignment
    - 32bit aligmnet can also only use upper 24 bits
*/
typedef enum {
    AOO_RESOLUTION_FLOAT = 0,
    AOO_RESOLUTION_DOUBLE = 1,
    AOO_RESOLUTION_RESERVE_1 = 2,
    AOO_RESOLUTION_RESERVE_2 = 3
    /* >=4 are number of bits per sample int */
    /* < 0 number of bits per sample unsigned int, not supported, maybe never */
} aoo_resolution;

#define AOO_ANNOUNCE_ADR  "/announce\0\0\0" // 12 Bytes incl. terminating
#define AOO_ANNOUNCE_TT ",iiiiiiiiiiiiiiiiiiiiiiiiis" // 27 Bytes + \0
#define AOO_ANNOUNCE_SIZE aoo_size4(AOO_ANNOUNCE_DRAIN)

/* --- SEND ANNOUNCE hopefully on Broadcast to anounce me --- */
#define AOO_ANNOUNCE_NAME_MAX 16 /* 15 chars + \0 */
typedef struct _osc_drain_announce {
    /* new: info from UDP packet has to be used
            unsigned int ip[4];
            unsigned int netmask[4];
            unsigned int gateway[4];
            unsigned int remoteip[4];
            unsigned int mac[6];
    */
    unsigned int count;        /* sequence nr of announces */
    char name[AOO_ANNOUNCE_NAME_MAX]; /* meaningful name to be identified */
} osc_drain_announcement;

/* --- DRAIN  --- */
typedef struct _AOO_DRAIN {

    unsigned int drain;
    unsigned int channels;
    /* received */
    osc_timetag timestamp;

    aoo_format_parameter format;

    unsigned int format_head_size;
    osc_string    format_head;

    unsigned int* channel_head_size;
    osc_data**     channel_head;
    aoo_channel_parameter* channel_parameter;
    osc_blob* channel_data;

    int (*process_channel)(int, aoo_format_parameter *,
                           aoo_channel_parameter *, osc_blob *);

    osc_drain_announcement announce;
} osc_drain;

typedef enum  {
    OSC_PARSE_NOT_VALID=-4,
    OSC_PARSE_MIME_UNKOWN=-3,
    OSC_PARSE_NO_FORMAT=-2,
    OSC_PARSE_NO_MATCH=-1,
    OSC_PARSE_NO_CHANNELS=0
} aoo_parser_ret;

typedef enum  {
    OSC_SRC_NO_MEMORY=-5,
    OSC_SRC_NOT_VALID=-4,
    OSC_SRC_NO_FORMAT=-2,
    OSC_SRC_NO_DRAIN=-1,
    OSC_SRC_NO_CHANNELS=0
} osc_src_ret;


/* --- drain prototypes --- */
osc_drain* osc_drain_new(unsigned int drain, unsigned int channels,
                                 int (*process_channel)(aoo_format_parameter *,
                                                        aoo_channel_parameter *));

void osc_drain_free(osc_drain* osc);

aoo_parser_ret osc_drain_parse(osc_drain* osc,
                             unsigned int datalen, void* data);


/* void aoo_osc_drain_announce(...); */ /* announce message send */

/* --- source --- */
typedef struct _AOO_SRC {

    unsigned int drain;
    unsigned int channels;

    aoo_format_parameter* format;

    unsigned int bundlemaxsize;
    unsigned int bundlesize;
    osc_data *bundle;
} osc_src;



/* source prototypes */
osc_src* osc_src_new(unsigned int drain,unsigned int channels,
                             unsigned int max_blob_size);
int osc_src_format(osc_src* src,aoo_format_parameter* format);
int osc_src_addchannel(osc_src* src, unsigned int ch_nr,osc_int samples,
                       osc_int id, osc_int resolution, osc_int resampling);


#endif /* __AOO_OSC_H__ */

