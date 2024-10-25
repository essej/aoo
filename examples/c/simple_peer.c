/** \file simple_peer.c
    \ingroup examples_c
    \brief A simple AOO peer
    \author Christof Ressi <info@christofressi.com>

This is a full working example of a simple AOO peer
written in C.

The `simple_peer` program connects to an AOO server and
then tries to join a group. The group name and user name
are provided as arguments (GROUP_NAME and USER_NAME).

\note Make sure that every `simple_peer` instance uses
a different user name when joining the same group!

By default, the program tries to connect to "localhost
7078", i.e. an AOO server running on the same machine.
If you want to actually run this test example, you need
to build the `aooserver` program (`AOO_BUILD_SERVER=ON`)
and then run it on the command line. Alternatively,
you can provide a different hostname, e.g. a public
AOO server, with the optional `SERVER_HOSTNAME` argument.

Once we have successfully joined the group, we wait for
other peers to join. Every new peer will be added to the
AooSource object and removed again when it leaves the group.
Conversely, the AooSink receives the streams of all the
other peers. The end result is similar to an online jamming
app where everyone sends its own input to all participants
and in turn receives all their inputs.

The sound source can either be a sine wave oscillator
with random frequency (`--mode sine`) or the audio input
(`--mode input`).

You can also set the streaming format with the `--codec`
and `--block` options.

Run `simple_peer -h` to see all available options.

*/

#include "aoo.h"
#include "aoo_source.h"
#include "aoo_sink.h"
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
#define DEFAULT_PORT 0 /* let the OS pick a port */

#define DEFAULT_CODEC kAooCodecPcm
#define DEFAULT_BLOCK_SIZE 256
#define DEFAULT_BUFFER_SIZE 1024

#define DEFAULT_MODE MODE_SINE
#define SINE_GAIN 0.25

#define DEFAULT_SERVER_HOST "localhost"
#define DEFAULT_SERVER_PORT 7078

enum {
    MODE_INPUT,
    MODE_SINE
};

typedef struct SimplePeerOptions {
    int indevno;
    int outdevno;
    int port;
    int mode;
    int buffersize;
    const char *codec;
    int blocksize;
    const char *group;
    const char *user;
    const char *server_host;
    int server_port;
} SimplePeerOptions;

typedef struct SimplePeer {
    PaStream *stream;
    AooSource *source;
    AooSink *sink;
    AooClient *client;
    volatile bool connected;
    volatile AooId group_id;

    const char *group;
    const char *user;
    const char *server_host;
    int server_port;

    double sr; // sample rate
    int channels; // number of channels
    int mode; // audio input or sine wave

    double freq; // sine osc frequency
    double last_phase;
    AooSample **process_buffer;

#ifdef _MSC_VER
    HANDLE send_thread;
    HANDLE receive_thread;
    HANDLE client_thread;
#else
    pthread_t send_thread;
    pthread_t receive_thread;
    pthread_t client_thread;
#endif
    bool threads_running;
} SimplePeer;

// audio callback passed to Pa_OpenStream()
PaError callback(const void *input, void *output, unsigned long frameCount,
                 const PaStreamCallbackTimeInfo* timeInfo,
                 PaStreamCallbackFlags statusFlags, void *userData)
{
    SimplePeer *x = (SimplePeer*)userData;

    // sample the current NTP time
    AooNtpTime t = aoo_getCurrentNtpTime();

    // send to peers
    if (x->mode == MODE_INPUT) {
        // forward audio input
        AooSource_process(x->source, (float **)input, frameCount, t);
    } else if (x->mode == MODE_SINE) {
        // make sine wave
        double advance = x->freq / (double)x->sr;
        double phase = x->last_phase;
        for (int i = 0; i < frameCount; i++) {
            double f = sin(phase * 2.0 * M_PI) * SINE_GAIN;
            phase += advance;
            // copy to all channels
            for (int j = 0; j < x->channels; j++) {
                x->process_buffer[j][i] = f;
            }
        }
        x->last_phase = fmod(phase, 1.0);

        AooSource_process(x->source, x->process_buffer, frameCount, t);
    }

    // receive from peers
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
    SimplePeer *x = (SimplePeer *)lpParam;
    AooClient_send(x->client, kAooInfinite);
    return 0;
}

DWORD WINAPI do_receive(LPVOID lpParam) {
    SimplePeer *x = (SimplePeer *)lpParam;
    AooClient_receive(x->client, kAooInfinite);
    return 0;
}

DWORD WINAPI do_run(LPVOID lpParam) {
    SimplePeer *x = (SimplePeer *)lpParam;
    AooClient_run(x->client, kAooInfinite);
    return 0;
}

