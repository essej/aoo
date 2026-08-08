#include "aoo/src/net/wire_protocol.hpp"
#include "aoo/src/net/osc_stream_receiver.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace aoo;
using namespace aoo::net;

namespace {
void check(bool condition) {
    if (!condition) {
        std::cerr << "wire protocol test failed" << std::endl;
        std::exit(EXIT_FAILURE);
    }
}
}

int main() {
    const AooByte legacy[] = { slip_end, '/', 'a', slip_end };
    check(detect_wire_protocol(legacy, 1) == wire_protocol::legacy);

    AooByte current[4];
    to_bytes<int32_t>(12, (char *)current);
    check(detect_wire_protocol(current, 1) == wire_protocol::unknown);
    check(detect_wire_protocol(current, 3) == wire_protocol::unknown);
    check(detect_wire_protocol(current, 4) == wire_protocol::current);

    to_bytes<int32_t>(0, (char *)current);
    check(detect_wire_protocol(current, 4) == wire_protocol::invalid);
    to_bytes<int32_t>(10, (char *)current);
    check(detect_wire_protocol(current, 4) == wire_protocol::invalid);
    to_bytes<int32_t>(max_stream_packet_size + 4, (char *)current);
    check(detect_wire_protocol(current, 4) == wire_protocol::invalid);

    const std::vector<AooByte> payload = { '/', 'a', 0, slip_end, slip_escape, 1, 2, 3 };
    const auto encoded = slip_encode(payload.data(), (AooSize)payload.size());
    for (size_t split = 0; split <= encoded.size(); ++split) {
        slip_stream_receiver receiver;
        std::vector<std::vector<AooByte>> packets;
        auto collect = [&](const AooByte *data, AooSize size) {
            packets.emplace_back(data, data + size);
        };
        if (split > 0) {
            receiver.handle_message(encoded.data(), (AooSize)split, collect);
        }
        if (split < encoded.size()) {
            receiver.handle_message(encoded.data() + split,
                                    (AooSize)(encoded.size() - split), collect);
        }
        check(packets.size() == 1);
        check(packets.front() == payload);
    }

    slip_stream_receiver receiver;
    std::vector<std::vector<AooByte>> packets;
    auto collect = [&](const AooByte *data, AooSize size) {
        packets.emplace_back(data, data + size);
    };
    auto twice = encoded;
    twice.insert(twice.end(), encoded.begin(), encoded.end());
    receiver.handle_message(twice.data(), (AooSize)twice.size(), collect);
    check(packets.size() == 2);
    check(packets[0] == payload && packets[1] == payload);

    bool malformed = false;
    const AooByte bad_escape[] = { slip_end, '/', slip_escape, 0, slip_end };
    try {
        receiver.reset();
        receiver.handle_message(bad_escape, sizeof(bad_escape), collect);
    } catch (const osc::MalformedPacketException&) {
        malformed = true;
    }
    check(malformed);

    // Every frame, not just the protocol probe, must enforce the size cap.
    osc_stream_receiver current_receiver;
    to_bytes<int32_t>(max_stream_packet_size + 4, (char *)current);
    malformed = false;
    try {
        current_receiver.handle_message((const char *)current, sizeof(current),
                                         [](const osc::ReceivedPacket&) {});
    } catch (const osc::MalformedPacketException&) {
        malformed = true;
    }
    check(malformed);

    std::cout << "wire protocol test succeeded!" << std::endl;
    return EXIT_SUCCESS;
}
