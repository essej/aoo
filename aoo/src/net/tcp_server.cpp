#include "tcp_server.hpp"

#include "common/log.hpp"
#include "common/utils.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

#ifndef AOO_DEBUG_TCP_SERVER
# define AOO_DEBUG_TCP_SERVER 0
#endif

namespace aoo {

void tcp_server::start(int port, accept_handler accept, receive_handler receive) {
    start(std::vector<int>{ port }, std::move(accept), std::move(receive));
}

void tcp_server::start(const std::vector<int>& ports, accept_handler accept,
                       receive_handler receive) {
    do_close();

    if (ports.empty()) {
        throw tcp_error(EINVAL);
    }

    try {
        event_socket_ = udp_socket(port_tag{}, 0);
        listen_sockets_.reserve(ports.size());
        for (auto port : ports) {
            tcp_socket socket(port_tag{}, port, true);
            socket.listen();
            listen_sockets_.push_back(std::move(socket));
        }
    } catch (const socket_error& e) {
        event_socket_.close();
        for (auto& socket : listen_sockets_) {
            socket.close();
        }
        listen_sockets_.clear();
        throw tcp_error(e);
    }

    accept_handler_ = std::move(accept);
    receive_handler_ = std::move(receive);

    // prepare poll array for listening and event sockets
    poll_array_.resize(listen_sockets_.size() + 1);
    for (size_t i = 0; i < listen_sockets_.size(); ++i) {
        poll_array_[i].events = POLLIN;
        poll_array_[i].revents = 0;
        poll_array_[i].fd = listen_sockets_[i].native_handle();
    }

    poll_array_[event_index()].events = POLLIN;
    poll_array_[event_index()].revents = 0;
    poll_array_[event_index()].fd = event_socket_.native_handle();

    last_error_ = 0;
    running_.store(true);

    for (auto port : ports) {
        LOG_DEBUG("tcp_server: start listening on port " << port);
    }
}

bool tcp_server::run(double timeout) {
    while (running_.load()) {
        auto ret = do_run(timeout);
        if (timeout >= 0) {
#if 1
            if (ret) {
                // drain sockets without blocking
                while (do_run(0)) {}
                return true;
            } else {
                return false;
            }
#else
            // only a single message at the time
            return return ;
#endif
        }
    }

    // NB: do_close() will also be called in start().
    do_close();

    return true;
}

bool tcp_server::do_run(double timeout) {
    // NOTE: macOS/BSD requires the negative timeout to be exactly -1!
    auto timeout_ms = timeout >= 0 ? std::ceil(timeout * 1000) : -1;
#ifdef _WIN32
    int result = WSAPoll(poll_array_.data(), poll_array_.size(), timeout_ms);
#else
    int result = ::poll(poll_array_.data(), poll_array_.size(), timeout_ms);
#endif
    if (result < 0) {
        auto err = socket::get_last_error();
        if (err != EINTR) {
            throw tcp_error(err);
        }
    } else if (result == 0) {
        return false; // timeout
    }

    // drain event socket
    if (poll_array_[event_index()].revents != 0) {
        char dummy[64];
        try {
            event_socket_.receive(dummy, sizeof(dummy));
        } catch (const socket_error& e) {
            LOG_ERROR("tcp_server: could not drain event socket: " << e.what());
        }
    }

    // first receive from clients
    receive_from_clients();

    // finally accept new clients (modifies the client list!)
    for (size_t i = 0; i < listen_sockets_.size(); ++i) {
        accept_client(i);
    }

    return true;
}

void tcp_server::stop() {
    bool running = running_.exchange(false);
    if (running) {
        if (!event_socket_.signal()) {
            // force wakeup by closing the socket.
            // this is not nice and probably undefined behavior,
            // the MSDN docs explicitly forbid it!
            event_socket_.close();
        }
    }
}

void tcp_server::notify() {
    event_socket_.signal();
}

void tcp_server::do_close() {
    // close listening socket
    LOG_DEBUG("tcp_server: stop listening");
    for (auto& socket : listen_sockets_) {
        socket.close();
    }
    listen_sockets_.clear();

    event_socket_.close();

    clients_.clear();
    poll_array_.clear();
    stale_clients_.clear();
    client_count_ = 0;
}

int tcp_server::send(AooId client, const AooByte *data, AooSize size) {
    // LATER optimize look up
    for (auto& c : clients_) {
        if (c.id == client) {
            // there is some controversy on whether send() in blocking mode
            // might do partial writes. To be sure, we call send() in a loop.
            try {
                int32_t nbytes = 0;
                while (nbytes < size) {
                    auto result = c.socket.send(data + nbytes, size - nbytes);
                    nbytes += result;
                }
            } catch (const socket_error& e) {
                throw tcp_error(e);
            }

            return size;
        }
    }
    LOG_ERROR("tcp_server: send(): unknown client (" << client << ")");
    return 0; // ?
}

bool tcp_server::close(AooId client) {
    for (size_t i = 0; i < clients_.size(); ++i) {
        if (clients_[i].id == client) {
        #if 1
            // "lingering close": shutdown() will send FIN, causing the
            // client to terminate the connection. However, it is important
            // that we read all pending data before calling close(), otherwise
            // we might accidentally send RST and the client might lose data.
            clients_[i].socket.shutdown(shutdown_send);
            clients_[i].id = kAooIdInvalid;
        #else
            close_and_remove_client(i);
        #endif
            LOG_DEBUG("tcp_server: close client " << client);
            return true;
        }
    }
    return false;
}

void tcp_server::receive_from_clients() {
    for (int i = 0; i < clients_.size(); ++i) {
        auto& fdp = poll_array_[client_index() + i];
        auto revents = std::exchange(fdp.revents, 0);
        if (revents == 0) {
            continue; // no event
        }
    #ifdef _WIN32
        if (fdp.fd == INVALID_SOCKET) {
            // Windows does not simply ignore invalid socket descriptors,
            // instead it would return POLLNVAL...
            continue;
        }
    #endif
        auto& c = clients_[i];

        if (revents & POLLERR) {
            LOG_DEBUG("tcp_server: POLLERR");
        }
        if (revents & POLLHUP) {
            LOG_DEBUG("tcp_server: POLLHUP");
        }
        if (revents & POLLNVAL) {
            // invalid socket, shouldn't happen...
            LOG_DEBUG("tcp_server: POLLNVAL");
            handle_client_error(c, EINVAL);
            close_and_remove_client(i);
            return;
        }

        // receive data from client
        try {
            AooByte buffer[AOO_MAX_PACKET_SIZE];
            auto result = c.socket.receive(buffer, sizeof(buffer));
            if (c.id != kAooIdInvalid) {
                if (result > 0) {
                    // received data
                    receive_handler_(c.id, 0, buffer, result, c.address);
                } else {
                    // client disconnected
                    LOG_DEBUG("tcp_server: connection closed by client");
                    handle_client_error(c, 0);
                    close_and_remove_client(i);
                }
            } else {
                // "lingering close": read from socket until EOF, see close() method.
                if (result == 0) {
                    close_and_remove_client(i);
                }
            }
        } catch (const socket_error& e) {
            if (e.code() == EINTR) {
                continue;
            }
            LOG_DEBUG("tcp_server: recv() failed: " << e.what());
            if (c.id != kAooIdInvalid) {
                handle_client_error(c, e.code());
            }
            close_and_remove_client(i);
        }
    }

#if 1
    // purge stale clients (if necessary)
    if (stale_clients_.size() > max_stale_clients) {
        clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
            [](auto& c) { return !c.socket.is_open(); }), clients_.end());