#else

void *do_send(void *y) {
    SimplePeer *x = (SimplePeer *)y;
    AooClient_send(x->client, kAooInfinite);
    return 0;
}

void *do_receive(void *y) {
    SimplePeer *x = (SimplePeer *)y;
    AooClient_receive(x->client, kAooInfinite);
    return 0;
}

void *do_run(void *y) {
    SimplePeer *x = (SimplePeer *)y;
    AooClient_run(x->client, kAooInfinite);
    return 0;
}

#endif

// example event handler
void AOO_CALL handle_event(void *user, const AooEvent *event,
                           AooThreadLevel level)
{
    SimplePeer *x = (SimplePeer *)user;

    AooChar ipstring[64];
    AooSize ipsize = 64;
    AooUInt16 port;
    AooId id;

    switch (event->type) {
    case kAooEventPeerJoin:
    {
        const AooEventPeerJoin *e = &event->peerJoin;
        // add peer to source
        AooEndpoint ep = { e->address.data, e->address.size, DEFAULT_ID };
        AooSource_addSink(x->source, &ep, kAooTrue);

        fprintf(stdout, "peer %s|%s joined\n", e->groupName, e->userName);

        break;
    }
    case kAooEventPeerLeave:
    {
        const AooEventPeerLeave *e = &event->peerLeave;
        // remove peer from source
        AooEndpoint ep = { e->address.data, e->address.size, DEFAULT_ID };
        AooSource_removeSink(x->source, &ep);

        fprintf(stdout, "peer %s|%s left\n", e->groupName, e->userName);

        break;
    }
    case kAooEventDisconnect:
    {
        fprintf(stdout, "forcefully disconnected from server: %s\n",
                event->disconnect.errorMessage);

        x->connected = false;

        break;
    }
    default:
        break;
    }
}

int SimplePeer_init(SimplePeer *x, const SimplePeerOptions *opt) {
    x->stream = 0;
    x->source = 0;
    x->sink = 0;
    x->client = 0;
    x->connected = false;
    x->group_id = kAooIdInvalid;
    x->mode = opt->mode;

    x->group = opt->group;
    x->user = opt->user;
    x->server_host = opt->server_host;
    x->server_port = opt->server_port;

    x->freq = 0;
    x->last_phase = 0;
    x->process_buffer = 0;
    x->send_thread = x->receive_thread = x->client_thread = 0;
    x->threads_running = false;

    // get audio device
    audio_device_info indev, outdev;
    get_audio_input_device(opt->indevno, &indev);
    get_audio_output_device(opt->outdevno, &outdev);

    x->channels = outdev.channels;
    x->sr = outdev.sample_rate;
    int buffersize = opt->buffersize;

    if (x->mode == MODE_SINE) {
        // make random sine wave frequency between 220 and 880 Hz
        x->freq = 220.0 * pow(2.0, randf() * 2.0);
        fprintf(stdout, "sine tone frequency: %g Hz\n", x->freq);
        // create buffers
        x->process_buffer = (AooSample**)malloc(x->channels * sizeof(AooSample *));
        for (int i = 0; i < x->channels; i++) {
            x->process_buffer[i] = (AooSample*)malloc(buffersize * sizeof(AooSample));
        }
    }

    // create AOO source
    x->source = AooSource_new(DEFAULT_ID);

    // create AOO sink
    x->sink = AooSink_new(DEFAULT_ID);

    // create and setup AOO client
    x->client = AooClient_new();
    AooClient_setEventHandler(x->client, handle_event, x, kAooEventModeCallback);

    AooClientSettings settings = AOO_CLIENT_SETTINGS_INIT();
    settings.portNumber = opt->port;
    AooError err = AooClient_setup(x->client, &settings);
    if (err == kAooOk) {
        fprintf(stdout, "sending and receiving on port %d...\n",
                settings.portNumber); /* actual port number! */
    } else {
        fprintf(stdout, "network setup failed: %s\n",
                aoo_strerror(err));
        return 1;
    }

    // add source to client
    AooClient_addSource(x->client, x->source);

    // add sink to client
    AooClient_addSink(x->client, x->sink);

    // portaudio input stream parameters
    PaStreamParameters inparams;
    inparams.device = indev.index;
    inparams.channelCount = indev.channels;
    inparams.sampleFormat = paFloat32 | paNonInterleaved;
    inparams.suggestedLatency = 0;
    inparams.hostApiSpecificStreamInfo = 0;

    PaStreamParameters outparams;
    outparams.device = outdev.index;
    outparams.channelCount = outdev.channels;
    outparams.sampleFormat = paFloat32 | paNonInterleaved;
    outparams.suggestedLatency = 0;
    outparams.hostApiSpecificStreamInfo = 0;

    fprintf(stdout, "open stream...\n"
            "input: %s with %d channels,\n"
            "output: %s with %d channels,\n"
            "sample rate: %d Hz, buffer size: %d samples\n",
            indev.display_name, indev.channels,
            outdev.display_name, outdev.channels,
            (int)x->sr, buffersize);

    // try to open portaudio stream
    PaError err2 = Pa_OpenStream(&x->stream, &inparams, &outparams, x->sr,
                                 buffersize, 0, callback, x);
    if (err2 != paNoError) {
        fprintf(stdout, "could not open stream: %s\n",
                Pa_GetErrorText(err2));
        return 1;
    }

    // setup the AOO source and sink with the same settings as the portaudio stream.
    AooSource_setup(x->source, x->channels, x->sr, buffersize, 0);
    AooSink_setup(x->sink, x->channels, x->sr, buffersize, 0);

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
    x->client_thread = CreateThread(0, 0, do_run, x, 0, 0);
#else
    pthread_create(&x->send_thread, 0, do_send, x);
    pthread_create(&x->receive_thread, 0, do_receive, x);
    pthread_create(&x->client_thread, 0, do_run, x);
#endif
    x->threads_running = true;

    return 0;
}

