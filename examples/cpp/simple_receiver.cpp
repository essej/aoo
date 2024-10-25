/** \file simple_receiver.cpp
    \ingroup examples_cpp
    \brief A simple AOO receiver
    \author Christof Ressi <info@christofressi.com>

This is a full working example of a simple AOO receiver
written in C++.

The `simple_receiver` program receives one or more AOO
streams. By default, it waits passively for AOO sources
to send, but you can also *invite* a specific source with
the optional `[HOSTNAME PORT] [ID]` arguments.

You can try this example together with the `simple_sender`
example (simple_sender.cpp).

\note If you want to run multiple instances of `simple_receiver`
you must give them distinct IDs with the `--id` option.

Run `simple_receiver -h` to see all available options.

*/

#include "aoo.h"
#include "aoo_sink.hpp"
#include "aoo_client.hpp"

// common utilities
#include "utils.hpp"

#include "portaudio.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>
#include <string_view>
#include <thread>

constexpr int DEFAULT_ID = 1;
constexpr int DEFAULT_PORT = 9999;

struct SimpleReceiverOptions {
    int devno = -1; // default device
    int port = DEFAULT_PORT;
    AooId id = DEFAULT_ID;
    int buffersize = 1024;
    int latency = 100;
    std::string source_host;
    int source_port = 0;
    int source_id = DEFAULT_ID;
};

class SimpleReceiver {
public:
    SimpleReceiver(const SimpleReceiverOptions& opt);

    ~SimpleReceiver();

    void start();

    void stop();
private:
    void process(AooSample **output, int numFrames);

    void handle_event(const AooEvent& event);

    // data
    PaStream *stream_ = nullptr;
    AooSink::Ptr sink_;
    AooClient::Ptr client_;

    std::string source_host_;
    int source_port_;
    int source_id_;
    AooSockAddrStorage source_addr_;
    AooAddrSize source_size_;

    int latency_;

    std::thread send_thread_;
    std::thread receive_thread_;
};

SimpleReceiver::SimpleReceiver(const SimpleReceiverOptions& opt)
    : source_host_(opt.source_host), source_port_(opt.source_port),
      source_id_(opt.source_id), latency_(opt.latency) {

    // create AOO sink
    sink_ = AooSink::create(opt.id);
    // set event handler
    sink_->setEventHandler(
        [](void *user, const AooEvent *event, AooThreadLevel) {
            static_cast<SimpleReceiver*>(user)->handle_event(*event);
        }, this, kAooEventModeCallback);
    // set jitter buffer latency
    sink_->setLatency(opt.latency * 0.001);

    // create and setup AOO client
    client_ = AooClient::create();

    AooClientSettings settings;
    settings.portNumber = opt.port;
    if (auto err = client_->setup(settings); err == kAooOk) {
        std::cout << "receiving on port " << opt.port << " with ID " << opt.id << "...\n";
    } else {
        std::stringstream ss;
        ss << "network setup failed: " << aoo_strerror(err);
        throw std::runtime_error(ss.str());
    }

    // add sink to client
    client_->addSink(sink_.get());

    // invite source, if provided by the user
    if (!source_host_.empty()) {
        // first convert IP address string (e.g. "127.0.0.1") resp. hostname
        // (e.g. "localhost") to socket address.
        // NB: if we only allowed IP address strings, we could use the (faster)
        // aoo_ipEndpointToSockAddr() instead.
        source_size_ = sizeof(source_addr_);
        if (aoo_resolveIpEndpoint(source_host_.c_str(), source_port_,
                                  settings.socketType,
                                   &source_addr_, &source_size_) != kAooOk) {
            // could not resolve host name resp. got invalid IP string
            AooInt32 code = 0;
            std::array<AooChar, 256> msg;
            AooSize size = msg.size();
            aoo_getLastSocketError(&code, msg.data(), &size);
            std::stringstream ss;
            ss << "could not resolve host name '" << opt.source_host
               << "': " << msg.data();
            throw std::runtime_error(ss.str());
        }
    }

    // get audio device
    auto dev = get_audio_output_device(opt.devno);

    // portaudio output stream parameters
    PaStreamParameters params;
    params.device = dev.index;
    params.channelCount = dev.channels;
    params.sampleFormat = paFloat32 | paNonInterleaved;
    params.suggestedLatency = 0;
    params.hostApiSpecificStreamInfo = nullptr;

    double sr = dev.sample_rate;

    std::cout << "open " << dev.display_name << " (" << dev.channels << " channels, "
              << sr << " Hz, " << opt.buffersize << " samples)\n";

    // audio callback passed to Pa_OpenStream()
    auto callback = [](const void *input, void *output, unsigned long frameCount,
                       const PaStreamCallbackTimeInfo* timeInfo,
                       PaStreamCallbackFlags statusFlags, void *userData) -> PaError
    {
        static_cast<SimpleReceiver *>(userData)->process((float **)output, frameCount);
        return paContinue;
    };

    // try to open portaudio stream
    if (auto err = Pa_OpenStream(&stream_, 0, &params, sr,
                                 opt.buffersize, 0, callback, this);
        err != paNoError) {
        std::stringstream ss;
        ss << "could not open stream: " << Pa_GetErrorText(err);
        throw std::runtime_error(ss.str());
    }

    // setup the AOO sink with the same settings as the portaudio stream.
    sink_->setup(dev.channels, sr, opt.buffersize, 0);

    // start the network threads
    send_thread_ = std::thread([this]() {
        client_->send(kAooInfinite);
    });

    receive_thread_ = std::thread([this]() {
        client_->receive(kAooInfinite);
    });
}

