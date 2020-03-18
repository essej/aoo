#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef AOO_SAMPLETYPE
#define AOO_SAMPLETYPE float
#endif

typedef AOO_SAMPLETYPE aoo_sample;

#define AOO_DOMAIN "/AoO"
#define AOO_FORMAT "/format"
#define AOO_FORMAT_NARGS 7
#define AOO_FORMAT_WILDCARD "/AoO/*/format"
#define AOO_DATA "/data"
#define AOO_DATA_NARGS 9
#define AOO_DATA_WILDCARD "/AoO/*/data"
#define AOO_REQUEST "/request"
#define AOO_RESEND "/resend"
#define AOO_PING "/ping"
#define AOO_INVITE "/invite"

#define AOO_MAXPACKETSIZE 4096 // ?

#ifndef AOO_CLIP_OUTPUT
#define AOO_CLIP_OUTPUT 0
#endif

// 0: error, 1: warning, 2: verbose, 3: debug
#ifndef LOGLEVEL
 #define LOGLEVEL 2
#endif

#ifndef AOO_DEBUG_DLL
 #define AOO_DEBUG_DLL 0
#endif

#ifndef AOO_DEBUG_RESAMPLING
 #define AOO_DEBUG_RESAMPLING 0
#endif

// default values

#ifndef AOO_PACKETSIZE
 #define AOO_PACKETSIZE 512
#endif

#ifndef AOO_SOURCE_BUFSIZE
 #define AOO_SOURCE_BUFSIZE 10
#endif

#ifndef AOO_SINK_BUFSIZE
 #define AOO_SINK_BUFSIZE 100
#endif

#ifndef AOO_RESEND_BUFSIZE
 #define AOO_RESEND_BUFSIZE 1000
#endif

#ifndef AOO_TIMEFILTER_BANDWIDTH
 #define AOO_TIMEFILTER_BANDWIDTH 0.012
#endif

#ifndef AOO_PING_INTERVAL
 #define AOO_PING_INTERVAL 1000
#endif

#ifndef AOO_RESEND_LIMIT
 #define AOO_RESEND_LIMIT 5
#endif

#ifndef AOO_RESEND_INTERVAL
 #define AOO_RESEND_INTERVAL 10
#endif

#ifndef AOO_RESEND_MAXNUMFRAMES
 #define AOO_RESEND_MAXNUMFRAMES 64
#endif

void aoo_setup(void);

void aoo_close(void);

struct aoo_format;

/*//////////////////// OSC ////////////////////////////*/

// id: the source or sink ID
// returns: the offset to the remaining address pattern

#define AOO_ID_WILDCARD -1
#define AOO_ID_NONE INT32_MIN

int32_t aoo_parsepattern(const char *msg, int32_t n, int32_t *id);

uint64_t aoo_osctime_get(void);

double aoo_osctime_toseconds(uint64_t t);

uint64_t aoo_osctime_fromseconds(double s);

uint64_t aoo_osctime_addseconds(uint64_t t, double s);

typedef int32_t (*aoo_replyfn)(
        void *,         // endpoint
        const char *,   // data
        int32_t         // number of bytes
);

/*//////////////////// AoO events /////////////////////*/

#define AOO_EVENTQUEUESIZE 64

// event types
typedef enum aoo_event_type
{
    AOO_PING_EVENT,
    AOO_FORMAT_EVENT,
    AOO_SOURCE_STATE_EVENT,
    AOO_BLOCK_LOSS_EVENT,
    AOO_BLOCK_REORDER_EVENT,
    AOO_BLOCK_RESEND_EVENT,
    AOO_BLOCK_GAP_EVENT
} aoo_event_type;

typedef struct aoo_event_header
{
    aoo_event_type type;
    void *endpoint;
    int32_t id;
} aoo_event_header;

// source state event
typedef enum aoo_source_state
{
    AOO_SOURCE_STATE_STOP,
    AOO_SOURCE_STATE_START
} aoo_source_state;

typedef struct aoo_source_state_event
{
    aoo_event_header header;
    int32_t state;
} aoo_source_state_event;

struct _aoo_block_event
{
    aoo_event_header header;
    int32_t count;
};

typedef struct _aoo_block_event aoo_block_loss_event;
typedef struct _aoo_block_event aoo_block_reorder_event;
typedef struct _aoo_block_event aoo_block_resend_event;
typedef struct _aoo_block_event aoo_block_gap_event;

// event union
typedef union aoo_event
{
    aoo_event_type type;
    aoo_event_header header;
    aoo_source_state_event source_state;
    aoo_block_loss_event block_loss;
    aoo_block_reorder_event block_reorder;
    aoo_block_resend_event block_resend;
    aoo_block_gap_event block_gap;
} aoo_event;

typedef void (*aoo_eventhandler)(
    void *,             // user
    const aoo_event *,  // event array
    int32_t             // number of events
);

/*//////////////////// AoO options ////////////////////*/

typedef enum aoo_option
{
    aoo_opt_format = 0,             // aoo_format
    aoo_opt_buffersize,             // int32_t
    aoo_opt_timefilter_bandwidth,      // float
    aoo_opt_channelonset,           // int32_t
    aoo_opt_packetsize,             // int32_t
    aoo_opt_ping_interval,          // int32_t
    aoo_opt_resend_buffersize,      // int32_t
    aoo_opt_resend_limit,           // int32_t
    aoo_opt_resend_interval,        // int32_t
    aoo_opt_resend_maxnumframes     // int32_t
} aoo_option;