void AOO_CALL handle_join_group(void *user, const AooRequest *request,
                             AooError result, const AooResponse *response)
{
    SimplePeer *x = (SimplePeer*)user;
    if (result == kAooOk) {
        fprintf(stdout, "joined group '%s' as user '%s'\n",
                x->group, x->user);
        // Save group ID, see SimplePeer_stop()
        x->group_id = response->groupJoin.groupId;
        // Now wait for peers to join, see handle_event()
    } else {
        fprintf(stdout, "could not join group '%s' as user '%s': %s\n",
                x->group, x->user, aoo_strerror(result));
    }
}

void AOO_CALL handle_connect(void *user, const AooRequest *request,
                             AooError result, const AooResponse *response)
{
    SimplePeer *x = (SimplePeer*)user;
    if (result == kAooOk) {
        fprintf(stdout, "connected to %s on port %d\n",
                x->server_host, x->server_port);
        x->connected = true;
        // Now join group.
        AooClientJoinGroup args = AOO_CLIENT_JOIN_GROUP_INIT();
        args.groupName = x->group;
        args.userName = x->user;
        AooClient_joinGroup(x->client, &args, handle_join_group, x);
    } else {
        fprintf(stdout, "could not connect to %s on port %d: %s\n",
                x->server_host, x->server_port, aoo_strerror(result));
    }
}

void SimplePeer_start(SimplePeer *x) {
    // start the portaudio stream
    assert(x->stream);
    Pa_StartStream(x->stream);
    // also start the AOO stream
    assert(x->source);
    AooSource_startStream(x->source, 0, 0);
    // connect to server and join group
    AooClientConnect args = AOO_CLIENT_CONNECT_INIT();
    args.hostName = x->server_host;
    args.port = x->server_port;
    AooClient_connect(x->client, &args, handle_connect, x);
}

void AOO_CALL handle_leave_group(void *user, const AooRequest *request,
                                 AooError result, const AooResponse *response)
{
    SimplePeer *x = (SimplePeer*)user;
    if (result == kAooOk) {
        fprintf(stdout, "left group '%s'\n", x->group);
        // invalidate group ID, see SimplePeer_stop()
        x->group_id = kAooIdInvalid;
    } else {
        fprintf(stdout, "could not leave group '%s': %s\n",
                x->group, aoo_strerror(result));
    }
}

void AOO_CALL handle_disconnect(void *user, const AooRequest *request,
                                AooError result, const AooResponse *response)
{
    SimplePeer *x = (SimplePeer*)user;
    if (result == kAooOk) {
        fprintf(stdout, "disconnected from server\n");
    } else {
        fprintf(stdout, "could not disconnect from server: %s\n",
                aoo_strerror(result));
    }
    x->connected = false;
}

