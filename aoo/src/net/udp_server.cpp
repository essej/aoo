#include "udp_server.hpp"

#include "common/log.hpp"
#include "common/utils.hpp"

namespace aoo {

void udp_server::start(int port, receive_handler receive, bool threaded) {
    do_close();

    receive_handler_ = std::move(receive);

    // Don't try to reuse ports because it would lead to silent errors
    // if the port is already taken by another application.
    // Also, it can cause deadlocks when trying to signal the socket
    // and join the network thread.
    // TODO: figure out if some operating systems let UDP sockets linger.
    try {
        socket_ = udp_socket(port_tag{}, port, false);
        bind_addr_ = socket_.address();
    } catch (const socket_error& e) {
        throw udp_error(e);
    }

    if (send_buffer_size_ > 0) {
        try {
            socket_.set_send_buffer_size(send_buffer_size_);
        } catch (const socket_error& e) {
            socket::print_error(e.code(),
                "udp_server: could not send send buffer size");
        }
    }

    if (receive_buffer_size_ > 0) {
        try {
            socket_.set_receive_buffer_size(receive_buffer_size_);
        } catch (const socket_error& e) {
            socket::print_error(e.code(),
                "udp_server: could not send receive buffer size");
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
                        receive_handler_(packet.data.data(), packet.data.size(), packet.address);
                    });
                    return true;
                } else {
                    return false;
                }
            } else {
                if (event_.wait_for(timeout)) {
                    packet_queue_.consume_all([this](const auto& packet){
                        receive_handler_(packet.data.data(), packet.data.size(), packet.address);
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
                    receive_handler_(packet.data.data(), packet.data.size(), packet.address);
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
        if (!socket_.signal()) {
            // force wakeup by closing the socket.
            // this is not nice and probably undefined behavior,
            // the MSDN docs explicitly forbid it!
            socket_.close();
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
        socket_.signal();
    }
}

void udp_server::do_close() {
    socket_.close();
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
        aoo::ip_address address;
        auto [success, result] = socket_.receive(buffer_.data(), buffer_.size(),
                                                 address, timeout);
        if (success) {
            if (result > 0) {
                if (threaded_) {
                    packet_queue_.produce([&, len=result](auto& packet){
                        packet.data.assign(buffer_.data(), buffer_.data() + len);
                        packet.address = address;
                    });
                    event_.set(); // notify main thread (if blocking)
                } else {
                    receive_handler_(buffer_.data(), result, address);
                }
            }
            // ignore timeout or empty packet (used for signalling)
            return true;
        } else {
            // timeout
            return false;
        }
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

} // aoo