        poll_array_.erase(std::remove_if(poll_array_.begin(), poll_array_.end(),
            [](auto& p) { return p.fd == invalid_socket; }), poll_array_.end());

        stale_clients_.clear();
    }
#endif
}

void tcp_server::close_and_remove_client(int index) {
    auto& c = clients_[index];
    c.socket.close();
    // mark as stale (will be ignored in poll())
    poll_array_[client_index() + index].fd = invalid_socket;
    stale_clients_.push_back(index);
    if (c.id != kAooIdInvalid) {
        LOG_DEBUG("tcp_server: close socket and remove client " << c.id);
    } else {
        LOG_DEBUG("tcp_server: close client socket");
    }
    client_count_--;
}

void tcp_server::accept_client(size_t listen_index) {
    auto revents = std::exchange(poll_array_[listen_index].revents, 0);
    if (revents == 0) {
        return; // no event
    }

    if (revents & POLLNVAL) {
        LOG_DEBUG("tcp_server: POLLNVAL");
        throw tcp_error(EINVAL);
    }
    if (revents & POLLERR) {
        LOG_DEBUG("tcp_server: POLLERR");
        // get actual error from accept() below
    }
    if (revents & POLLHUP) {
        LOG_DEBUG("tcp_server: POLLHUP");
        // shouldn't happen on listening socket...
    }

    try {
        auto [sock, addr] = listen_sockets_[listen_index].accept();
        auto sockfd = sock.native_handle();
        auto replyfn = [sockfd](const AooByte *data, AooSize size) {
            AooSize nbytes = 0;
            while (nbytes < size) {
                auto result = ::send(sockfd, (const char *)data + nbytes, size - nbytes, 0);
                if (result >= 0) {
                    nbytes += result;
                } else {
                    throw socket_error(socket::get_last_error());
                }
            }
            return nbytes;
        };
        auto id = accept_handler_(addr, replyfn);
        if (id == kAooIdInvalid) {
            // user refused to accept client
            return;
        }

        if (!stale_clients_.empty()) {
            // reuse stale client
            auto index = stale_clients_.back();
            stale_clients_.pop_back();

            clients_[index] = client { addr, std::move(sock), id };
            poll_array_[client_index() + index].fd = sockfd;
        } else {
            // add new client
            clients_.push_back(client { addr, std::move(sock), id });
            pollfd p;
            p.fd = sockfd;
            p.events = POLLIN;
            p.revents = 0;
            poll_array_.push_back(p);
        }
        LOG_DEBUG("tcp_server: accepted client " << addr << " " << id);
        client_count_++;
    } catch (const accept_error& e) {
        handle_accept_error(e);
    }
}

