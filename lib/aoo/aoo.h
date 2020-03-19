#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef AOO_API
# if defined(_WIN32)
#  if defined(AOO_BUILD)
#     if defined(DLL_EXPORT)
#       define AOO_API __declspec(dllexport) __cdecl
#     else
#       define AOO_API __cdecl
#     endif
#  else
#   define AOO_API __declspec(dllimport) __cdecl
#  endif
# elif defined(__GNUC__) && defined(AOO_BUILD)
#  define AOO_API __attribute__ ((visibility ("default"))) __cdecl
# else
#  define AOO_API __cdecl
# endif
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

/*////////// default values ////////////*/

// max. UDP packet size
#ifndef AOO_PACKETSIZE
 #define AOO_PACKETSIZE 512
#endif

// source buffer size in ms
#ifndef AOO_SOURCE_BUFSIZE
 #define AOO_SOURCE_BUFSIZE 10
#endif

// sink buffer size in ms
#ifndef AOO_SINK_BUFSIZE
 #define AOO_SINK_BUFSIZE 100
#endif

// time DLL filter bandwidth
#ifndef AOO_TIMEFILTER_BANDWIDTH
 #define AOO_TIMEFILTER_BANDWIDTH 0.012
#endif

// ping interval (sink to source) in ms
#ifndef AOO_PING_INTERVAL
 #define AOO_PING_INTERVAL 1000
#endif

// resend buffer size in ms
#ifndef AOO_RESEND_BUFSIZE
 #define AOO_RESEND_BUFSIZE 1000
#endif

// max. number of resend attempts per packet
#ifndef AOO_RESEND_LIMIT
 #define AOO_RESEND_LIMIT 5
#endif

// interval between resend attempts in ms
#ifndef AOO_RESEND_INTERVAL
 #define AOO_RESEND_INTERVAL 10
#endif

// max. number of frames to request per call
#ifndef AOO_RESEND_MAXNUMFRAMES
 #define AOO_RESEND_MAXNUMFRAMES 64
#endif

// setup AoO library - call only once!
AOO_API void aoo_setup(void);

// close AoO library - call only once!
AOO_API void aoo_close(void);

struct aoo_format;

/*//////////////////// OSC ////////////////////////////*/

// id: the source or sink ID
// returns: the offset to the remaining address pattern

#define AOO_ID_WILDCARD -1
#define AOO_ID_NONE INT32_MIN

// get the ID from an AoO OSC message, e.g. in /AoO/<id>/data
// returns 1 on success, 0 on fail
AOO_API int32_t aoo_parsepattern(const char *msg, int32_t n, int32_t *id);

// get the current NTP time
AOO_API uint64_t aoo_osctime_get(void);

// convert NTP time to seconds
AOO_API double aoo_osctime_toseconds(uint64_t t);

// convert seconds to NTP time
AOO_API uint64_t aoo_osctime_fromseconds(double s);

// add seconds to NTP timestamp
AOO_API uint64_t aoo_osctime_addseconds(uint64_t t, double s);

// reply function for endpoints
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
    // source: received a ping from sink
    AOO_PING_EVENT,
    // sink: source format changed
    AOO_FORMAT_EVENT,
    // sink: source changed state
    AOO_SOURCE_STATE_EVENT,
    // sink: blocks have been lost
    AOO_BLOCK_LOSS_EVENT,
    // sink: blocks arrived out of order
    AOO_BLOCK_REORDER_EVENT,
    // sink: blocks have been resent
    AOO_BLOCK_RESEND_EVENT,
    // sink: large gap between blocks
    AOO_BLOCK_GAP_EVENT
} aoo_event_type;

typedef struct aoo_event_header
{
    aoo_event_type type;
    void *endpoint;
    int32_t id;
} aoo_event_header;

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
    // Stream format (set: aoo_format, get: aoo_format_storage)
    // ---
    // The settings for the audio codec to be used for a stream.
    // If you want to set the format, you have to send the format
    // header, e.g. aoo_format_pcm.header. This is only allowed
    // for sources.
    // If you want to get the format, you have to pass a
    // aoo_format_storage, which is filled with the format.
    aoo_opt_format = 0,
    // Buffer size in ms (int32_t)
    // ---
    // This is the size of the ring buffer
    // between the audio and network thread.
    // For the source, this can be rather small,
    // as you only have to compensate the latency
    // for thread signalling.
    // For the sink, a larger buffer size helps
    // to deal with network jitter, packet reordering
    // and packet loss. For local networks, small
    // buffersizes between 10-50ms should work,
    // but for unreliable/unpredictable networks
    // you might need to increased it significantly.
    aoo_opt_buffersize,
    // Time filter DLL bandwidth (float)
    // ---
    // The time DLL filter estimates the effective samplerate
    // and is used to compensate clock drift via dynamic resampling.
    // See the paper "Using a DLL to filter time" by Fons Adriaensen.
    aoo_opt_timefilter_bandwidth,
    // Sink channel onset (int32_t)
    // ---
    // The channel onset of the sink where a given source
    // should be received. For example, if the channel onset
    // is 5, a 2-channel source will be summed into sink
    // channels 5 and 6. The default is 0 (= the first channel).
    aoo_opt_channelonset,
    // Max. UDP packet size in bytes (int32_t)
    // ---
    // The default value of 512 should work across most
    // networks (even the internet). You might increase
    // this value for local networks. Larger packet sizes
    // have less overhead. If a audio block exceeds
    // the max. UDP packet size, it will be automatically
    // broken up into several "frames" in reassembled
    // in the sink.
    aoo_opt_packetsize,
    // Ping interval in ms (int32_t)
    // ---
    // The sink sends a periodic ping message to each
    // source to signify that it is actually receiving data.
    // For example, a application might choose to remove
    // a sink after the source hasn't received a ping
    // for a certain amount of time.
    aoo_opt_ping_interval,
    // Resend buffer size in ms (int32_t).
    // ---
    // The source keeps the last N ms of audio in a buffer,
    // so it can resend parts of it if requested, e.g. to
    // handle packet loss.
    aoo_opt_resend_buffersize,
    // Resend limit (int32_t)
    // ---
    // The max. number of resend attempts per frame.
    // The sink will stop to request a missing frame
    // after this limit has been reached.
    // If set to 0, resending is effectively disabled.
    aoo_opt_resend_limit,           // int32_t
    // Resend interval in ms (int32_t)
    // ---
    // This is the interval between individual resend
    // attempts for a specific frame.
    // Since there is always a certain roundtrip delay
    // between source and sink, it makes sense to wait
    // between resend attempts to not spam the network
    // with redundant /resend messages.
    aoo_opt_resend_interval,
    // Max. number of frames to resend (int32_t)
    // ---
    // This is the max. number of frames to request
    // in a single call to sink_handlemessage().
    aoo_opt_resend_maxnumframes
} aoo_option;

