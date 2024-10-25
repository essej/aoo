/** \file simple_sender.c
    \ingroup examples_c
    \brief A simple AOO sender
    \author Christof Ressi <info@christofressi.com>

This is a full working example of a simple AOO sender
written in C.

The `simple_sender` program sends a stream to an AOO
sink specified with the [HOSTNAME PORT] [ID]` arguments.
If the arguments are omitted, the program waits passively
for an invitation.

You can try this example together with the `simple_receiver`
example (simple_receiver.c).

\note If you want to run multiple instances of `simple_sender`
you must give them distinct IDs with the `--id` option.

The sound source can either be sine wave oscillators
with random frequencies (`--mode sine`) or the audio input
(`--mode input`).

You can also set the streaming format with the `--codec`
and `--block` options.

Run `simple_sender -h` to see all available options.

*/


#include "aoo.h"
#include "aoo_source.h"
#include "aoo_client.h"
#include "codec/aoo_pcm.h"
#if AOO_USE_OPUS
#include "codec/aoo_opus.h"
#endif

// common utilities
#include "utils.h"

#include "portaudio.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
# include <signal.h>
# include <stdio.h>
#endif

#ifndef _MSC_VER
#include <pthread.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SLEEP_GRAIN 100

#define DEFAULT_ID 1
#define DEFAULT_PORT 9998

#define DEFAULT_CODEC kAooCodecPcm
#define DEFAULT_BLOCK_SIZE 256
#define DEFAULT_BUFFER_SIZE 1024

#define DEFAULT_MODE MODE_SINE
#define SINE_GAIN 0.25

enum {
    MODE_INPUT,
    MODE_SINE
};

typedef struct SimpleSenderOptions {
    int devno;
    int port;
    AooId id;
    int mode;
    int buffersize;
    const char *codec;
    int blocksize;
    const char *sink_host;
    int sink_port;
    int sink_id;
} SimpleSenderOptions;

typedef struct SimpleSender {
    PaStream *stream;
    AooSource *source;
    AooClient *client;

    double sr; // sample rate
    int channels; // number of channels
    int mode; // audio input or sine wave

    double *freq;
    double *last_phase;
    AooSample **process_buffer;

#ifdef _MSC_VER
    HANDLE send_thread;
    HANDLE receive_thread;
#else
    pthread_t send_thread;
    pthread_t receive_thread;
#endif
    bool threads_running;
} SimpleSender;

// audio callback passed to Pa_OpenStream()
PaError callback(const void *input, void *output, unsigned long frameCount,
                 const PaStreamCallbackTimeInfo* timeInfo,
                 PaStreamCallbackFlags statusFlags, void *userData)
{
    SimpleSender *x = (SimpleSender*)userData;

    // sample the current NTP time
    AooNtpTime t = aoo_getCurrentNtpTime();

    if (x->mode == MODE_INPUT) {
        // forward audio input
        AooSource_process(x->source, (float **)input, frameCount, t);
    } else if (x->mode == MODE_SINE) {
        // make sine waves
        for (int i = 0; i < x->channels; i++) {
            double advance = x->freq[i] / x->sr;
            double phase = x->last_phase[i];
            AooSample *buf = x->process_buffer[i];
            for (int j = 0; j < frameCount; j++) {
                buf[j] = sin(phase * 2.0 * M_PI) * SINE_GAIN;
                phase += advance;
            }
            x->last_phase[i] = fmod(phase, 1.0);
        }

        AooSource_process(x->source, x->process_buffer, frameCount, t);
    }
    // tell the client that there may be data to send out
    AooClient_notify(x->client);

    return paContinue;
}

// network thread functions
// NB: C11 threads are still not supported some platforms,
// so we use platform-specific thread APIs, i.e. Win32 threads
// for MSVC and pthreads for everything else.

#ifdef _MSC_VER

DWORD WINAPI do_send(LPVOID lpParam) {
    SimpleSender *x = (SimpleSender *)lpParam;
    AooClient_send(x->client, kAooInfinite);
    return 0;
}

DWORD WINAPI do_receive(LPVOID lpParam) {
    SimpleSender *x = (SimpleSender *)lpParam;
    AooClient_receive(x->client, kAooInfinite);
    return 0;
}

#else

void *do_send(void *y) {
    SimpleSender *x = (SimpleSender *)y;
    AooClient_send(x->client, kAooInfinite);
    return 0;
}

void *do_receive(void *y) {
    SimpleSender *x = (SimpleSender *)y;
    AooClient_receive(x->client, kAooInfinite);
    return 0;
}

#endif

