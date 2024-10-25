/** \file simple_sender.cpp
    \ingroup examples_cpp
    \brief A simple AOO sender
    \author Christof Ressi <info@christofressi.com>

This is a full working example of a simple AOO sender
written in C++.

The `simple_sender` program sends a stream to an AOO
sink specified with the [HOSTNAME PORT] [ID]` arguments.
If the arguments are omitted, the program waits passively
for an invitation.

You can try this example together with the `simple_receiver`
example (simple_receiver.cpp).

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
#include "aoo_source.hpp"
#include "aoo_client.hpp"
#include "codec/aoo_pcm.h"
#if AOO_USE_OPUS
#include "codec/aoo_opus.h"
#endif

// common utilities
#include "utils.hpp"

#include "portaudio.h"

#include <array>
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
constexpr int DEFAULT_PORT = 9998;

constexpr double SINE_GAIN = 0.25;

enum class process_mode {
    input,
    sine
};

struct SimpleSenderOptions {
    int devno = -1; // default device
    int port = DEFAULT_PORT;
    AooId id = DEFAULT_ID;
    process_mode mode = process_mode::sine;
    int buffersize = 1024;
    std::string codec = kAooCodecPcm;
    int blocksize = 256;
    std::string sink_host;
    int sink_port = 0;
    int sink_id = DEFAULT_ID;
};

class SimpleSender {
public:
    SimpleSender(const SimpleSenderOptions& opt);

    ~SimpleSender();

    void start();

    void stop();
private:
    void process(AooSample **input, int numFrames);

    void handle_event(const AooEvent& event);

    // data
    PaStream *stream_ = nullptr;
    AooSource::Ptr source_;
    AooClient::Ptr client_;

    double sr_; // sample rate
    int channels_; // number of channels
    process_mode mode_; // audio input or sine wave

    std::vector<double> freq_;
    std::vector<double> last_phase_;
    std::vector<AooSample*> process_buffer_;
    std::vector<AooSample> buffer_data_;

    std::thread send_thread_;
    std::thread receive_thread_;
};

SimpleSender::SimpleSender(const SimpleSenderOptions& opt) {
    // create AOO source
    source_ = AooSource::create(opt.id);
    // set event handler
    source_->setEventHandler(
        [](void *user, const AooEvent *event, AooThreadLevel) {
            static_cast<SimpleSender*>(user)->handle_event(*event);
    }, this, kAooEventModeCallback);

    // create and setup AOO client
    client_ = AooClient::create();

    AooClientSettings settings;
    settings.portNumber = opt.port;
    if (auto err = client_->setup(settings); err == kAooOk) {
        std::cout << "sending from port " << opt.port << " with ID "
                  << opt.id << "...\n";
    } else {
        std::stringstream ss;
        ss << "network setup failed: " << aoo_strerror(err);
        throw std::runtime_error(ss.str());
    }

    // add source to client
    client_->addSource(source_.get());

    // add sink to source, if provided by the user
    if (!opt.sink_host.empty()) {
        // first convert IP address string (e.g. "127.0.0.1") resp. hostname
        // (e.g. "localhost") to socket address.
        // NB: if we only allowed IP address strings, we could use the (faster)
        // aoo_ipEndpointToSockAddr() function instead.
        AooSockAddrStorage addr;
        AooAddrSize addrlen = sizeof(addr);
        if (aoo_resolveIpEndpoint(opt.sink_host.c_str(), opt.sink_port,
                                  settings.socketType,
                                  &addr, &addrlen) != kAooOk) {
            // could not resolve host name resp. got invalid IP string
            AooInt32 code = 0;
            std::array<AooChar, 256> msg;
            AooSize size = msg.size();
            aoo_getLastSocketError(&code, msg.data(), &size);
            std::stringstream ss;
            ss << "could not resolve host name '" << opt.sink_host
               << "': " << msg.data();
            throw std::runtime_error(ss.str());
        }

        // now make sink endpoint and add it to the source
        AooEndpoint ep = { &addr, addrlen, opt.sink_id };

        source_->addSink(ep, kAooTrue);

        std::cout << "sending to " << ep << "...\n";
    }

    // get audio device
    auto dev = get_audio_input_device(opt.devno);
    channels_ = dev.channels;
    sr_ = dev.sample_rate;
    mode_ = opt.mode;

    if (mode_ == process_mode::sine) {
        // make random frequencies between 220 and 880 Hz
        freq_.resize(channels_);
        last_phase_.resize(channels_);
        for (auto& f : freq_) {
            f = 220.0 * std::pow(2.0, randf() * 2.0);
        }
        // create buffers
        // NB: process_buffer_ points into buffer_data_
        buffer_data_.resize(channels_ * opt.buffersize);
        process_buffer_.resize(channels_);
        for (int i = 0; i < channels_; ++i) {
            process_buffer_[i] = &buffer_data_[i * opt.buffersize];
        }
    }

    // portaudio input stream parameters
    PaStreamParameters params;
    params.device = dev.index;
    params.channelCount = channels_;
    params.sampleFormat = paFloat32 | paNonInterleaved;
    params.suggestedLatency = 0;
    params.hostApiSpecificStreamInfo = nullptr;

    std::cout << "open " << dev.display_name << " (" << channels_
              << " channels, " << sr_ << " Hz, " << opt.buffersize
              << " samples)\n";

    // audio callback passed to Pa_OpenStream()
    auto callback = [](const void *input, void *output, unsigned long frameCount,
                     const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags, void *userData) -> PaError
    {
        static_cast<SimpleSender *>(userData)->process((float **)input, frameCount);
        return paContinue;
    };

    // try to open portaudio stream
    if (auto err = Pa_OpenStream(&stream_, &params, 0, sr_,
                                 opt.buffersize, 0, callback, this);
        err != paNoError) {
        std::stringstream ss;
        ss << "could not open stream: " << Pa_GetErrorText(err);
        throw std::runtime_error(ss.str());
    }

    // setup the AOO source with the same settings as the portaudio stream.
    source_->setup(channels_, sr_, opt.buffersize, 0);

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
}

SimpleSender::~SimpleSender() {
    // stop the client and join network threads.
    assert(client_);
    client_->stop();

    if (send_thread_.joinable()) {
        send_thread_.join();
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }

    // close portaudio stream
    if (stream_) {
        std::cout << "close audio\n";
        Pa_CloseStream(stream_);
    }
}

void SimpleSender::start() {
    //start the portaudio stream
    assert(stream_);
    Pa_StartStream(stream_);
    // also start the AOO stream
    assert(source_);
    source_->startStream(0, nullptr);
}

void SimpleSender::stop() {
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

void SimpleSender::process(AooSample **input, int numFrames) {
    // sample the current NTP time
    AooNtpTime t = aoo_getCurrentNtpTime();

    if (mode_ == process_mode::input) {
        // forward audio input
        source_->process((AooSample **)input, numFrames, t);
    } else if (mode_ == process_mode::sine) {
        // make sine waves
        for (int i = 0; i < channels_; i++) {
            double advance = freq_[i] / sr_;
            double phase = last_phase_[i];
            for (int j = 0; j < numFrames; j++) {
                process_buffer_[i][j] = std::sin(phase * 2.0 * M_PI) * SINE_GAIN;
                phase += advance;
            }
            last_phase_[i] = std::fmod(phase, 1.0);
        }

        source_->process(process_buffer_.data(), numFrames, t);
    }
    // tell the client that there may be data to send out
    client_->notify();
}

void SimpleSender::handle_event(const AooEvent &event) {
    switch (event.type) {
    case kAooEventSinkPing:
    {
        // just post a message to the console together with the endpoint and the RTT
        std::cout << "got ping from sink "
                  << event.sinkPing.endpoint << std::endl;
        break;
    }
    case kAooEventInvite:
    {
        // post message to console and accept invitation
        std::cout << "accept invitation from sink "
                  << event.invite.endpoint << std::endl;

        source_->handleInvite(event.invite.endpoint,
                              event.invite.token, kAooTrue);

        break;
    }
    case kAooEventUninvite:
    {
        // post message to console and accept uninvitation
        std::cout << "accept uninvitation from sink "
                  << event.uninvite.endpoint << std::endl;

        source_->handleUninvite(event.uninvite.endpoint,
                                event.uninvite.token, kAooTrue);

        break;
    }
    default:
        break;
    }
}

void print_usage(void) {
    std::cout
        << "usage: simple_sender [OPTIONS]... [HOSTNAME PORT] [ID]\n"
        << "  -h, --help    show help\n"
        << "  -l, --list    list output devices\n"
        << "  -p, --port    UDP port (default = " << DEFAULT_PORT << ")\n"
        << "  -I, --id      source ID (default = " << DEFAULT_ID << ")"
        << "  -i, --input   input device number\n"
        << "  -b, --buffer  buffer size in samples\n"
#if AOO_USE_OPUS
        << "  -c, --codec   codec name ('pcm' or 'opus')\n"
#endif
        << "  -B, --block   stream block size in samples\n"
        << "  -m, --mode    'input' or 'sine'\n";
}

int main(int argc, const char **argv) {
    // parse command line arguments
    SimpleSenderOptions opt;
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
            } else if (match_option(argv, argc, "-I", "--id", opt.id)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-i", "--input", opt.devno)) {
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

    // optional positional arguments [HOST PORT] [ID]
    try {
        get_endpoint_argument(argv, argc, opt.sink_host,
                              opt.sink_port, opt.sink_id);
    } catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        print_usage();
        return EXIT_FAILURE;
    }

    setup();
    // initialize AOO library
    aoo_initialize(nullptr);
    // initialize portaudio library
    Pa_Initialize();

    int result = EXIT_SUCCESS;

    try {
        SimpleSender sender(opt);
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
