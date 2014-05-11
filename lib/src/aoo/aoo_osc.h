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
#define AOO_MAX_NAME_LEN 11 /* max string of drain or channel nr, since uint */

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
/* OSC is always 4bytes aligned */
typedef uint32_t      osc_data;
typedef osc_data*     osc_blob;

typedef union _OSC_TIMETAG {
    struct {
        uint32_t sec;
        uint32_t frac;
    } time;
    uint64_t val;
} osc_timetag;
#define TIMETAG_NO          (0x0000000000000000UL)
#define TIMETAG_IMMIDIATELY (0x0000000000000001UL)
#define TIMETAG_MAX         (0xffffffffffffffffUL)
/*
 * ~ align sizeof literal strings to 4 bytes of len
 * ~ eg.: sizeof("123")=4, 4*((4+3)/4)=4*1=4, "1234"->8
 */
#define aoo_size4(s) (4*(((s)+3)/4))
unsigned int aoo_string_expand4(char* sptr);

/* ---  OSC messages drain --- */
/* try to make all 4 bytes aligned for faster copy and check */

#define OSC_BUNDLE "#bundle"  /* 8 bytes with implizit \0*/

#define AOO_DOMAIN "/AoO"       /* 4 bytes without \0*/

/* shortened for efficiency */
#define AOO_DRAIN  "/AoO/drain/"   /* 11 bytes without \0*/

/* drain string is 4 bytes, so range is 0-9999 */
#define AOO_MAX_DRAIN   9999
#define osc_drain_set_string(s,n)     sprintf((s),"%04u",(n))

/* format is last in address so needs one \0 */
#define AOO_FORMAT       "/format"   /* 7 bytes without \0*/
#define AOO_FORMAT_TT    ",iiis\0\0" /* 7 Bytes + \0 */
#define AOO_FORMAT_TT_TC ",iiisf\0"  /* 7 Bytes + \0 */

/* format message data
   order of data in OSC message, do not change */
/* hack since only one MIME/TYPE now  for efficiency*/
#define AOO_MIME_SIZE 12
#define AOO_MIME_PCM "audio/pcm\0\0" /* 12 byte incl \0 */
#define AOO_TIME_CORRECTION_NO 0
typedef struct _AOO_FORMAT_PARAMETER {
    osc_int samplerate;
    osc_int blocksize;
    osc_int overlap;
    char mimetype[AOO_MIME_SIZE];
    osc_float time_correction;
} aoo_format_parameter;

/* channel */
/* shortened for efficiency */
#define AOO_CHANNEL    "/channel/" /* 9 bytes without \0*/
#define AOO_CHANNEL_TT ",iiifb\0"  /* 7 Bytes + \0 */
/* channel string is 4 bytes, so range is 0-9999 */
#define AOO_MAX_CHANNELS   9999
#define osc_channel_set_string(s,n)  sprintf((s),"%04u",(n))

/* channel data without blob data, but blobsize
   order of data in OSC message, do not change */
typedef struct _AOO_CHANNEL_PARAMETER {
    osc_int id;
    osc_int sequence;
    osc_int resolution;
    osc_int resampling;
    osc_int blobsize;
} aoo_channel_parameter;

/** Notes on Resolution of samples: enum aoo_resolution
    - not used for internal processing but for transmission
    - integer are supported to allow smaller transmission sizes
    - Number means aligment of bits: eg. 24Bit means 24bit alignment
    - 0-3 are special meanings see enum aoo_resolution
*/
#define AOO_RESOLUTION_MIN_BITS 4
typedef enum {
    AOO_RESOLUTION_FLOAT = 0,
    AOO_RESOLUTION_DOUBLE = 1,
    AOO_RESOLUTION_RESERVE_1 = 2,
    AOO_RESOLUTION_RESERVE_2 = 3
    /* >=AOO_RESOLUTION_MIN_BITS are number of bits per sample int */
    /* < 0 number of bits per sample unsigned int, not supported, maybe never */
} aoo_resolution;
#define aoo_resbits(res) ((res)>3?(res):((res)==AOO_RESOLUTION_DOUBLE?64:32))

/** Notes on resampling:
    Resampling of channels means downsampled signals only if positive > 1
    and downsampled if negativ < -2
    example: -1,0,1 means no down or upsampling,
             -2, -3, -4, --- downsampled data by 2, 3, 4
             2 , 4, 8 means upsampled by factor 2, 4, 8
    Numbers not (+/-)2^N should be avoided.
    Antialising filter on downsampling (positive numbers) is weak !
*/
/* aligne samples bitwise for blobsize on 4 byte border for OSC */
#define aoo_resampling_fakt(res) \
  ((res)>1?(float)(res):((res<-1)?(-1.0/((float)(res))):1.0))
