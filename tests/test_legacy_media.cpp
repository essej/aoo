#include "aoo.h"
#include "aoo_sink.hpp"
#include "aoo_source.hpp"
#include "codec/aoo_pcm.h"
#if AOO_USE_OPUS
#include "codec/aoo_opus.h"
#endif

#include "common/net_utils.hpp"
#include "common/utils.hpp"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace aoo;

namespace {

[[noreturn]] void fail(const char *message) {
    std::cerr << "legacy media test failed: " << message << std::endl;
    std::abort();
}

void check(bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

struct bridge {
    AooSource *source;
    AooSink *sink;
    ip_address source_address{"127.0.0.1", 19001};
    ip_address sink_address{"127.0.0.1", 19002};
    bool saw_format = false;
    bool saw_legacy_pcm24 = false;
    bool saw_data = false;
};

struct invite_capture {
    bool saw_legacy_invite = false;
    std::array<AooByte, AOO_MAX_PACKET_SIZE> packet {};
    AooInt32 packet_size = 0;
};

AooError AOO_CALL capture_invite(void *user, const AooByte *data, AooInt32 size,
                                 const void *, AooAddrSize, AooFlag) {
    auto& state = *static_cast<invite_capture *>(user);
    osc::ReceivedPacket packet((const char *)data, size);
    osc::ReceivedMessage message(packet);
    if (!std::strcmp(message.AddressPattern(), "/aoo/src/0/invite")) {
        auto it = message.ArgumentsBegin();
        state.saw_legacy_invite = message.ArgumentCount() == 2
                && (it++)->AsInt32() == 42 && (it++)->AsInt32() == 0;
        state.packet_size = std::min<AooInt32>(size, state.packet.size());
        std::memcpy(state.packet.data(), data, state.packet_size);
    }
    return kAooOk;
}

void check_legacy_invite() {
    auto sink = AooSink::create(42);
    check(sink && sink->setup(1, 48000, 64, 0) == kAooOk,
          "could not setup invite sink");
    ip_address address("127.0.0.1", 19004);
    AooEndpoint source { address.address(), address.length(), 0 };
    check(sink->setSourceLegacyProtocol(source, kAooTrue) == kAooOk,
          "could not enable legacy invite wire");
    check(sink->inviteSource(source, nullptr) == kAooOk,
          "could not invite legacy source 0");

    std::array<AooSample, 64> output {};
    AooSample *outputs[] = {output.data()};
    invite_capture state;
    for (int i = 0; i < 100 && !state.saw_legacy_invite; ++i) {
        sink->process(outputs, 64, aoo_getCurrentNtpTime(), nullptr, nullptr);
        sink->send(capture_invite, &state);
    }
    check(state.saw_legacy_invite, "legacy source 0 invite schema changed");

    auto source_zero = AooSource::create(0);
    check(source_zero && source_zero->setup(1, 48000, 64, 0) == kAooOk,
          "could not setup legacy source 0");
    check(source_zero->handleMessage(state.packet.data(), state.packet_size,
                                     address.address(), address.length()) == kAooOk,
          "legacy source 0 invite was not accepted");
}

AooError AOO_CALL send_to_sink(void *user, const AooByte *data, AooInt32 size,
                               const void *, AooAddrSize, AooFlag) {
    auto& state = *static_cast<bridge *>(user);
    osc::ReceivedPacket packet((const char *)data, size);
    osc::ReceivedMessage message(packet);
    if (std::strstr(message.AddressPattern(), "/format")) {
        state.saw_format = true;
        auto it = message.ArgumentsBegin();
        for (int i = 0; i < 7; ++i) {
            ++it;
        }
        const void *extension;
        osc::osc_bundle_element_size_t size;
        it->AsBlob(extension, size);
        state.saw_legacy_pcm24 = size == 4
                && aoo::from_bytes<int32_t>((const AooByte *)extension) == 1;
    } else if (std::strstr(message.AddressPattern(), "/data")) {
        check(message.ArgumentCount() == 9, "legacy data schema changed");
        state.saw_data = true;
    }
    return state.sink->handleMessage(data, size, state.source_address.address(),
                                     state.source_address.length());
}

AooError AOO_CALL send_to_source(void *user, const AooByte *data, AooInt32 size,
                                 const void *, AooAddrSize, AooFlag) {
    auto& state = *static_cast<bridge *>(user);
    return state.source->handleMessage(data, size, state.sink_address.address(),
                                       state.sink_address.length());
}

#if AOO_USE_OPUS
void check_legacy_opus_format(AooSink& sink, const ip_address& source_address,
                              AooId source_id, const AooByte *extension,
                              AooInt32 extension_size, AooInt32 application)
{
    char address[64];
    snprintf(address, sizeof(address), "/aoo/sink/2/format");
    char buffer[AOO_MAX_PACKET_SIZE];
    osc::OutboundPacketStream message(buffer, sizeof(buffer));
    message << osc::BeginMessage(address)
            << source_id << (int32_t)(2 << 24) << source_id
            << (int32_t)1 << (int32_t)48000 << (int32_t)64 << "opus"
            << osc::Blob(extension, extension_size) << osc::EndMessage;
    check(sink.handleMessage((const AooByte *)message.Data(), message.Size(),
                             source_address.address(), source_address.length()) == kAooOk,
          "legacy Opus format was rejected");

    AooEndpoint endpoint { source_address.address(), source_address.length(), source_id };
    AooFormatStorage format {};
    check(sink.getSourceFormat(endpoint, format) == kAooOk,
          "legacy Opus format was not stored");
    check(((const AooFormatOpus *)&format)->applicationType == application,
          "legacy Opus application type was decoded incorrectly");
}

void check_legacy_opus_formats(AooSink& sink, const ip_address& source_address)
{
    std::array<AooByte, 16> extension {};
    aoo::to_bytes<int32_t>(OPUS_APPLICATION_RESTRICTED_LOWDELAY, extension.data());
    check_legacy_opus_format(sink, source_address, 10, extension.data(), 4,
                             OPUS_APPLICATION_RESTRICTED_LOWDELAY);

    aoo::to_bytes<int32_t>(-1000, extension.data());
    aoo::to_bytes<int32_t>(10, extension.data() + 4);
    aoo::to_bytes<int32_t>(-1000, extension.data() + 8);
    check_legacy_opus_format(sink, source_address, 11, extension.data(), 12,
                             OPUS_APPLICATION_AUDIO);

    aoo::to_bytes<int32_t>(OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                           extension.data() + 12);
    check_legacy_opus_format(sink, source_address, 12, extension.data(), 16,
                             OPUS_APPLICATION_RESTRICTED_LOWDELAY);
}
#endif

} // namespace

int main() {
    AooSettings settings;
    check(aoo_initialize(&settings) == kAooOk, "could not initialize AOO");

    auto source = AooSource::create(1);
    auto sink = AooSink::create(2);
    check(source && sink, "could not create source and sink");

    check_legacy_invite();

    constexpr int block_size = 64;
    constexpr int sample_rate = 48000;
    check(source->setup(1, sample_rate, block_size, 0) == kAooOk,
          "could not setup source");
    check(sink->setup(1, sample_rate, block_size, 0) == kAooOk,
          "could not setup sink");

#if AOO_USE_OPUS
    check_legacy_opus_formats(*sink, ip_address("127.0.0.1", 19003));
#endif

    AooFormatPcm format;
    AooFormatPcm_init(&format, 1, sample_rate, block_size, kAooPcmInt24);
    check(source->setFormat(format.header) == kAooOk, "could not set PCM24 format");

    bridge state{source.get(), sink.get()};
    AooEndpoint sink_endpoint{state.sink_address.address(),
                              state.sink_address.length(), 2};
    AooEndpoint source_endpoint{state.source_address.address(),
                                state.source_address.length(), 1};
    check(source->addSink(sink_endpoint, kAooTrue) == kAooOk, "could not add sink");
    check(source->setSinkLegacyProtocol(sink_endpoint, kAooTrue) == kAooOk,
          "could not enable legacy source wire");
    check(sink->setSourceLegacyProtocol(source_endpoint, kAooTrue) == kAooOk,
          "could not enable legacy sink wire");
    check(source->startStream(0, nullptr) == kAooOk, "could not start stream");

    std::array<AooSample, block_size> input;
    std::array<AooSample, block_size> output;
    AooSample *inputs[] = {input.data()};
    AooSample *outputs[] = {output.data()};
    bool heard_audio = false;

    for (int block = 0; block < 300 && !heard_audio; ++block) {
        for (int i = 0; i < block_size; ++i) {
            auto sample = block * block_size + i;
            input[i] = (AooSample)(0.25 * std::sin(sample * 2.0 * 3.141592653589793
                                                   * 440.0 / sample_rate));
        }
        auto time = aoo_getCurrentNtpTime();
        source->process(inputs, block_size, time);
        source->send(send_to_sink, &state);
        sink->send(send_to_source, &state);
        std::fill(output.begin(), output.end(), 0);
        sink->process(outputs, block_size, time, nullptr, nullptr);
        heard_audio = std::any_of(output.begin(), output.end(), [](auto sample) {
            return std::abs(sample) > 0.001f;
        });
    }

    check(state.saw_format, "legacy format message was not emitted");
    check(state.saw_legacy_pcm24, "PCM24 bit depth was not mapped to legacy wire");
    check(state.saw_data, "legacy data message was not emitted");
    check(heard_audio, "legacy PCM24 audio did not decode");
    source.reset();
    sink.reset();
    aoo_terminate();
    std::cout << "legacy media test succeeded!" << std::endl;
    return EXIT_SUCCESS;
}
