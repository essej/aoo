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

#define AOO_MAXPACKETSIZE 4096 // ?
#define AOO_DEFPACKETSIZE 512 // ?
#define AOO_DOMAIN "/AoO"
#define AOO_FORMAT "/format"
#define AOO_FORMAT_NARGS 7
#define AOO_FORMAT_WILDCARD "/AoO/*/format"
#define AOO_DATA "/data"
#define AOO_DATA_NARGS 8
#define AOO_DATA_WILDCARD "/AoO/*/data"
#define AOO_REQUEST "/request"

#ifndef AOO_CLIP_OUTPUT
#define AOO_CLIP_OUTPUT 0
#endif

// 0: error, 1: warning, 2: verbose, 3: debug
#ifndef LOGLEVEL
 #define LOGLEVEL 2
#endif

// time DLL:
// default bandwidth
#ifndef AOO_DLL_BW
 #define AOO_DLL_BW 0.012
#endif

#ifndef AOO_DEBUG_DLL
 #define AOO_DEBUG_DLL 0
#endif

#ifndef AOO_DEBUG_RESAMPLING
 #define AOO_DEBUG_RESAMPLING 0
#endif

typedef void (*aoo_replyfn)(
        void *,         // endpoint
        const char *,   // data
        int32_t         // number of bytes
);

/*/////////////////// PCM codec ////////////////////////*/

#define AOO_CODEC_PCM "pcm"

typedef enum
{
    AOO_INT16,
    AOO_INT24,
    AOO_FLOAT32,
    AOO_FLOAT64
} aoo_pcm_bitdepth;

int32_t aoo_pcm_bytes_per_sample(aoo_pcm_bitdepth bd);

typedef struct
{
    aoo_pcm_bitdepth bitdepth;
} aoo_pcm_settings;

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


/*//////////////////// AoO source /////////////////////*/

#define AOO_SOURCE_DEFBUFSIZE 10

typedef struct aoo_source aoo_source;

typedef struct
{
    int32_t nchannels;
    int32_t samplerate;
    int32_t blocksize;
    const char *codec;
    void *settings;
} aoo_format;

typedef struct
{
    int32_t samplerate;
    int32_t blocksize;
    int32_t nchannels;
    int32_t buffersize;
    int32_t packetsize;
    double time_filter_bandwidth;
} aoo_source_settings;

aoo_source * aoo_source_new(int32_t id);

void aoo_source_free(aoo_source *src);

void aoo_source_setup(aoo_source *src, aoo_source_settings *settings);

void aoo_source_setformat(aoo_source *src, aoo_format *f);

// will send /AoO/<id>/start message
void aoo_source_addsink(aoo_source *src, void *sink, int32_t id, aoo_replyfn fn);

// will send /AoO/<id>/stop message
void aoo_source_removesink(aoo_source *src, void *sink, int32_t id);

// stop all sinks
void aoo_source_removeall(aoo_source *src);

void aoo_source_setsinkchannel(aoo_source *src, void *sink, int32_t id, int32_t chn);

// e.g. /request
void aoo_source_handlemessage(aoo_source *src, const char *data, int32_t n,
                              void *sink, aoo_replyfn fn);

int32_t aoo_source_send(aoo_source *src);

int32_t aoo_source_process(aoo_source *src, const aoo_sample **data, int32_t n, uint64_t t);

/*//////////////////// AoO sink /////////////////////*/

#define AOO_SINK_DEFBUFSIZE 10

typedef struct aoo_sink aoo_sink;

// event types
typedef enum
{
    AOO_SOURCE_STATE_EVENT
} aoo_event_type;

// source state event
typedef enum
{
    AOO_SOURCE_STOP,
    AOO_SOURCE_PLAY
} aoo_source_state;

typedef struct
{
    aoo_event_type type;
    void *endpoint;
    int32_t id;
    aoo_source_state state;
} aoo_source_state_event;

// event union
typedef union
{
    aoo_event_type type;
    aoo_source_state_event source_state;
} aoo_event;

typedef void (*aoo_processfn)(
        void *,                 // user data
        const aoo_sample **,    // sample data
        int32_t,                // number of samples per channel
        const aoo_event *,      // event array
        int32_t                 // number of events
);

typedef struct
{
    void *userdata;
    aoo_processfn processfn;
    int32_t samplerate;
    int32_t blocksize;
    int32_t nchannels;
    int32_t buffersize;
    double time_filter_bandwidth;
} aoo_sink_settings;

aoo_sink * aoo_sink_new(int32_t id);

void aoo_sink_free(aoo_sink *sink);

void aoo_sink_setup(aoo_sink *sink, aoo_sink_settings *settings);

// e.g. /start, /stop, /data.
// Might reply with /AoO/<id>/request
int32_t aoo_sink_handlemessage(aoo_sink *sink, const char *data, int32_t n,
                            void *src, aoo_replyfn fn);

int32_t aoo_sink_process(aoo_sink *sink, uint64_t t);

#ifdef __cplusplus
} // extern "C"
#endif
