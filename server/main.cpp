#include "aoo/aoo.h"
#include "aoo/aoo_server.hpp"

#include "common/udp_server.hpp"
#include "common/tcp_server.hpp"
#include "common/sync.hpp"

#include <stdlib.h>
#include <string.h>
#include <iostream>

#ifdef _WIN32
# include <windows.h>
#else
# include <signal.h>
# include <stdio.h>
#endif

#ifndef AOO_DEFAULT_SERVER_PORT
# define AOO_DEFAULT_SERVER_PORT 7078
#endif

AooLogLevel g_loglevel = kAooLogLevelWarning;

void log_function(AooLogLevel level, const AooChar *msg) {
    if (level <= g_loglevel) {
        switch (level) {
        case kAooLogLevelDebug:
            std::cout << "[debug] ";
            break;
        case kAooLogLevelVerbose:
            std::cout << "[verbose] ";
            break;
        case kAooLogLevelWarning:
            std::cout << "[warning] ";
            break;
        case kAooLogLevelError:
            std::cout << "[error] ";
            break;
        default:
            break;
        }
        std::cout << msg << std::endl;
    }
}

AooServer::Ptr g_aoo_server;

aoo::sync::semaphore g_semaphore;

void stop_server() {
    g_semaphore.post();
}

aoo::udp_server g_udp_server;

void handle_udp_receive(int e, const aoo::ip_address& addr,
                        const AooByte *data, AooSize size) {
    if (e == 0) {
        g_aoo_server->handleUdpMessage(data, size, addr.address(), addr.length(),
            [](void *, const AooByte *data, AooInt32 size,
                    const void *address, AooAddrSize addrlen, AooFlag) {
                aoo::ip_address addr((const struct sockaddr *)address, addrlen);
                return g_udp_server.send(addr, data, size);
            }, nullptr);
    } else {
        if (g_loglevel >= kAooLogLevelError)
            std::cout << "UDP server: recv() failed: " << aoo::socket_strerror(e) << std::endl;
        stop_server();
    }
}

aoo::tcp_server g_tcp_server;

AooId handle_tcp_accept(int e, const aoo::ip_address& addr, int sockfd) {
    if (e == 0) {
        // add new client
        AooId id;
        g_aoo_server->addClient([](void *, AooId client, const AooByte *data, AooSize size) {
            return g_tcp_server.send(client, data, size);
        }, nullptr, sockfd, &id);
        if (g_loglevel >= kAooLogLevelVerbose) {
            std::cout << "Add new client " << id << std::endl;
        }
        return id;
    } else {
        // error
        if (g_loglevel >= kAooLogLevelError)
            std::cout << "TCP server: accept() failed: " << aoo::socket_strerror(e) << std::endl;
        // TODO handle error?
    #if 0
        stop_server();
    #endif
        return kAooIdInvalid;
    }
}

void handle_tcp_receive(AooId client, int e, const AooByte *data, AooSize size) {
    if (e == 0 && size > 0) {
        // handle client message
        if (auto err = g_aoo_server->handleClientMessage(client, data, size); err != kAooOk) {
            // remove misbehaving client
            g_aoo_server->removeClient(client);
            g_tcp_server.close(client);
            if (g_loglevel >= kAooLogLevelWarning)
                std::cout << "Close client " << client << " after error: " << aoo_strerror(err) << std::endl;
        }
    } else {
        // close client
        if (e != 0) {
            if (g_loglevel >= kAooLogLevelWarning)
                std::cout << "Close client after error: " << aoo::socket_strerror(e) << std::endl;
        } else {
            if (g_loglevel >= kAooLogLevelVerbose)
                std::cout << "Client " << client << " has disconnected" << std::endl;
        }
        g_aoo_server->removeClient(client);
    }
}

#ifdef _WIN32
BOOL WINAPI console_handler(DWORD signal) {
    switch (signal) {
    case CTRL_C_EVENT:
        stop_server();
        return TRUE;
    case CTRL_CLOSE_EVENT:
        return TRUE;
    // Pass other signals to the next handler.
    default:
        return FALSE;
    }
}
#else
bool set_signal_handler(int sig, sig_t handler) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(sig, &sa, nullptr) == 0) {
        return true;
    } else {
        perror("sigaction");
        return false;
    }
}

bool set_signal_handlers() {
    // NB: stop_server() is async-signal-safe!
    auto handler = [](int) { stop_server(); };
    return set_signal_handler(SIGINT, handler)
           && set_signal_handler(SIGTERM, handler);
}
#endif