SimpleReceiver::~SimpleReceiver() {
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

void SimpleReceiver::start() {
    // finally we can start the portaudio stream (after AooSink::setup()!)
    assert(stream_);
    Pa_StartStream(stream_);

    if (!source_host_.empty()) {
        // invite AOO source
        AooEndpoint ep = { &source_addr_, source_size_, source_id_ };

        std::cout << "invite source " << ep << "\n";

        assert(sink_);
        sink_->inviteSource(ep, nullptr);
    }
}

void SimpleReceiver::stop() {
    if (!source_host_.empty()) {
        fprintf(stdout, "uninvite sources\n");
        // Uninvite all sources. The source(s) will output a kAooEventUninvite
        // event, see handle_event() in simple_sender.c.
        // NB: If we don't uninvite, the source(s) will just keep sending data!
        sink_->uninviteAll();
        // HACK: sleep a little bit so the client has a chance to actually
        // send the /uninvite message and receive the /stop message.
        // Do this while the portaudio stream is still running!
        Pa_Sleep(SLEEP_GRAIN + latency_);
    }
    // stop the portaudio stream.
    std::cout << "stop audio\n";
    assert(stream_);
    Pa_StopStream(stream_);
}

// audio callback passed to Pa_OpenStream()
void SimpleReceiver::process(AooSample **output, int numFrames) {
    // sample the current NTP time
    AooNtpTime t = aoo_getCurrentNtpTime();
    // write stream directy to audio output
    sink_->process(output, numFrames, t, nullptr, nullptr);
    // tell the client that there may be data to send out
    client_->notify();
}

void SimpleReceiver::handle_event(const AooEvent &event) {
    switch (event.type) {
    case kAooEventStreamStart:
    {
        std::cout << "start stream from source "
                  << event.streamStart.endpoint << std::endl;
        break;
    }
    case kAooEventStreamStop:
    {
        std::cout << "stop stream from source "
                  << event.streamStop.endpoint << std::endl;
        break;
    }
    case kAooEventFormatChange:
    {
        auto& f = *event.formatChange.format;
        std::cout << "format changed: '" << f.codecName << "' codec, "
                  << f.numChannels << " channels, " << f.sampleRate
                  << " Hz, " << f.blockSize << " samples" << std::endl;
        break;
    }
    default:
        break;
    }
}

void print_usage(void) {
    std::cout
        << "usage: simple_receiver [OPTIONS]... [HOSTNAME PORT] [ID]\n"
        << "  -h, --help    show help\n"
        << "  -l, --list    list output devices\n"
        << "  -p, --port    UDP port (default = " << DEFAULT_PORT << ")\n"
        << "  -I, --id      sink ID (default = " << DEFAULT_ID << ")"
        << "  -o, --output  output device number\n"
        << "  -b, --buffer  buffer size in samples\n"
        << "  -L, --latency latency in milliseconds\n";
}

// the main function
int main(int argc, const char **argv) {
    // parse command line arguments
    SimpleReceiverOptions opt;
    // parse command line arguments
    try {
        argc--; argv++;
        while (argc && argv[0][0] == '-') {
            if (match_option(argv, "-h", "--help")) {
                print_usage();
                return EXIT_SUCCESS;
            } else if (match_option(argv, "-l", "--list")) {
                Pa_Initialize();
                print_output_devices();
                Pa_Terminate();
                return EXIT_SUCCESS;
            } else if (match_option(argv, argc, "-p", "--port", opt.port)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-I", "--id", opt.id)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-o", "--output", opt.devno)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-b", "--buffer", opt.buffersize)) {
                argc -= 2; argv += 2;
            } else if (match_option(argv, argc, "-L", "--latency", opt.latency)) {
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
        get_endpoint_argument(argv, argc, opt.source_host,
                              opt.source_port, opt.source_id);
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
        SimpleReceiver receiver(opt);
        // start
        receiver.start();
        // wait for the user to press Ctrl+C
        wait_for_quit();
        // stop
        receiver.stop();
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