// example event handler
void AOO_CALL handle_event(void *user, const AooEvent *event,
                            AooThreadLevel level)
{
    SimpleSender *x = (SimpleSender *)user;

    AooChar ipstring[64];
    AooSize ipsize = 64;
    AooUInt16 port;
    AooId id;

    switch (event->type) {
    case kAooEventSinkPing:
    {
        // just post a message to the console together with the endpoint and the RTT
        aoo_sockAddrToIpEndpoint(event->sinkPing.endpoint.address,
                                 event->sinkPing.endpoint.addrlen,
                                 ipstring, &ipsize, &port, 0);
        id = event->sinkPing.endpoint.id;
        AooSeconds rtt = aoo_ntpTimeDuration(event->sinkPing.t1, event->sinkPing.t2);

        fprintf(stdout, "got ping from sink [%s]:%d|%d\n", ipstring, port, id);
        fprintf(stdout, "RTT: %f ms\n", rtt * 1000.0);

        break;
    }
    case kAooEventInvite:
    {
        // post message to console and accept invitation
        aoo_sockAddrToIpEndpoint(event->invite.endpoint.address,
                                 event->invite.endpoint.addrlen,
                                 ipstring, &ipsize, &port, 0);
        id = event->invite.endpoint.id;

        fprintf(stdout, "accept invitation from sink [%s]:%d|%d\n",
                ipstring, port, id);

        AooSource_handleInvite(x->source, &event->invite.endpoint,
                               event->invite.token, kAooTrue);

        break;
    }
    case kAooEventUninvite:
    {
        // post message to console and accept uninvitation
        aoo_sockAddrToIpEndpoint(event->uninvite.endpoint.address,
                                 event->uninvite.endpoint.addrlen,
                                 ipstring, &ipsize, &port, 0);
        id = event->uninvite.endpoint.id;

        fprintf(stdout, "accept uninvitation from sink [%s]:%d|%d\n",
                ipstring, port, id);

        AooSource_handleUninvite(x->source, &event->uninvite.endpoint,
                                 event->uninvite.token, kAooTrue);

        break;
    }
    default:
        break;
    }
}