void print_usage() {
    std::cout
        << "Usage: aooserver [OPTIONS]... [PORTNUMBER]\n"
        << "Run AOO server instance, listening on the port specified "
        << "by PORTNUMBER (default = " << AOO_DEFAULT_SERVER_PORT << ")\n"
        << "Options:\n"
        << "  -h, --help             display help and exit\n"
        << "  -v, --version          print version and exit\n"
        << "  -r, --relay            enable server relay\n"
        << "  -l, --log-level=LEVEL  set log level\n"
        << std::endl;
}

bool match_option(const char *str, const char *short_option, const char *long_option) {
    return (short_option && !strcmp(str, short_option))
           || (long_option && !strcmp(str, long_option));
}

int main(int argc, const char **argv) {
    // set control handler
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
        std::cout << "Could not set console handler" << std::endl;
        return EXIT_FAILURE;
    }
#else
    if (!set_signal_handlers()) {
        return EXIT_FAILURE;
    }
#endif

    // parse command line options
    bool relay = false;
    argc--; argv++;
    try {
        while ((argc > 0) && (**argv == '-')) {
            if (match_option(*argv, "-r", "--relay")) {
                relay = true;
            } else if (match_option(*argv, "-l", "--log-level")) {
                if (argc > 1) {
                    g_loglevel = std::stoi(argv[1]);
                    argc--; argv++;
                } else {
                    print_usage();
                    return EXIT_FAILURE;
                }
            } else if (match_option(*argv, "-h", "--help")) {
                print_usage();
                return EXIT_SUCCESS;
            } else if (match_option(*argv, "-v", "--version")) {
                std::cout << "aooserver " << aoo_getVersionString() << std::endl;
                return EXIT_SUCCESS;
            } else {
                std::cout << "Unknown command line option '" << *argv << "'" << std::endl;
                print_usage();
                return EXIT_FAILURE;
            }
            argc--; argv++;
        }
    } catch (const std::exception& e) {
        std::cout << "Bad argument for option '" << *argv << "': "
                  << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // get port number
    int port = AOO_DEFAULT_SERVER_PORT;
    if (argc > 0) {
        try {
            port = std::stoi(*argv);
            if (port <= 0 || port > 65535) {
                std::cout << "Port number " << port << " out of range" << std::endl;
                return EXIT_FAILURE;
            }
        } catch (const std::exception& e) {
            std::cout << "Bad port number argument '" << *argv << "'" << std::endl;
            return EXIT_FAILURE;
        }
    }

    AooSettings settings;
    AooSettings_init(&settings);
    settings.logFunc = log_function;
    if (auto err = aoo_initialize(&settings); err != kAooOk) {
        std::cout << "Could not initialize AOO library: "
                  << aoo_strerror(err) << std::endl;
        return EXIT_FAILURE;
    }

    AooError err;
    g_aoo_server = AooServer::create(&err);
    if (!g_aoo_server) {
        std::cout << "Could not create AooServer: "
                  << aoo_strerror(err) << std::endl;
        return EXIT_FAILURE;
    }

    // setup UDP server
    // TODO: increase socket receive buffer for relay? Use threaded receive?
    try {
        g_udp_server.start(port, handle_udp_receive);
    } catch (const std::exception& e) {
        std::cout << "Could not start UDP server: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // setup TCP server
    try {
        g_tcp_server.start(port, handle_tcp_accept, handle_tcp_receive);
    } catch (const std::exception& e) {
        std::cout << "Could not start TCP server: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    // setup AooServer
    auto flags = aoo::socket_family(g_udp_server.socket()) == aoo::ip_address::IPv6 ?
                     kAooSocketDualStack : kAooSocketIPv4;

    if (auto err = g_aoo_server->setup(port, flags); err != kAooOk) {
        std::cout << "Could not setup AooServer: " << aoo_strerror(err) << std::endl;
        return EXIT_FAILURE;
    }

    g_aoo_server->setServerRelay(relay);

    // finally start network threads
    auto udp_thread = std::thread([]() {
        g_udp_server.run();
    });
    auto tcp_thread = std::thread([]() {
        g_tcp_server.run();
    });

    // keep running until interrupted
    g_semaphore.wait();
    std::cout << "Program stopped by the user" << std::endl;

    // stop UDP and TCP server and exit
    g_udp_server.stop();
    udp_thread.join();

    g_tcp_server.stop();
    tcp_thread.join();

    aoo_terminate();

    return EXIT_SUCCESS;
}
