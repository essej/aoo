#pragma once

#include "aoo.h"

#include "common/utils.hpp"

#include "osc/OscReceivedElements.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace aoo {
namespace net {

enum class wire_protocol : uint8_t {
    unknown,
    current,
    legacy,
    invalid
};

static constexpr uint8_t slip_end = 0xc0;
static constexpr uint8_t slip_escape = 0xdb;
static constexpr uint8_t slip_escaped_end = 0xdc;
static constexpr uint8_t slip_escaped_escape = 0xdd;
static constexpr int32_t max_stream_packet_size = 16 * 1024 * 1024;

inline wire_protocol detect_wire_protocol(const AooByte *data, AooSize size) noexcept {
    if (size <= 0) {
        return wire_protocol::unknown;
    }
    if (data[0] == slip_end) {
        return wire_protocol::legacy;
    }
    if (size < (AooSize)sizeof(int32_t)) {
        return wire_protocol::unknown;
    }

    auto message_size = from_bytes<int32_t>((const char *)data);
    if (message_size <= 0 || message_size > max_stream_packet_size
            || (message_size & 3) != 0) {
        return wire_protocol::invalid;
    }
    return wire_protocol::current;
}

inline std::vector<AooByte> slip_encode(const AooByte *data, AooSize size) {
    std::vector<AooByte> result;
    result.reserve(size + 2);
    result.push_back(slip_end);
    for (AooSize i = 0; i < size; ++i) {
        switch (data[i]) {
        case slip_end:
            result.push_back(slip_escape);
            result.push_back(slip_escaped_end);
            break;
        case slip_escape:
            result.push_back(slip_escape);
            result.push_back(slip_escaped_escape);
            break;
        default:
            result.push_back(data[i]);
            break;
        }
    }
    result.push_back(slip_end);
    return result;
}

class slip_stream_receiver {
public:
    template<typename Fn>
    void handle_message(const AooByte *data, AooSize size, Fn&& handler) {
        for (AooSize i = 0; i < size; ++i) {
            const auto byte = data[i];
            if (!started_) {
                if (byte != slip_end) {
                    reset();
                    throw osc::MalformedPacketException("SLIP packet does not start with END");
                }
                started_ = true;
                continue;
            }

            if (escaped_) {
                escaped_ = false;
                if (byte == slip_escaped_end) {
                    append(slip_end);
                } else if (byte == slip_escaped_escape) {
                    append(slip_escape);
                } else {
                    reset();
                    throw osc::MalformedPacketException("bad SLIP escape sequence");
                }
            } else if (byte == slip_escape) {
                escaped_ = true;
            } else if (byte == slip_end) {
                if (!packet_.empty()) {
                    handler(packet_.data(), (AooSize)packet_.size());
                    packet_.clear();
                }
            } else {
                append(byte);
            }
        }
    }

    void reset() noexcept {
        packet_.clear();
        started_ = false;
        escaped_ = false;
    }

private:
    void append(AooByte byte) {
        if (packet_.size() >= (size_t)max_stream_packet_size) {
            reset();
            throw osc::MalformedPacketException("SLIP packet too large");
        }
        packet_.push_back(byte);
    }

    std::vector<AooByte> packet_;
    bool started_ = false;
    bool escaped_ = false;
};

} // net
} // aoo
