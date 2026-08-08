#include "udp_server.hpp"

#include "common/log.hpp"
#include "common/utils.hpp"

#include <cmath>

namespace aoo {

void udp_server::start(int port, receive_handler receive, bool threaded) {
    start(std::vector<int>{ port }, std::move(receive), threaded);
}

void udp_server::start(const std::vector<int>& ports, receive_handler receive, bool threaded) {
    do_close();

    if (ports.empty()) {
        throw udp_error(EINVAL);
    }

    receive_handler_ = std::move(receive);

    // Don't try to reuse ports because it would lead to silent errors
    // if the port is already taken by another application.
    // Also, it can cause deadlocks when trying to signal the socket
    // and join the network thread.
    // TODO: figure out if some operating systems let UDP sockets linger.
    try {
        sockets_.reserve(ports.size());
        for (auto port : ports) {
            sockets_.emplace_back(port_tag{}, port, false);
        }
        bind_addr_ = sockets_.front().address();
    } catch (const socket_error& e) {
        for (auto& socket : sockets_) {
            socket.close();
        }
        sockets_.clear();
        throw udp_error(e);
    }

    for (auto& socket : sockets_) {
        if (send_buffer_size_ > 0) {
            try {
                socket.set_send_buffer_size(send_buffer_size_);
            } catch (const socket_error& e) {
                socket::print_error(e.code(),
                    "udp_server: could not set send buffer size");
            }
        }

        if (receive_buffer_size_ > 0) {
            try {
                socket.set_receive_buffer_size(receive_buffer_size_);
            } catch (const socket_error& e) {
                socket::print_error(e.code(),
                    "udp_server: could not set receive buffer size");
            }
        }
    }

    running_.store(true);
    threaded_ = threaded;
    if (threaded_) {
        packet_queue_.clear();
        // TODO: lower thread priority?
        thread_ = std::thread([this](){
            try {
                this->receive(-1.0);
            } catch (const udp_error& e) {
                LOG_DEBUG("udp_server: thread function failed: " << e.what());
                // TODO: report error to main thread
            }
            running_.store(false);
        });
    }
}

bool udp_server::run(double timeout) {
    if (timeout >= 0) {
        // 1) with timeout
        if (threaded_) {
            // a) threaded
            if (timeout == 0) {
                if (!packet_queue_.empty()) {
                    packet_queue_.consume_all([this](const auto& packet){
                        receive_handler_(packet.data.data(), packet.data.size(),
                                         packet.address, packet.socket_index);
                    });
                    return true;
                } else {
                    return false;
                }
            } else {
                if (event_.wait_for(timeout)) {
                    packet_queue_.consume_all([this](const auto& packet){
                        receive_handler_(packet.data.data(), packet.data.size(),
                                         packet.address, packet.socket_index);
                    });
                    return true;
                } else {
                    return false;
                }
            }
        } else {
            // b) non-threaded
#if 1
            if (receive(timeout)) {
                // drain sockets without blocking
                while (receive(0)) {}
                return true;
            } else {
                return false;
            }
#else
            // only a single packet at the time
            return receive(timeout);
#endif
        }
    } else {
        // 2) blocking
        if (threaded_) {
            // a) threaded
            while (running_.load()) {
                packet_queue_.consume_all([&](const auto& packet){
                    receive_handler_(packet.data.data(), packet.data.size(),
                                     packet.address, packet.socket_index);
                });
                // wait for packets
                event_.wait();
            }
        } else {
            // b) non-threaded
            while (running_.load()) {
                receive(-1.0);
            }
        }

        do_close();

        return true;
    }
}

void udp_server::stop() {
    bool running = running_.exchange(false);
    if (running) {
        // wake up receive
        bool signalled = false;
        for (auto& socket : sockets_) {
            signalled = socket.signal() || signalled;
        }
        if (!signalled) {
            // force wakeup by closing the socket.
            // this is not nice and probably undefined behavior,
            // the MSDN docs explicitly forbid it!
            for (auto& socket : sockets_) {
                socket.close();
            }
        }
        if (threaded_) {
            // wake up main thread
            event_.set();
            // join receive thread
            if (thread_.joinable()) {
                thread_.join();
            }
        }
    }
}

void udp_server::notify() {
    if (threaded_) {
        event_.set(); // wake up main thread
    } else {
        for (auto& socket : sockets_) {
            socket.signal();
        }
    }
}

void udp_server::do_close() {
    for (auto& socket : sockets_) {
        socket.close();
    }
    sockets_.clear();
    bind_addr_.clear();
    if (thread_.joinable()) {
        thread_.join();
    }
}

udp_server::~udp_server() {
    stop();
    do_close();
}

bool udp_server::receive(double timeout) {
    try {
        std::vector<pollfd> poll_array(sockets_.size());
        for (size_t i = 0; i < sockets_.size(); ++i) {
            poll_array[i].fd = sockets_[i].native_handle();
            poll_array[i].events = POLLIN;
            poll_array[i].revents = 0;
        }
        auto timeout_ms = timeout >= 0 ? (int)std::ceil(timeout * 1000) : -1;
#ifdef _WIN32
        auto result = WSAPoll(poll_array.data(), (ULONG)poll_array.size(), timeout_ms);
#else
        auto result = ::poll(poll_array.data(), poll_array.size(), timeout_ms);
#endif
        if (result < 0) {
            throw socket_error(socket::get_last_error());
        } else if (result == 0) {
            return false;
        }
        for (size_t i = 0; i < poll_array.size(); ++i) {
            if (poll_array[i].revents != 0) {
                return receive_from_socket(i);
            }
        }
        return true;
    } catch (const socket_error& e) {
#ifdef _WIN32
        // ignore ICMP Port Unreachable message!
        if (e.code() == WSAECONNRESET) {
            return true; // continue
        }
#else
        if (e.code() == EINTR){
            return true; // continue
        }
#endif
        // notify main thread (if blocking)
        if (threaded_) {
            running_.store(false);
            event_.set();
        }

        throw udp_error(e);
    }
}

bool udp_server::receive_from_socket(size_t index) {
    aoo::ip_address address;
    auto [success, result] = sockets_[index].receive(buffer_.data(), buffer_.size(),
                                                     address, 0);
    if (!success) {
        return false;
    }
    if (result > 0) {
        if (threaded_) {
            packet_queue_.produce([&, len=result](auto& packet){
                packet.data.assign(buffer_.data(), buffer_.data() + len);
                packet.address = address;
                packet.socket_index = index;
            });
            event_.set(); // notify main thread (if blocking)
        } else {
            receive_handler_(buffer_.data(), result, address, index);
        }
    }
    // ignore empty packets used for signalling
    return true;
}

} // aoo