void SimplePeer_stop(SimplePeer *x) {
    if (x->connected) {
        // First leave group, if joined as a member. This is not strictly
        // necessary, but we do it for demonstration purposes.
        if (x->group_id != kAooIdInvalid) {
            AooClient_leaveGroup(x->client, x->group_id, handle_leave_group, x);
            // Wait until we left the group.
            // NOTE: handle_leave_group() will invalidate the group ID.
            while (x->group_id != kAooIdInvalid) {
                Pa_Sleep(SLEEP_GRAIN);
            }
        }

        // Then disconnect from the server.
        AooClient_disconnect(x->client, handle_disconnect, x);
        // Let's wait until we have disconnected.
        while (x->connected) {
            Pa_Sleep(SLEEP_GRAIN);
        }
    }

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

void SimplePeer_free(SimplePeer *x) {
    if (x->threads_running) {
        // stop the client and join network threads.
        assert(x->client);
        AooClient_stop(x->client);
#ifdef _MSC_VER
        WaitForSingleObject(x->send_thread, INFINITE);
        WaitForSingleObject(x->receive_thread, INFINITE);
        WaitForSingleObject(x->client_thread, INFINITE);
        CloseHandle(x->send_thread);
        CloseHandle(x->receive_thread);
        CloseHandle(x->client_thread);
#else
        pthread_join(x->send_thread, 0);
        pthread_join(x->receive_thread, 0);
        pthread_join(x->client_thread, 0);
#endif
    }

    // free buffers for sine wave
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

    // free source, sink and client objects
    if (x->source) AooSource_free(x->source);
    if (x->sink) AooSink_free(x->sink);
    if (x->client) AooClient_free(x->client);
}

void print_usage(void) {
    fprintf(stdout,
            "usage: simple_peer [OPTIONS]... GROUP_NAME USER_NAME\n"
            "       [SERVER_HOSTNAME] [SERVER_PORT]\n"
            "  -h, --help    show help\n"
            "  -l, --list    list input and output devices\n"
            "  -p, --port    UDP port\n"
            "  -i, --input   input device number\n"
            "  -o, --output  output device number\n"
            "  -b, --buffer  buffer size in samples\n"
#if AOO_USE_OPUS
            "  -c, --codec   codec name ('pcm' or 'opus')\n"
#endif
            "  -B, --block   stream block size in samples\n"
            "  -m, --mode    'input' or 'sine'\n");
}

// entry point
int main(int argc, const char **argv) {
    // default settings
    SimplePeerOptions opt;
    opt.indevno = opt.outdevno = -1; // use default device
    opt.port = DEFAULT_PORT;
    opt.mode = DEFAULT_MODE;
    opt.buffersize = DEFAULT_BUFFER_SIZE;
    opt.codec = DEFAULT_CODEC;
    opt.blocksize = DEFAULT_BLOCK_SIZE;
    opt.group = 0;
    opt.user = 0;
    opt.server_host = DEFAULT_SERVER_HOST;
    opt.server_port = DEFAULT_SERVER_PORT;

    // parse command line arguments
    argc--; argv++;
    while (argc && argv[0][0] == '-') {
        if (match_option(argv[0], "-h", "--help")) {
            print_usage();
            return EXIT_SUCCESS;
        } else if (match_option(argv[0], "-l", "--list")) {
            Pa_Initialize();
            fprintf(stdout, "input devices:\n");
            print_input_devices();
            fprintf(stdout, "---\n"
                            "output devices:\n");
            print_output_devices();
            Pa_Terminate();
            return EXIT_SUCCESS;
        } else if (match_option(argv[0], "-p", "--port")) {
            if (argc < 2 || !parse_int(argv[1], &opt.port)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-i", "--input")) {
            if (argc < 2 || !parse_int(argv[1], &opt.indevno)) {
                goto fail;
            }
            argc -= 2; argv += 2;
        } else if (match_option(argv[0], "-o", "--output")) {
            if (argc < 2 || !parse_int(argv[1], &opt.outdevno)) {
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

    // mandatory positional arguments
    if (argc > 1) {
        opt.group = argv[0];
        opt.user = argv[1];
    } else {
        fprintf(stdout, "missing GROUP_NAME and USER_NAME arguments\n");
        goto fail;
    }
    // optional server hostname and port
    if (argc > 2) {
        opt.server_host = argv[2];
    }
    if (argc > 3) {
        if (sscanf(argv[3], "%d", &opt.server_port) != 1) {
            fprintf(stdout, "bad SERVER_PORT argument (%s)\n", argv[3]);
            goto fail;
        }
    }

    setup();
    // initialize portaudio library
    Pa_Initialize();
    // initialize AOO library
    aoo_initialize(0);

    SimplePeer x;
    int ret = SimplePeer_init(&x, &opt);
    if (ret == 0) {
        // start
        SimplePeer_start(&x);
        // wait for the user to press Ctrl+C
        wait_for_quit();
        // stop
        SimplePeer_stop(&x);
    }
    SimplePeer_free(&x);

    // deinitialize AOO library
    aoo_terminate();
    // deinitialize portaudio library
    Pa_Terminate();

    return ret;

fail:
    print_usage();
    return EXIT_FAILURE;
}