#define AOO_ARG(x) &x, sizeof(x)

/*//////////////////// AoO source /////////////////////*/

#ifdef __cplusplus
namespace aoo {
    class isource;
}
using aoo_source = aoo::isource;
#else
typedef struct aoo_source aoo_source;
#endif

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

// create a new AoO source instance
AOO_API aoo_source * aoo_source_new(int32_t id);

// destroy the AoO source instance
AOO_API void aoo_source_free(aoo_source *src);

AOO_API int32_t aoo_source_setup(aoo_source *src, const aoo_source_settings *settings);

// Call from any thread - synchronize with network and audio thread!
AOO_API int32_t aoo_source_addsink(aoo_source *src, void *sink, int32_t id, aoo_replyfn fn);

// Call from any thread - synchronize with network and audio thread!
AOO_API int32_t aoo_source_removesink(aoo_source *src, void *sink, int32_t id);

// Call from any thread - synchronize with network and audio thread!
AOO_API void aoo_source_removeall(aoo_source *src);

// Call from the network thread.
AOO_API int32_t aoo_source_handlemessage(aoo_source *src, const char *data, int32_t n,
                                 void *sink, aoo_replyfn fn);

// Call from the network thread.
AOO_API int32_t aoo_source_send(aoo_source *src);

// Call from the audio thread.
// data:        array of channel data (non-interleaved)
// nsamples:    number of samples per channel
// t:           current NTP timestamp (see aoo_osctime_get)
AOO_API int32_t aoo_source_process(aoo_source *src, const aoo_sample **data,
                           int32_t nsamples, uint64_t t);

// Call from any thread - always thread safe!
AOO_API int32_t aoo_source_eventsavailable(aoo_source *src);

// Call from any thread - always thread safe!
AOO_API int32_t aoo_source_handleevents(aoo_source *src);

// Call from any thread - synchronize with network and audio thread!
AOO_API int32_t aoo_source_setoption(aoo_source *src, int32_t opt, void *p, int32_t size);

AOO_API int32_t aoo_source_getoption(aoo_source *src, int32_t opt, void *p, int32_t size);

AOO_API int32_t aoo_source_setsinkoption(aoo_source *src, void *endpoint, int32_t id,
                                 int32_t opt, void *p, int32_t size);

AOO_API int32_t aoo_source_getsinkoption(aoo_source *src, void *endpoint, int32_t id,
                                 int32_t opt, void *p, int32_t size);

/*//////////////////// AoO sink /////////////////////*/

#ifdef __cplusplus
namespace aoo {
    class isink;
}
using aoo_sink = aoo::isink;
#else
typedef struct aoo_sink aoo_sink;
#endif

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

// create a new AoO sink instance
AOO_API aoo_sink * aoo_sink_new(int32_t id);

// destroy the AoO sink instance
AOO_API void aoo_sink_free(aoo_sink *sink);

// Call from any thread - synchronize with network and audio thread!
AOO_API int32_t aoo_sink_setup(aoo_sink *sink, const aoo_sink_settings *settings);

// Call from the network thread.
AOO_API int32_t aoo_sink_handlemessage(aoo_sink *sink, const char *data, int32_t n,
                            void *src, aoo_replyfn fn);

// Call from the audio thread.
AOO_API int32_t aoo_sink_process(aoo_sink *sink, uint64_t t);

// Call from any thread - always thread safe!
AOO_API int32_t aoo_sink_eventsavailable(aoo_sink *sink);

// Call from any thread - always thread safe!
AOO_API int32_t aoo_sink_handleevents(aoo_sink *sink);

// Call from any thread - synchronize with network and audio thread!
AOO_API int32_t aoo_sink_setoption(aoo_sink *sink, int32_t opt, void *p, int32_t size);

AOO_API int32_t aoo_sink_getoption(aoo_sink *sink, int32_t opt, void *p, int32_t size);

AOO_API int32_t aoo_sink_setsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size);

AOO_API int32_t aoo_sink_getsourceoption(aoo_sink *sink, void *endpoint, int32_t id,
                              int32_t opt, void *p, int32_t size);

/*//////////////////// Codec API //////////////////////////*/

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

// register an external codec plugin
AOO_API int32_t aoo_register_codec(const char *name, const aoo_codec *codec);

// The type of 'aoo_register_codec', which gets passed to codec setup functions.
// For now, plugins are registered statically - or manually by the user.
// Later we might want to automatically look for codec plugins.
typedef int32_t (*aoo_codec_registerfn)(const char *, const aoo_codec *);

#ifdef __cplusplus
} // extern "C"
#endif
