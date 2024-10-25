/** \file simple_peer.cpp
    \ingroup examples_cpp
    \brief A simple AOO peer
    \author Christof Ressi <info@christofressi.com>

This is a full working example of a simple AOO peer
written in C++.

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
#include "aoo_source.hpp"
#include "aoo_sink.hpp"
#include "aoo_client.hpp"
#include "codec/aoo_pcm.h"
#if AOO_USE_OPUS
#include "codec/aoo_opus.h"
#endif

// common utilities
#include "utils.hpp"

#include "portaudio.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

constexpr int DEFAULT_ID = 1;
constexpr int DEFAULT_PORT = 0; /* let the OS pick a port */

constexpr double SINE_GAIN = 0.25;

enum class process_mode {
    input,
    sine
};

constexpr const char *DEFAULT_SERVER_HOST = "localhost";
constexpr int DEFAULT_SERVER_PORT = 7078;

struct SimplePeerOptions {
    int indevno = -1; // default device
    int outdevno = -1; // default device
    int port = DEFAULT_PORT;
    process_mode mode = process_mode::sine;
    int buffersize = 1024;
    std::string codec = kAooCodecPcm;
    int blocksize = 256;
    std::string group;
    std::string user;
    std::string server_host = DEFAULT_SERVER_HOST;
    int server_port = DEFAULT_SERVER_PORT;
};

class SimplePeer {
public:
    SimplePeer(const SimplePeerOptions& opt);

    ~SimplePeer();

    void start();

    void stop();
private:
    void process(AooSample **input, AooSample **output, int numFrames);

    void handle_event(const AooEvent& event);

    void handle_connect(const AooRequest& request, AooError result,
                        const AooResponse& response);

    void handle_disconnect(const AooRequest& request, AooError result,
                           const AooResponse& response);

    void handle_join_group(const AooRequest& request, AooError result,
                           const AooResponse& response);

    void handle_leave_group(const AooRequest& request, AooError result,
                            const AooResponse& response);

    // data
    PaStream *stream_;
    AooSource::Ptr source_;
    AooSink::Ptr sink_;
    AooClient::Ptr client_;
    std::atomic<bool> connected_{false};
    std::atomic<AooId> group_id_{kAooIdInvalid};

    std::string group_;
    std::string user_;
    std::string server_host_;
    int server_port_;

    double sr_ = 0; // sample rate
    int channels_ = 0; // number of channels
    process_mode mode_; // audio input or sine wave

    double freq_ = 0; // sine osc frequency
    double last_phase_ = 0;
    std::vector<AooSample*> process_buffer_;
    std::vector<AooSample> buffer_data_;

    std::thread send_thread_;
    std::thread receive_thread_;
    std::thread client_thread_;
};

