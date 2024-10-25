/** \file simple_receiver.c
    \ingroup examples_c
    \brief A simple AOO receiver
    \author Christof Ressi <info@christofressi.com>

This is a full working example of a simple AOO receiver
written in C.

The `simple_receiver` program receives one or more AOO
streams. By default, it waits passively for AOO sources
to send, but you can also *invite* a specific source with
the optional `[HOSTNAME PORT] [ID]` arguments.

You can try this example together with the `simple_sender`
example (simple_sender.c).

\note If you want to run multiple instances of `simple_receiver`
you must give them distinct IDs with the `--id` option.

Run `simple_receiver -h` to see all available options.

*/

#include "aoo.h"
#include "aoo_sink.h"
#include "aoo_client.h"

// common utilities
#include "utils.h"

#include "portaudio.h"

#include <assert.h>
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

#define SLEEP_GRAIN 100

#define DEFAULT_ID 1
#define DEFAULT_PORT 9999

#define DEFAULT_BUFFER_SIZE 1024
#define DEFAULT_LATENCY 100

typedef struct SimpleReceiverOptions {
    int devno;
    int port;
    AooId id;
    int latency;
    int buffersize;
    const char *source_host;
    int source_port;
    int source_id;
} SimpleReceiverOptions;

typedef struct SimpleReceiver {
    PaStream *stream;
    AooSink *sink;
    AooClient *client;

    int latency;

    const char *source_host;
    int source_port;
    int source_id;
    AooSockAddrStorage source_addr;
    AooAddrSize source_size;

#ifdef _MSC_VER
    HANDLE send_thread;
    HANDLE receive_thread;
#else
    pthread_t send_thread;
    pthread_t receive_thread;
#endif
    bool threads_running;
} SimpleReceiver;

// audio callback passed to Pa_OpenStream()
PaError callback(const void *input, void *output, unsigned long frameCount,
                 const PaStreamCallbackTimeInfo* timeInfo,
                 PaStreamCallbackFlags statusFlags, void *userData)
{
    SimpleReceiver *x = (SimpleReceiver*)userData;

    // sample the current NTP time
    AooNtpTime t = aoo_getCurrentNtpTime();
    // write stream directy to audio output
    AooSink_process(x->sink, (float **)output, frameCount, t, 0, 0);
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
    SimpleReceiver *x = (SimpleReceiver *)lpParam;
    AooClient_send(x->client, kAooInfinite);
    return 0;
}

DWORD WINAPI do_receive(LPVOID lpParam) {
    SimpleReceiver *x = (SimpleReceiver *)lpParam;
    AooClient_receive(x->client, kAooInfinite);
    return 0;
}

#else

void *do_send(void *y) {
    SimpleReceiver *x = (SimpleReceiver *)y;
    AooClient_send(x->client, kAooInfinite);
    return 0;
}

void *do_receive(void *y) {
    SimpleReceiver *x = (SimpleReceiver *)y;
    AooClient_receive(x->client, kAooInfinite);
    return 0;
}

#endif

// example event handler that just posts messages to the console
void AOO_CALL handle_event(void *user, const AooEvent *event,
                            AooThreadLevel level)
{
    AooChar ipstring[64];
    AooSize ipsize = 64;
    AooUInt16 port;
    AooId id;

    switch (event->type) {
    case kAooEventStreamStart:
    {
        aoo_sockAddrToIpEndpoint(event->streamStart.endpoint.address,
                                 event->streamStart.endpoint.addrlen,
                                 ipstring, &ipsize, &port, 0);
        id = event->streamStart.endpoint.id;

        fprintf(stdout, "start stream from source [%s]:%d|%d\n", ipstring, port, id);

        break;
    }
    case kAooEventStreamStop:
    {
        aoo_sockAddrToIpEndpoint(event->streamStop.endpoint.address,
                                 event->streamStop.endpoint.addrlen,
                                 ipstring, &ipsize, &port, 0);
        id = event->streamStart.endpoint.id;

        fprintf(stdout, "stop stream from source [%s]:%d|%d\n", ipstring, port, id);

        break;
    }
    case kAooEventFormatChange:
    {
        const AooFormat *f = event->formatChange.format;

        fprintf(stdout, "format changed: '%s' codec, %d channels, %d Hz, %d samples\n",
                f->codecName, f->numChannels, (int)f->sampleRate, f->blockSize);

        break;
    }
    default:
        break;
    }
}

