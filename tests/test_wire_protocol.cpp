#include "aoo/src/net/wire_protocol.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace aoo;
using namespace aoo::net;

int main() {
    const AooByte legacy[] = { slip_end, '/', 'a', slip_end };
    assert(detect_wire_protocol(legacy, 1) == wire_protocol::legacy);

    AooByte current[4];
    to_bytes<int32_t>(12, (char *)current);
    assert(detect_wire_protocol(current, 1) == wire_protocol::unknown);
    assert(detect_wire_protocol(current, 3) == wire_protocol::unknown);
    assert(detect_wire_protocol(current, 4) == wire_protocol::current);

    to_bytes<int32_t>(0, (char *)current);
    assert(detect_wire_protocol(current, 4) == wire_protocol::invalid);
    to_bytes<int32_t>(10, (char *)current);
    assert(detect_wire_protocol(current, 4) == wire_protocol::invalid);
    to_bytes<int32_t>(max_stream_packet_size + 4, (char *)current);
    assert(detect_wire_protocol(current, 4) == wire_protocol::invalid);

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
        assert(packets.size() == 1);
        assert(packets.front() == payload);
    }

    slip_stream_receiver receiver;
    std::vector<std::vector<AooByte>> packets;
    auto collect = [&](const AooByte *data, AooSize size) {
        packets.emplace_back(data, data + size);
    };
    auto twice = encoded;
    twice.insert(twice.end(), encoded.begin(), encoded.end());
    receiver.handle_message(twice.data(), (AooSize)twice.size(), collect);
    assert(packets.size() == 2);
    assert(packets[0] == payload && packets[1] == payload);

    bool malformed = false;
    const AooByte bad_escape[] = { slip_end, '/', slip_escape, 0, slip_end };
    try {
        receiver.reset();
        receiver.handle_message(bad_escape, sizeof(bad_escape), collect);
    } catch (const osc::MalformedPacketException&) {
        malformed = true;
    }
    assert(malformed);

    std::cout << "wire protocol test succeeded!" << std::endl;
    return EXIT_SUCCESS;
}