SimplePeer::SimplePeer(const SimplePeerOptions& opt) {
    group_ = opt.group;
    user_ = opt.user;
    server_host_ = opt.server_host;
    server_port_ = opt.server_port;

    // create AOO source
    source_ = AooSource::create(DEFAULT_ID);

    // create AOO sink
    sink_ = AooSink::create(DEFAULT_ID);

    // create and setup AOO client
    client_ = AooClient::create();

    client_->setEventHandler(
        [](void *user, const AooEvent *event, AooThreadLevel) {
            static_cast<SimplePeer*>(user)->handle_event(*event);
        }, this, kAooEventModeCallback);

    AooClientSettings settings;
    settings.portNumber = opt.port;
    if (auto err = client_->setup(settings); err == kAooOk) {
        std::cout << "sending and receiving on port "
                  << settings.portNumber << "\n"; // actual port number!
    } else {
        std::stringstream ss;
        ss << "network setup failed: " << aoo_strerror(err);
        throw std::runtime_error(ss.str());
    }

    // add source to client
    client_->addSource(source_.get());
    // add sink to client
    client_->addSink(sink_.get());

    // get audio device
    auto indev = get_audio_input_device(opt.indevno);
    auto outdev = get_audio_output_device(opt.outdevno);

    channels_ = outdev.channels;
    sr_ = outdev.sample_rate;
    auto buffersize = opt.buffersize;
    mode_ = opt.mode;

    if (mode_ == process_mode::sine) {
        // generate sine wave frequency between 220 and 880 Hz
        freq_ = 220.0 * std::pow(2.0, randf() * 2.0);
        std::cout << "sine tone frequency: " << freq_ << " Hz\n";
        // create buffers
        // NB: process_buffer_ points into buffer_data_
        buffer_data_.resize(channels_ * buffersize);
        process_buffer_.resize(channels_);
        for (int i = 0; i < channels_; ++i) {
            process_buffer_[i] = &buffer_data_[i * buffersize];
        }
    }

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

    std::cout << "open stream...\n"
              << "input: " << indev.display_name << " with "
              << indev.channels << " channels,\n"
              << "output: " << outdev.display_name << " with "
              << outdev.channels << " channels,\n"
              << "sample rate: " << (int)sr_ << " Hz, buffer size: "
              << buffersize << " samples\n";

    // audio callback passed to Pa_OpenStream()
    auto callback = [](const void *input, void *output, unsigned long frameCount,
                       const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags, void *userData) -> PaError
    {
        static_cast<SimplePeer *>(userData)->process(
            (float **)input, (float **)output, frameCount);
        return paContinue;
    };

    // try to open portaudio stream
    if (auto err = Pa_OpenStream(&stream_, &inparams, &outparams, sr_,
                                 buffersize, 0, callback, this);
        err != paNoError) {
        std::stringstream ss;
        ss << "could not open stream: " << Pa_GetErrorText(err);
        throw std::runtime_error(ss.str());
    }

    // setup the AOO source and sink with the same settings as the portaudio stream.
    source_->setup(channels_, sr_, buffersize, 0);
    sink_->setup(channels_, sr_, buffersize, 0);

    // set AOO stream format; use the same number of channels and samplerate
    // as the portaudio stream.
    if (opt.codec == kAooCodecPcm) {
        AooFormatPcm fmt;
        AooFormatPcm_init(&fmt, channels_, sr_, opt.blocksize, kAooPcmInt16);
        source_->setFormat(fmt.header);
        std::cout << "PCM stream: " << fmt.header.numChannels << " channels, "
                  << fmt.header.sampleRate << " Hz, " << fmt.header.blockSize << " samples\n";
#if AOO_USE_OPUS
    } else if (opt.codec == kAooCodecOpus) {
        AooFormatOpus fmt;
        AooFormatOpus_init(&fmt, channels_, sr_, opt.blocksize, OPUS_APPLICATION_AUDIO);
        source_->setFormat(fmt.header);
        source_->setFormat(fmt.header);
        std::cout << "Opus stream: " << fmt.header.numChannels << " channels, "
                  << fmt.header.sampleRate << " Hz, " << fmt.header.blockSize << " samples\n";
#endif
    } else {
        std::stringstream ss;
        ss << "unknown/unsupported codec '" << opt.codec;
        throw std::runtime_error(ss.str());
    }

    // start the network threads
    send_thread_ = std::thread([this]() {
        client_->send(kAooInfinite);
    });

    receive_thread_ = std::thread([this]() {
        client_->receive(kAooInfinite);
    });

    client_thread_ = std::thread([this]() {
        client_->run(kAooInfinite);
    });
}