void tcp_server::handle_accept_error(const accept_error& e) {
    // avoid spamming the console with repeated errors
    thread_local ip_address last_addr;
    if (e.code() != last_error_ || e.address() != last_addr) {
        if (e.address().valid()) {
            LOG_DEBUG("tcp_server: could not accept " << e.address() << ": " << e.what());
        } else {
            LOG_DEBUG("tcp_server: accept() failed: " << e.what());
        }
    }
    last_error_ = e.code();
    last_addr = e.address();
    // handle error
    switch (e.code()) {
    // We ran out of open file descriptors.
#ifdef _WIN32
    case WSAEMFILE:
#else
    case EMFILE:
    case ENFILE:
#endif
        // Sleep to avoid hogging the CPU, but do not stop the server
        // because we might be able to recover.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        break;

    // non-fatal errors
    // TODO: should we rather explicitly check for fatal errors instead?
#ifdef _WIN32
    // remote peer has terminated the connection
    case WSAECONNRESET:
#else
    // remote peer has terminated the connection
    case ECONNABORTED:
    // reached process/system limit for open file descriptors
    // interrupted by signal handler
    case EINTR:
    // not allowed by firewall rules
    case EPERM:
#endif
#ifdef __linux__
    // On Linux, also ignore TCP/IP errors! See man page for accept().
    case ENETDOWN:
    case EPROTO:
    case ENOPROTOOPT:
    case EHOSTDOWN:
    case ENONET:
    case EHOSTUNREACH:
    case EOPNOTSUPP:
    case ENETUNREACH:
#endif
        break;

    default:
        // fatal error
        throw tcp_error(e);
    }
}

} // aoo