#define aoo_resample_blocksize(blksz,res) \
  ((unsigned int) ((float) (blksz)*aoo_resampling_fakt((res))))
#define aoo_blobsize_bytes(bs,resbits) (((bs)*(resbits)+7)/8)

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
    osc_timetag timetag;
    aoo_format_parameter format; /* copied from receive */

    /* for parse */
    unsigned int format_head_size;
    char *format_head;

    unsigned int            (* channel_head_size)[];
    char                    *((* channel_head)[]);
    aoo_channel_parameter   (* channel_parameter)[];
    osc_blob                (* channel_data)[];

    int (*process_channel)(unsigned int, unsigned int, osc_timetag,
                           aoo_format_parameter *,
                           aoo_channel_parameter *, osc_blob);

    osc_drain_announcement announce;
} osc_drain;

typedef enum  {
    OSC_PARSE_NOT_VALID=-4,
    OSC_PARSE_MIME_UNKOWN=-3,
    OSC_PARSE_NO_FORMAT=-2,
    OSC_PARSE_NO_MATCH=-1,
    OSC_PARSE_NO_CHANNELS=0
} osc_parser_ret;

typedef enum  {
    OSC_SRC_NO_MEMORY=-5,
    OSC_SRC_NOT_VALID=-4,
    OSC_SRC_NO_FORMAT=-2,
    OSC_SRC_NO_DRAIN=-1,
    OSC_SRC_NO_CHANNELS=0
} osc_src_ret;


/* --- drain prototypes --- */
osc_drain* osc_drain_new(unsigned int drain, unsigned int channels,
                                 int (* process_channel)(unsigned int, unsigned int,
                                                         osc_timetag,
                                                         aoo_format_parameter *,
                                                         aoo_channel_parameter *,
                                                         osc_blob));
void osc_drain_free(osc_drain* osc);
osc_parser_ret osc_drain_parse(osc_drain* osc,
                             unsigned int datalen, void* data);

/* void aoo_osc_drain_announce(...); */ /* announce message send */

/* --- source --- */
typedef struct _AOO_SRC {

    unsigned int drain;
    unsigned int channels;

    /* internal parameter cache */
    aoo_format_parameter iformat;
    aoo_channel_parameter *ichannel;
    unsigned int *ichannel_nr;

    /* vars for construction */
    unsigned int bundle_len;
    unsigned int format_len;
    char *fmt_head;
    unsigned int fmt_headlen;
    char *((* ch_head)[]);
    unsigned int (*ch_headlen)[];

    /* pointers in message */
    osc_timetag *timetag;
    aoo_format_parameter* format;
    aoo_channel_parameter *((* channel)[]);
    osc_blob  (* channel_data)[];

    unsigned int bundlesize;
    osc_blob bundle;
} osc_src;

/* source prototypes */
osc_src *osc_src_new(unsigned int drain,unsigned int channels);
void osc_src_free(osc_src *src);
unsigned int osc_src_set_format(osc_src *src, osc_int samplerate,
               osc_int blocksize, osc_int overlap);
unsigned int osc_src_set_channel(osc_src *src,
                         unsigned int send_channel, unsigned int drain_channel,
                         osc_int id, osc_int resolution, osc_int resampling);
osc_blob osc_src_get_blobs(osc_src *src);
#define osc_src_get_blobsize(src,n) ((src)->ichannel[(n)].blobsize)
#define osc_src_get_bundle(src) ((src)->bundle)
#define osc_src_get_bundlesize(src) ((src)->bundlesize)
#define osc_src_timetag(src,tt) (((src)->timetag)->val=(tt));

/* ============ DEFAULTS ============ */
#define AOO_FORMAT_DEFAULT_SAMPLERATE 44100
#define AOO_FORMAT_DEFAULT_BLOCKSIZE  64
#define AOO_FORMAT_DEFAULT_OVERLAP    0
#define AOO_CHANNEL_DEFAULT_ID 0
#define AOO_CHANNEL_DEFAULT_RESAMPLING 0
#define AOO_CHANNEL_DEFAULT_RESOLUTION AOO_RESOLUTION_FLOAT

#endif /* __AOO_OSC_H__ */