SimplePeer::~SimplePeer() {
    // stop the client and join network threads.
    assert(client_);
    client_->stop();

    if (send_thread_.joinable()) {
        send_thread_.join();
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    if (client_thread_.joinable()) {
        client_thread_.join();
    }

    // close portaudio stream
    if (stream_) {
        std::cout << "close audio\n";
        Pa_CloseStream(stream_);
    }
}

void SimplePeer::start() {
    //start the portaudio stream
    assert(stream_);
    Pa_StartStream(stream_);
    // also start the AOO stream
    assert(source_);
    source_->startStream(0, nullptr);

    // connect to server and join group
    AooClientConnect args;
    args.hostName = server_host_.c_str();
    args.port = server_port_;
    auto cb = [](void *user, const AooRequest *request, AooError result,
                 const AooResponse *response) {
        static_cast<SimplePeer*>(user)->handle_connect(
            *request, result, *response);
    };
    client_->connect(args, cb, this);
}

void SimplePeer::stop() {
    if (connected_.load()) {
        // First leave group, if joined as a member. This is not strictly
        // necessary, but we do it for demonstration purposes.
        if (group_id_ != kAooIdInvalid) {
            client_->leaveGroup(group_id_,
                [](void *user, const AooRequest *request, AooError result,
                   const AooResponse *response) {
                    static_cast<SimplePeer*>(user)->handle_leave_group(
                        *request, result, *response);
                }, this);

            // Wait until we left the group.
            // NOTE: handle_leave_group() will invalidate the group ID.
            while (group_id_.load() != kAooIdInvalid) {
                Pa_Sleep(SLEEP_GRAIN);
            }
        }

        // Then disconnect from the server.
        client_->disconnect([](void *user, const AooRequest *request,
                               AooError result, const AooResponse *response) {
            static_cast<SimplePeer*>(user)->handle_disconnect(
                *request, result, *response);
        }, this);

        // Let's wait until we have disconnected.
        while (connected_.load()) {
            Pa_Sleep(SLEEP_GRAIN);
        }
    }

    // Stop the stream.
    // NB: This is not strictly necessary because the sink would automatically
    // remove the source after a few seconds of inactivity, but in this example
    // we want the sink to output a AooEventStreamStop event, see handle_event()
    // in simple_receiver.c.
    assert(source_);
    source_->stopStream(0);
    // HACK: sleep a little bit so the client has a chance to actually send
    // the /stop message. Do this while the portaudio stream is still running!
    Pa_Sleep(SLEEP_GRAIN);

    // stop the portaudio stream.
    std::cout << "stop audio\n";
    assert(stream_);
    Pa_StopStream(stream_);
}

void SimplePeer::handle_connect(const AooRequest& request,
                                AooError result,
                                const AooResponse& response)
{
    if (result == kAooOk) {
        std::cout << "connected to " << server_host_
                  << " on port " << server_port_ << "\n";
        connected_.store(true);
        // Now join group.
        AooClientJoinGroup args;
        args.groupName = group_.c_str();
        args.userName = user_.c_str();
        auto cb = [](void *user, const AooRequest *request, AooError result,
                     const AooResponse *response) {
            static_cast<SimplePeer*>(user)->handle_join_group(
                *request, result, *response);
        };
        client_->joinGroup(args, cb, this);
    } else {
        std::cout << "could not connect to " << server_host_
                  << " on port " << server_port_ << ": "
                  << aoo_strerror(result) << "\n";
    }
}

void SimplePeer::handle_disconnect(const AooRequest& request,
                                   AooError result,
                                   const AooResponse& response)
{
    if (result == kAooOk) {
        std::cout << "disconnected from server\n";
    } else {
        std::cout << "could not disconnect from server: "
                  << aoo_strerror(result) << "\n";
    }
    connected_.store(false);
}

void SimplePeer::handle_join_group(const AooRequest& request,
                                   AooError result,
                                   const AooResponse& response)
{
    if (result == kAooOk) {
        std::cout << "joined group '" << group_
                  << "' as user '" << user_ << "'\n";
        // Save group ID, see SimplePeer_stop()
        group_id_.store(response.groupJoin.groupId);
        // Now wait for peers to join, see handle_event()
    } else {
        std::cout << "could not join group '" << group_
                  << "' as user '" << user_ << "': "
                  << aoo_strerror(result) << "\n";
    }
}

void SimplePeer::handle_leave_group(const AooRequest& request,
                                    AooError result,
                                    const AooResponse& response)
{
    if (result == kAooOk) {
        std::cout << "left group '" << group_ << "' as user '"
                  << user_ << "'\n";
        // invalidate group ID, see SimplePeer_stop()
        group_id_.store(kAooIdInvalid);
    } else {
        std::cout << "could not leave group '" << group_ << "': "
                  << aoo_strerror(result) << "\n";
    }
}

void SimplePeer::process(AooSample **input, AooSample **output, int numFrames) {
    // sample the current NTP time
    AooNtpTime t = aoo_getCurrentNtpTime();

    // send to peers
    if (mode_ == process_mode::input) {
        // forward audio input
        source_->process(input, numFrames, t);
    } else if (mode_ == process_mode::sine) {
        double advance = freq_ / (double)sr_;
        double phase = last_phase_;
        for (int i = 0; i < numFrames; i++) {
            double f = std::sin(phase * 2.0 * M_PI) * SINE_GAIN;
            phase += advance;
            // copy to all channels
            for (int j = 0; j < channels_; j++) {
                process_buffer_[j][i] = f;
            }
        }
        last_phase_ = std::fmod(phase, 1.0);

        source_->process(process_buffer_.data(), numFrames, t);
    }

    // receive from peers
    sink_->process(output, numFrames, t, nullptr, nullptr);

    // tell the client that there may be data to send out
    client_->notify();
}

// example event handler
void SimplePeer::handle_event(const AooEvent& event) {
    switch (event.type) {
    case kAooEventPeerJoin:
    {
        auto& e = event.peerJoin;
        // add peer to source
        AooEndpoint ep = { e.address.data, e.address.size, DEFAULT_ID };
        source_->addSink(ep, kAooTrue);

        std::cout << "peer " << e.groupName << "|" << e.userName << " joined\n";

        break;
    }
    case kAooEventPeerLeave:
    {
        auto& e = event.peerJoin;
        // remove peer from source
        AooEndpoint ep = { e.address.data, e.address.size, DEFAULT_ID };
        source_->removeSink(ep);

        std::cout << "peer " << e.groupName << "|" << e.userName << " left\n";

        break;
    }
    case kAooEventDisconnect:
    {
        std::cout << "forcefully disconnected from server: "
                  << event.disconnect.errorMessage << "\n";

        connected_.store(false);

        break;
    }
    default:
        break;
    }
}

void print_usage(void) {
    std::cout
        << "usage: simple_peer [OPTIONS]... GROUP_NAME USER_NAME\n"
        << "       [SERVER_HOSTNAME] [SERVER_PORT]\n"
        << "  -h, --help    show help\n"
        << "  -l, --list    list input and output devices\n"
        << "  -p, --port    UDP port\n"
        << "  -i, --input   input device number\n"
        << "  -o, --output  output device number\n"
        << "  -b, --buffer  buffer size in samples\n"
#if AOO_USE_OPUS
        << "  -c, --codec   codec name ('pcm' or 'opus')\n"
#endif
        << "  -B, --block   stream block size in samples\n"
        << "  -m, --mode    'input' or 'sine'\n";
}

// entry point
int main(int argc, const char **argv) {
    // default settings
    SimplePeerOptions opt;
    try {
        argc--; argv++;
        while (argc && argv[0][0] == '-') {
            if (match_option(argv, "-h", "--help")) {
                print_usage();
                return EXIT_SUCCESS;
            } else if (match_option(argv, "-l", "--list")) {
                Pa_Initialize();
                print_input_devices();
                Pa_Terminate();
                return EXIT_SUCCESS;
            } else if (match_option(argv, argc, "-p", "--port", opt.port)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-i", "--input", opt.indevno)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-o", "--output", opt.outdevno)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-b", "--buffer", opt.buffersize)) {
                argc -= 2; argv += 2;
#if AOO_USE_OPUS
            } else if (match_option(argv, "-c", "--codec")) {
                if (argc < 2) {
                    throw std::runtime_error("missing argument for 'codec' option");
                }
                opt.codec = argv[1];
                argc -= 2; argv += 2;
#endif
            } else if (match_option(argv, argc, "-B", "--block", opt.blocksize)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, "-m", "--mode")) {
                if (argc < 2) {
                    throw std::runtime_error("missing argument for 'mode' option");
                }
                std::string mode = argv[1];
                if (mode == "input") {
                    opt.mode = process_mode::input;
                } else if (mode == "sine") {
                    opt.mode = process_mode::sine;
                } else {
                    throw std::runtime_error("bad argument for 'mode' option");
                }
                argc -= 2; argv += 2;
            } else {
                std::stringstream ss;
                ss << "unknown option '" << argv[0] << "'";
                throw std::runtime_error(ss.str());
            }
        }
    } catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
        print_usage();
        return EXIT_FAILURE;
    }

    // mandatory positional arguments
    if (argc > 1) {
        opt.group = argv[0];
        opt.user = argv[1];
    } else {
        std::cout << "missing GROUP_NAME and USER_NAME arguments\n";
        print_usage();
        return EXIT_FAILURE;
    }
    // optional server hostname and port
    if (argc > 2) {
        opt.server_host = argv[2];
    }
    if (argc > 3) {
        try {
            opt.server_port = std::stoi(argv[1]);
        }  catch (const std::exception& e) {
            std::cout << "bad SERVER_PORT argument ("
                      << argv[1] << "): " << e.what() << "\n";
            print_usage();
            return EXIT_FAILURE;
        }
    }

    setup();
    // initialize portaudio library
    Pa_Initialize();
    // initialize AOO library
    aoo_initialize(0);

    int result = EXIT_SUCCESS;

    try {
        SimplePeer sender(opt);
        // start
        sender.start();
        // wait for the user to press Ctrl+C
        wait_for_quit();
        // stop
        sender.stop();
    } catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
        result = EXIT_FAILURE;
    }

    // deinitialize AOO library
    aoo_terminate();
    // deinitialize portaudio library
    Pa_Terminate();

    return result;
}