int SimpleReceiver_init(SimpleReceiver *x,
                        const SimpleReceiverOptions *opt) {
    x->stream = 0;
    x->sink = 0;
    x->client = 0;
    x->latency = opt->latency;
    x->source_host = opt->source_host;
    x->source_port = opt->source_port;
    x->source_id = opt->source_id;
    x->source_size = 0;
    x->send_thread = x->receive_thread = 0;
    x->threads_running = false;

    // get audio device
    audio_device_info dev;
    get_audio_output_device(opt->devno, &dev);

    // create AOO sink, set event handler and set jitter buffer latency.
    x->sink = AooSink_new(opt->id);
    AooSink_setEventHandler(x->sink, handle_event, x, kAooEventModeCallback);
    AooSink_setLatency(x->sink, x->latency * 0.001);

    // create and setup AOO client
    x->client = AooClient_new();

    AooClientSettings settings = AOO_CLIENT_SETTINGS_INIT();
    settings.portNumber = opt->port;
    AooError err = AooClient_setup(x->client, &settings);
    if (err == kAooOk) {
        fprintf(stdout, "receiving on port %d with ID %d...\n",
                opt->port, opt->id);
    } else {
        fprintf(stdout, "could not setup client: %s\n",
                aoo_strerror(err));
        return 1;
    }

    // add sink to client
    AooClient_addSink(x->client, x->sink);

    // invite source, if provided by the user
    if (opt->source_host) {
        // first convert IP address string (e.g. "127.0.0.1") resp. hostname
        // (e.g. "localhost") to socket address.
        // NB: if we only allowed IP address strings, we could use the (faster)
        // aoo_ipEndpointToSockAddr() instead.
        x->source_size = sizeof(x->source_addr);
        if (aoo_resolveIpEndpoint(opt->source_host, opt->source_port,
                                  settings.socketType, &x->source_addr,
                                  &x->source_size) != kAooOk) {
            // could not resolve host name resp. got invalid IP string
            AooInt32 code = 0;
            AooChar buf[256];
            AooSize size = sizeof(buf);
            aoo_getLastSocketError(&code, buf, &size);
            fprintf(stdout, "could not resolve host name '%s': %s",
                    opt->source_host, buf);
        }
    }

    // portaudio output stream parameters
    PaStreamParameters params;
    params.device = dev.index;
    params.channelCount = dev.channels;
    params.sampleFormat = paFloat32 | paNonInterleaved;
    params.suggestedLatency = 0;
    params.hostApiSpecificStreamInfo = 0;

    int sr = (int)dev.sample_rate;

    fprintf(stdout, "open %s (%d channels, %d Hz, %d samples)\n",
            dev.display_name, dev.channels, sr, opt->buffersize);

    // try to open portaudio stream
    PaError err2 = Pa_OpenStream(&x->stream, 0, &params, sr,
                                 opt->buffersize, 0, callback, x);
    if (err2 != paNoError) {
        fprintf(stdout, "could not open stream: %s\n",
                Pa_GetErrorText(err2));
        return 1;
    }

    // setup the AOO sink with the same settings as the portaudio stream.
    AooSink_setup(x->sink, dev.channels, sr, opt->buffersize, 0);

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

void SimpleReceiver_start(SimpleReceiver *x) {
    // start the portaudio stream
    assert(x->stream);
    Pa_StartStream(x->stream);

    if (x->source_host) {
        // invite AOO source
        AooEndpoint ep = { &x->source_addr, x->source_size, x->source_id };

        assert(x->sink);
        AooSink_inviteSource(x->sink, &ep, 0);

        fprintf(stdout, "invite source [%s]:%d|%d\n",
                x->source_host, x->source_port, x->source_id);
    }
}

void SimpleReceiver_stop(SimpleReceiver *x) {
    if (x->source_host) {
        fprintf(stdout, "uninvite sources\n");
        // Uninvite all sources. The source(s) will output a kAooEventUninvite
        // event, see handle_event() in SimpleReceiver.c.
        // NB: If we don't uninvite, the source(s) will just keep sending data!
        assert(x->sink);
        AooSink_uninviteAll(x->sink);
        // HACK: sleep a little bit so the client has a chance to actually
        // send the /uninvite message and receive the /stop message.
        // Do this while the portaudio stream is still running!
        Pa_Sleep(SLEEP_GRAIN + x->latency);
    }
    // stop the portaudio stream.
    fprintf(stdout, "stop audio\n");
    assert(x->stream);
    Pa_StopStream(x->stream);
}

void SimpleReceiver_free(SimpleReceiver *x) {
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

    // close audio stream
    if (x->stream) {
        fprintf(stdout, "close audio\n");
        Pa_CloseStream(x->stream);
    }

    // free source and client objects
    if (x->sink) AooSink_free(x->sink);
    if (x->client) AooClient_free(x->client);
}

void print_usage(void) {
    fprintf(stdout,
            "usage: simple_receiver [OPTIONS]... [HOSTNAME PORT] [ID]\n"
            "  -h, --help    show help\n"
            "  -l, --list    list output devices\n"
            "  -p, --port    UDP port (default = %d)\n"
            "  -I, --id      source ID (default = %d)\n"
            "  -o, --output  output device number\n"
            "  -b, --buffer  buffer size in samples\n"
            "  -L, --latency latency in milliseconds\n",
            DEFAULT_PORT, DEFAULT_ID);
}

// entry point
int main(int argc, const char **argv) {
    // default settings
    SimpleReceiverOptions opt;
    opt.devno = -1; // use default device
    opt.port = DEFAULT_PORT;
    opt.id = DEFAULT_ID;
    opt.buffersize = DEFAULT_BUFFER_SIZE;
    opt.latency = DEFAULT_LATENCY;
    // optional source to invite
    opt.source_host = 0;
    opt.source_port = 0;
    opt.source_id = DEFAULT_ID;

    // parse command line arguments
    argc--; argv++;
    while (argc && argv[0][0] == '-') {
        if (match_option(argv[0], "-h", "--help")) {
            print_usage();
            return EXIT_SUCCESS;
        } else if (match_option(argv[0], "-l", "--list")) {
            Pa_Initialize();
            print_output_devices();
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
        } else if (match_option(argv[0], "-o", "--output")) {
            if (argc < 2 || !parse_int(argv[1], &opt.devno)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-b", "--buffer")) {
            if (argc < 2 || !parse_int(argv[1], &opt.buffersize)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-L", "--latency")) {
            if (argc < 2 || !parse_int(argv[1], &opt.latency)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else {
            fprintf(stdout, "unknown option '%s'\n", argv[0]);
            goto fail;
        }
    }

    // optional positional arguments [HOST PORT] [ID]
    if (!get_endpoint_argument(argv, argc, &opt.source_host,
                               &opt.source_port, &opt.source_id)) {
        goto fail;
    }

    setup();
    // initialize portaudio library
    Pa_Initialize();
    // initialize AOO library
    aoo_initialize(0);

    SimpleReceiver x;
    int ret = SimpleReceiver_init(&x, &opt);
    if (ret == 0) {
        // start
        SimpleReceiver_start(&x);
        // wait for the user to press Ctrl+C
        wait_for_quit();
        // stop
        SimpleReceiver_stop(&x);
    }
    SimpleReceiver_free(&x);

    // deinitialize AOO library
    aoo_terminate();
    // deinitialize portaudio library
    Pa_Terminate();

    return ret;

fail:
    print_usage();
    return EXIT_FAILURE;
}