int SimpleSender_init(SimpleSender *x,
                      const SimpleSenderOptions *opt) {
    x->stream = 0;
    x->source = 0;
    x->client = 0;
    x->mode = opt->mode;
    x->freq = 0;
    x->last_phase = 0;
    x->process_buffer = 0;
    x->send_thread = x->receive_thread = 0;
    x->threads_running = false;

    // get audio device
    audio_device_info dev;
    get_audio_input_device(opt->devno, &dev);

    x->channels = dev.channels;
    x->sr = dev.sample_rate;
    int buffersize = opt->buffersize;

    if (x->mode == MODE_SINE) {
        // make random frequencies between 220 and 880 Hz
        x->freq = (double*)malloc(x->channels * sizeof(double));
        x->last_phase = (double*)malloc(x->channels * sizeof(double));
        for (int i = 0; i < x->channels; i++) {
            x->freq[i] = 220.0 * pow(2.0, randf() * 2.0);
            x->last_phase[i] = 0;
        }
        // create buffers
        x->process_buffer = (AooSample**)malloc(x->channels * sizeof(AooSample *));
        for (int i = 0; i < x->channels; i++) {
            x->process_buffer[i] = (AooSample*)malloc(buffersize * sizeof(AooSample));
        }
    }

    // create AOO source and set event handler
    x->source = AooSource_new(opt->id);
    AooSource_setEventHandler(x->source, handle_event, x, kAooEventModeCallback);

    // create and setup AOO client
    x->client = AooClient_new();

    AooClientSettings settings = AOO_CLIENT_SETTINGS_INIT();
    settings.portNumber = opt->port;
    AooError err = AooClient_setup(x->client, &settings);
    if (err == kAooOk) {
        fprintf(stdout, "sending from port %d with ID %d...\n",
                opt->port, opt->id);
    } else {
        fprintf(stdout, "network setup failed: %s\n",
                aoo_strerror(err));
        return 1;
    }

    // add source to client
    AooClient_addSource(x->client, x->source);

    // add sink to source, if provided by the user
    if (opt->sink_host) {
        // first convert IP address string (e.g. "127.0.0.1") resp. hostname
        // (e.g. "localhost") to socket address.
        // NB: if we only allowed IP address strings, we could use the (faster)
        // aoo_ipEndpointToSockAddr() function instead.
        AooSockAddrStorage addr;
        AooAddrSize addrlen = sizeof(addr);
        if (aoo_resolveIpEndpoint(opt->sink_host, opt->sink_port,
                                  settings.socketType,
                                   &addr, &addrlen) != kAooOk) {
            // could not resolve host name resp. got invalid IP string
            AooInt32 code = 0;
            AooChar buf[256];
            AooSize size = sizeof(buf);
            aoo_getLastSocketError(&code, buf, &size);
            fprintf(stdout, "could not resolve host name '%s': %s\n",
                    opt->sink_host, buf);
            return 1;
        }

        // now make sink endpoint and add it to the source
        AooEndpoint ep = { &addr, addrlen, opt->sink_id };

        AooSource_addSink(x->source, &ep, kAooTrue);

        fprintf(stdout, "sending to [%s]:%d|%d from port %d...\n",
                opt->sink_host, opt->sink_port, opt->sink_id, opt->port);
    }

    // portaudio input stream parameters
    PaStreamParameters params;
    params.device = dev.index;
    params.channelCount = x->channels;
    params.sampleFormat = paFloat32 | paNonInterleaved;
    params.suggestedLatency = 0;
    params.hostApiSpecificStreamInfo = 0;

    fprintf(stdout, "open %s (%d channels, %d Hz, %d samples)\n",
            dev.display_name, x->channels, (int)x->sr, buffersize);

    // try to open portaudio stream
    PaError err2 = Pa_OpenStream(&x->stream, &params, 0, x->sr,
                                buffersize, 0, callback, x);
    if (err2 != paNoError) {
        fprintf(stdout, "could not open stream: %s\n",
                Pa_GetErrorText(err2));
        return 1;
    }

    // setup the AOO source with the same settings as the portaudio stream.
    AooSource_setup(x->source, x->channels, x->sr, buffersize, 0);

    // set AOO stream format; use the same number of channels and samplerate
    // as the portaudio stream.
    if (!strcmp(opt->codec, kAooCodecPcm)) {
        AooFormatPcm fmt;
        AooFormatPcm_init(&fmt, x->channels, x->sr, opt->blocksize, kAooPcmInt16);
        AooSource_setFormat(x->source, &fmt.header);
        fprintf(stdout, "PCM stream: %d channels, %d Hz, %d samples\n",
                fmt.header.numChannels, fmt.header.sampleRate, fmt.header.blockSize);
#if AOO_USE_OPUS
    } else if (!strcmp(opt->codec, kAooCodecOpus)) {
        AooFormatOpus fmt;
        AooFormatOpus_init(&fmt, x->channels, x->sr, opt->blocksize, OPUS_APPLICATION_AUDIO);
        AooSource_setFormat(x->source, &fmt.header);
        fprintf(stdout, "Opus stream: %d channels, %d Hz, %d samples\n",
                fmt.header.numChannels, fmt.header.sampleRate, fmt.header.blockSize);
#endif
    } else {
        fprintf(stdout, "unknown/unsupported codec '%s'\n", opt->codec);
        return 1;
    }

    // start the network threads
#ifdef _MSC_VER
    x->send_thread = CreateThread(0, 0, do_send, x, 0, 0);
    x->receive_thread = CreateThread(0, 0, do_receive, x, 0, 0);
#else
    pthread_create(&x->send_thread, 0, do_send, x);
    pthread_create(&x->receive_thread, 0, do_receive, x);
#endif
    x->threads_running = true;

    return 0;
}

void SimpleSender_start(SimpleSender *x) {
    // start the portaudio stream
    assert(x->stream);
    Pa_StartStream(x->stream);
    // also start the AOO stream
    assert(x->source);
    AooSource_startStream(x->source, 0, 0);
}

void SimpleSender_stop(SimpleSender *x) {
    // Stop the AOO stream.
    // NB: This is not strictly necessary because the sink would automatically
    // remove the source after a few seconds of inactivity, but in this example
    // we want the sink to output a AooEventStreamStop event, see handle_event()
    // in simple_receiver.c.
    assert(x->source);
    AooSource_stopStream(x->source, 0);
    // HACK: sleep a little bit so the client has a chance to actually send
    // the /stop message. Do this while the portaudio stream is still running!
    Pa_Sleep(SLEEP_GRAIN);

    // stop the portaudio stream.
    fprintf(stdout, "stop audio\n");
    assert(x->stream);
    Pa_StopStream(x->stream);
}