#define AOO_ARG(x) &x, sizeof(x)

/*//////////////////// AoO source /////////////////////*/

typedef struct aoo_source aoo_source;

typedef struct aoo_format
{
    const char *codec;
    int32_t nchannels;
    int32_t samplerate;
    int32_t blocksize;
} aoo_format;

typedef struct aoo_format_storage
{
    aoo_format header;
    char buf[256];
} aoo_format_storage;

typedef struct aoo_source_settings
{
    void *userdata;
    aoo_eventhandler eventhandler;
    int32_t samplerate;
    int32_t blocksize;
    int32_t nchannels;
} aoo_source_settings;

aoo_source * aoo_source_new(int32_t id);

void aoo_source_free(aoo_source *src);

int32_t aoo_source_setup(aoo_source *src, const aoo_source_settings *settings);

// will send /AoO/<id>/start message
int32_t aoo_source_addsink(aoo_source *src, void *sink, int32_t id, aoo_replyfn fn);

// will send /AoO/<id>/stop message
int32_t aoo_source_removesink(aoo_source *src, void *sink, int32_t id);

// stop all sinks
void aoo_source_removeall(aoo_source *src);

// e.g. /request
int32_t aoo_source_handlemessage(aoo_source *src, const char *data, int32_t n,
                              void *sink, aoo_replyfn fn);

int32_t aoo_source_send(aoo_source *src);

int32_t aoo_source_process(aoo_source *src, const aoo_sample **data, int32_t n, uint64_t t);

int32_t aoo_source_eventsavailable(aoo_source *src);

int32_t aoo_source_handleevents(aoo_source *src);

int32_t aoo_source_setoption(aoo_source *src, int32_t opt, void *p, int32_t size);

int32_t aoo_source_getoption(aoo_source *src, int32_t opt, void *p, int32_t size);

int32_t aoo_source_setsinkoption(aoo_source *src, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size);

int32_t aoo_source_getsinkoption(aoo_source *src, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size);

/*//////////////////// AoO sink /////////////////////*/

typedef struct aoo_sink aoo_sink;

typedef void (*aoo_processfn)(
        void *,                 // user data
        const aoo_sample **,    // sample data
        int32_t                 // number of samples per channel
);

typedef struct aoo_sink_settings
{
    void *userdata;
    aoo_processfn processfn;
    aoo_eventhandler eventhandler;
    int32_t samplerate;
    int32_t blocksize;
    int32_t nchannels;
} aoo_sink_settings;

aoo_sink * aoo_sink_new(int32_t id);

void aoo_sink_free(aoo_sink *sink);

int32_t aoo_sink_setup(aoo_sink *sink, const aoo_sink_settings *settings);

// Might reply with /AoO/<id>/request
int32_t aoo_sink_handlemessage(aoo_sink *sink, const char *data, int32_t n,
                            void *src, aoo_replyfn fn);

int32_t aoo_sink_process(aoo_sink *sink, uint64_t t);


int32_t aoo_sink_eventsavailable(aoo_sink *sink);

int32_t aoo_sink_handleevents(aoo_sink *sink);

int32_t aoo_sink_setoption(aoo_sink *sink, int32_t opt, void *p, int32_t size);

int32_t aoo_sink_getoption(aoo_sink *sink, int32_t opt, void *p, int32_t size);

int32_t aoo_sink_setsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size);

int32_t aoo_sink_getsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size);

/*//////////////////// Codec //////////////////////////*/

#define AOO_CODEC_MAXSETTINGSIZE 256

typedef void* (*aoo_codec_new)(void);

typedef void (*aoo_codec_free)(void *);

typedef int32_t (*aoo_codec_setformat)(void *, aoo_format *);

typedef int32_t (*aoo_codec_getformat)(void *, aoo_format_storage *);

typedef int32_t (*aoo_codec_writeformat)(
        void *,         // the encoder instance
        int32_t *,      // nchannels
        int32_t *,      // samplerate
        int32_t *,      // blocksize
        char *,         // output buffer
        int32_t         // buffer size
);

typedef int32_t (*aoo_codec_readformat)(
        void *,         // the decoder instance
        int32_t,        // nchannels
        int32_t,        // samplerate
        int32_t,        // blocksize
        const char *,   // input buffer
        int32_t         // number of bytes
);

typedef int32_t (*aoo_codec_encode)(
        void *,             // the encoder instance
        const aoo_sample *, // input samples (interleaved)
        int32_t,            // number of samples
        char *,             // output buffer
        int32_t             // max. size of output buffer
);

typedef int32_t (*aoo_codec_decode)(
        void *,         // the decoder instance
        const char *,   // input bytes
        int32_t,        // input size
        aoo_sample *,   // output samples (interleaved)
        int32_t         // number of samples

);

typedef struct aoo_codec
{
    const char *name;
    // encoder
    aoo_codec_new encoder_new;
    aoo_codec_free encoder_free;
    aoo_codec_setformat encoder_setformat;
    aoo_codec_getformat encoder_getformat;
    aoo_codec_writeformat encoder_writeformat;
    aoo_codec_encode encoder_encode;
    // decoder
    aoo_codec_new decoder_new;
    aoo_codec_free decoder_free;
    aoo_codec_setformat decoder_setformat;
    aoo_codec_getformat decoder_getformat;
    aoo_codec_readformat decoder_readformat;
    aoo_codec_decode decoder_decode;
} aoo_codec;

typedef void (*aoo_codec_registerfn)(const char *, const aoo_codec *);

#ifdef __cplusplus
} // extern "C"
#endif