void SimpleSender_free(SimpleSender *x) {
    if (x->threads_running) {
        // stop the client and join network threads.
        assert(x->client);
        AooClient_stop(x->client);
#ifdef _MSC_VER
        WaitForSingleObject(x->send_thread, INFINITE);
        WaitForSingleObject(x->receive_thread, INFINITE);
        CloseHandle(x->send_thread);
        CloseHandle(x->receive_thread);
#else
        pthread_join(x->send_thread, 0);
        pthread_join(x->receive_thread, 0);
#endif
    }

    // free buffers for sine wave (may be NULL)
    free(x->freq);
    free(x->last_phase);
    if (x->process_buffer) {
        for (int i = 0; i < x->channels; ++i) {
            free(x->process_buffer[i]);
        }
        free(x->process_buffer);
    }

    // close audio stream
    if (x->stream) {
        fprintf(stdout, "close audio\n");
        Pa_CloseStream(x->stream);
    }

    // free source and client objects
    if (x->source) AooSource_free(x->source);
    if (x->client) AooClient_free(x->client);
}

void print_usage(void) {
    fprintf(stdout,
            "usage: simple_sender [OPTIONS]... [HOSTNAME PORT] [ID]\n"
            "  -h, --help    show help\n"
            "  -l, --list    list output devices\n"
            "  -p, --port    UDP port (default = %d)\n"
            "  -I, --id      source ID (default = %d)\n"
            "  -i, --input   input device number\n"
            "  -b, --buffer  buffer size in samples\n"
#if AOO_USE_OPUS
            "  -c, --codec   codec name ('pcm' or 'opus')\n"
#endif
            "  -B, --block   stream block size in samples\n"
            "  -m, --mode    'input' or 'sine'\n",
            DEFAULT_PORT, DEFAULT_ID);
}

// entry point
int main(int argc, const char **argv) {
    // default settings
    SimpleSenderOptions opt;
    opt.devno = -1; // use default device
    opt.port = DEFAULT_PORT;
    opt.id = DEFAULT_ID;
    opt.mode = DEFAULT_MODE;
    opt.buffersize = DEFAULT_BUFFER_SIZE;
    opt.codec = DEFAULT_CODEC;
    opt.blocksize = DEFAULT_BLOCK_SIZE;
    // optional sink to send to
    opt.sink_host = 0;
    opt.sink_port = 0;
    opt.sink_id = DEFAULT_ID;

    // parse command line arguments
    argc--; argv++;
    while (argc && argv[0][0] == '-') {
        if (match_option(argv[0], "-h", "--help")) {
            print_usage();
            return EXIT_SUCCESS;
        } else if (match_option(argv[0], "-l", "--list")) {
            Pa_Initialize();
            print_input_devices();
            Pa_Terminate();
            return EXIT_SUCCESS;
        } else if (match_option(argv[0], "-p", "--port")) {
            if (argc < 2 || !parse_int(argv[1], &opt.port)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-I", "--id")) {
            if (argc < 2 || !parse_int(argv[1], &opt.id)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-i", "--input")) {
            if (argc < 2 || !parse_int(argv[1], &opt.devno)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-b", "--buffer")) {
            if (argc < 2 || !parse_int(argv[1], &opt.buffersize)) {
                goto fail;
            }
            argc -= 2; argv += 2;
#if AOO_USE_OPUS
        } else if (match_option(argv[0], "-c", "--codec")) {
            if (argc < 2) {
                goto fail;
            }
            opt.codec = argv[1];
            argc -= 2; argv += 2;
#endif
        } else if (match_option(argv[0], "-B", "--block")) {
            if (argc < 2 || !parse_int(argv[1], &opt.blocksize)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-m", "--mode")) {
            if (argc < 2) {
                goto fail;
            }
            if (!strcmp(argv[1], "input")) {
                opt.mode = MODE_INPUT;
            } else if (!strcmp(argv[1], "sine")) {
                opt.mode = MODE_SINE;
            } else {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else {
            fprintf(stdout, "unknown option '%s'\n", argv[0]);
            goto fail;
        }
    }

    // optional positional arguments [HOST PORT] [ID]
    if (!get_endpoint_argument(argv, argc, &opt.sink_host,
                               &opt.sink_port, &opt.sink_id)) {
        goto fail;
    }

    setup();
    // initialize portaudio library
    Pa_Initialize();
    // initialize AOO library
    aoo_initialize(0);

    SimpleSender x;
    int ret = SimpleSender_init(&x, &opt);
    if (ret == 0) {
        // start
        SimpleSender_start(&x);
        // wait for the user to press Ctrl+C
        wait_for_quit();
        // stop
        SimpleSender_stop(&x);
    }
    SimpleSender_free(&x);

    // deinitialize AOO library
    aoo_terminate();
    // deinitialize portaudio library
    Pa_Terminate();

    return ret;

fail:
    print_usage();
    return EXIT_FAILURE;
}
