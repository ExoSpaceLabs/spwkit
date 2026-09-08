// SPDX-License-Identifier: Apache-2.0

#include <CCSDSPack.h>
#include <spwkit/device.h>
#include <spwkit/spwkit.hpp>
#include <spwkit/udp.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr spwkit::Timeout io_timeout_us = UINT64_C(5000000);
constexpr unsigned state_timeout_ms = 15000u;
constexpr std::uint32_t link_id = 90u;
constexpr std::size_t rx_capacity = 4096u;

int fail_ccsds(const char* operation, const ccsds::Error& error) {
    std::fprintf(stderr,
                 "%s failed: code=%d message=%s\n",
                 operation,
                 error.code(),
                 error.message().c_str());
    return 1;
}

bool build_tc(std::vector<std::uint8_t>& wire) {
    ccsds::Packet packet;
    if (const auto result = packet.setPrimaryHeader(ccsds::PrimaryHeader{
            0U, 0U, 0U, 0x123U, ccsds::UNSEGMENTED, 7U, 0U}); !result) {
        return fail_ccsds("TC setPrimaryHeader", result.error()) == 0;
    }
    if (const auto result = packet.setSecondaryHeader(
            std::make_shared<ccsds::pus::rev_c::TcHeader>(
                17U, 1U, 0x1234U, 0x09U)); !result) {
        return fail_ccsds("TC setSecondaryHeader", result.error()) == 0;
    }
    if (const auto result = packet.setApplicationData({0x60U, 0x70U}); !result) {
        return fail_ccsds("TC setApplicationData", result.error()) == 0;
    }

    const auto serialized = packet.serialize();
    if (!serialized) {
        fail_ccsds("TC serialize", serialized.error());
        return false;
    }
    wire = serialized.value();
    return true;
}

ccsds::pus::rev_c::TmTailoring tm_tailoring() {
    ccsds::pus::rev_c::TmTailoring tailoring;
    tailoring.timestampPresent = true;
    tailoring.cuc = {
        ccsds::time::Epoch::Ccsds1958Tai,
        ccsds::time::PFieldMode::Implicit,
        4U,
        0U,
    };
    return tailoring;
}

bool build_tm(std::vector<std::uint8_t>& wire) {
    const auto tailoring = tm_tailoring();

    ccsds::Packet packet;
    if (const auto result = packet.setPrimaryHeader(ccsds::PrimaryHeader{
            0U, 1U, 0U, 0x456U, ccsds::UNSEGMENTED, 8U, 0U}); !result) {
        return fail_ccsds("TM setPrimaryHeader", result.error()) == 0;
    }
    if (const auto result = packet.setSecondaryHeader(
            std::make_shared<ccsds::pus::rev_c::TmHeader>(
                tailoring,
                3U,
                25U,
                7U,
                0x0102U,
                5U,
                ccsds::time::CucTime{0x11223344U, 0U})); !result) {
        return fail_ccsds("TM setSecondaryHeader", result.error()) == 0;
    }
    if (const auto result = packet.setApplicationData({0x80U, 0x81U}); !result) {
        return fail_ccsds("TM setApplicationData", result.error()) == 0;
    }

    const auto serialized = packet.serialize();
    if (!serialized) {
        fail_ccsds("TM serialize", serialized.error());
        return false;
    }
    wire = serialized.value();
    return true;
}

bool report_valid(const char* label, const ccsds::ValidationReport& report) {
    if (report.valid()) {
        return true;
    }
    std::fprintf(stderr, "%s validation failed:\n", label);
    for (const auto& check : report) {
        if (!check.passed) {
            std::fprintf(stderr,
                         "  - %s\n",
                         ccsds::validationCodeName(check.code));
        }
    }
    return false;
}

bool validate_tc(const std::vector<std::uint8_t>& wire) {
    ccsds::Packet decoded;
    const auto consumed =
        decoded.deserializeBounded<ccsds::pus::rev_c::TcHeader>(wire);
    if (!consumed) {
        fail_ccsds("TC deserializeBounded", consumed.error());
        return false;
    }

    const auto header =
        std::static_pointer_cast<const ccsds::pus::rev_c::TcHeader>(
            decoded.getSecondaryHeader());
    const std::vector<std::uint8_t> expected_application{0x60U, 0x70U};
    if (consumed.value() != wire.size() || !header ||
        header->getServiceType() != 17U ||
        header->getServiceSubtype() != 1U ||
        header->getSourceId() != 0x1234U ||
        header->getAcknowledgementFlags() != 0x09U ||
        decoded.getDirection() != ccsds::PacketDirection::Telecommand ||
        decoded.getApplicationDataBytes() != expected_application) {
        std::fprintf(stderr, "TC metadata/application-data mismatch after parsing\n");
        return false;
    }

    ccsds::Validator validator;
    return report_valid("TC", validator.validate(decoded));
}

bool validate_tm(const std::vector<std::uint8_t>& wire) {
    const auto tailoring = tm_tailoring();

    ccsds::Packet decoded;
    const auto consumed =
        decoded.deserializeBounded<ccsds::pus::rev_c::TmHeader>(wire, tailoring);
    if (!consumed) {
        fail_ccsds("TM deserializeBounded", consumed.error());
        return false;
    }

    const auto header =
        std::static_pointer_cast<const ccsds::pus::rev_c::TmHeader>(
            decoded.getSecondaryHeader());
    const std::vector<std::uint8_t> expected_application{0x80U, 0x81U};
    if (consumed.value() != wire.size() || !header ||
        header->getServiceType() != 3U ||
        header->getServiceSubtype() != 25U ||
        header->getMessageTypeCounter() != 7U ||
        header->getDestinationId() != 0x0102U ||
        header->getTimeReferenceStatus() != 5U ||
        header->getTimestamp() != ccsds::time::CucTime{0x11223344U, 0U} ||
        decoded.getDirection() != ccsds::PacketDirection::Telemetry ||
        decoded.getApplicationDataBytes() != expected_application) {
        std::fprintf(stderr, "TM metadata/application-data mismatch after parsing\n");
        return false;
    }

    ccsds::Validator validator;
    return report_valid("TM", validator.validate(decoded));
}

bool wait_for_run(spwkit::Port& port) {
    const unsigned attempts = state_timeout_ms / 10u + 1u;
    for (unsigned attempt = 0u; attempt < attempts; ++attempt) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (port.link_state(state) == SPW_OK && state == SPW_LINK_RUN) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::fprintf(stderr, "SpWKit peer did not reach SPW_LINK_RUN\n");
    return false;
}

bool exchange(spwkit::Port& port, char id, const char* backend_name) {
    std::vector<std::uint8_t> local_wire;
    std::vector<std::uint8_t> expected_peer_wire;

    if (id == 'A') {
        if (!build_tc(local_wire) || !build_tm(expected_peer_wire)) {
            return false;
        }
    } else {
        if (!build_tm(local_wire) || !build_tc(expected_peer_wire)) {
            return false;
        }
    }

    if (port.send(local_wire.data(),
                  local_wire.size(),
                  SPW_TERMINATOR_EOP,
                  io_timeout_us) != SPW_OK) {
        std::fprintf(stderr, "SpWKit send failed\n");
        return false;
    }

    std::array<std::uint8_t, rx_capacity> rx_storage{};
    spw_packet_t rx{
        rx_storage.data(),
        0U,
        rx_storage.size(),
        SPW_TERMINATOR_EOP,
    };
    if (port.receive(rx, io_timeout_us) != SPW_OK) {
        std::fprintf(stderr, "SpWKit receive failed\n");
        return false;
    }

    if (rx.terminator != SPW_TERMINATOR_EOP ||
        rx.length != expected_peer_wire.size() ||
        std::memcmp(rx.data,
                    expected_peer_wire.data(),
                    expected_peer_wire.size()) != 0) {
        std::fprintf(stderr,
                     "transport byte mismatch backend=%s id=%c expected=%zu actual=%zu terminator=%d\n",
                     backend_name,
                     id,
                     expected_peer_wire.size(),
                     rx.length,
                     static_cast<int>(rx.terminator));
        return false;
    }

    const std::vector<std::uint8_t> received(rx.data, rx.data + rx.length);
    const bool parsed = id == 'A' ? validate_tm(received) : validate_tc(received);
    if (!parsed) {
        return false;
    }

    std::printf("PASS backend=%s id=%c tx=%s rx=%s tx_bytes=%zu rx_bytes=%zu terminator=EOP\n",
                backend_name,
                id,
                id == 'A' ? "PUS:revC:TC" : "PUS:revC:TM",
                id == 'A' ? "PUS:revC:TM" : "PUS:revC:TC",
                local_wire.size(),
                received.size());
    return true;
}

bool parse_port(const char* text, std::uint16_t& value) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (text[0] == '\0' || end == nullptr || *end != '\0' ||
        parsed == 0UL || parsed > UINT16_MAX) {
        return false;
    }
    value = static_cast<std::uint16_t>(parsed);
    return true;
}

const char* environment_address(const char* variable) {
    const char* value = std::getenv(variable);
    return value != nullptr && value[0] != '\0' ? value : "127.0.0.1";
}

bool copy_address(char* destination, std::size_t capacity, const char* source) {
    const std::size_t length = std::strlen(source);
    if (length >= capacity) {
        return false;
    }
    std::memcpy(destination, source, length + 1U);
    return true;
}

int run_udp(char id, const char* local_text, const char* remote_text) {
    std::uint16_t local_port = 0U;
    std::uint16_t remote_port = 0U;
    if (!parse_port(local_text, local_port) ||
        !parse_port(remote_text, remote_port)) {
        std::fprintf(stderr, "invalid UDP port\n");
        return EXIT_FAILURE;
    }

    const char* local_address = environment_address("SPWKIT_LOCAL_ADDRESS");
    const char* remote_address = environment_address("SPWKIT_REMOTE_ADDRESS");

    spw_udp_config_t udp =
        SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);
    if (!copy_address(udp.local_address, sizeof(udp.local_address), local_address) ||
        !copy_address(udp.remote_address, sizeof(udp.remote_address), remote_address)) {
        std::fprintf(stderr, "UDP address exceeds SpWKit configuration capacity\n");
        return EXIT_FAILURE;
    }
    udp.ack_timeout_ms = 50U;
    udp.max_retries = 5U;
    udp.keepalive_interval_ms = 100U;
    udp.peer_timeout_ms = 500U;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spwkit::Port port;
    if (spwkit::Port::open(config, port) != SPW_OK ||
        port.start() != SPW_OK ||
        !wait_for_run(port)) {
        std::fprintf(stderr, "failed to open/start UDP SpWKit port local=%s:%u remote=%s:%u\n",
                     local_address,
                     static_cast<unsigned>(local_port),
                     remote_address,
                     static_cast<unsigned>(remote_port));
        return EXIT_FAILURE;
    }

    return exchange(port, id, "udp") ? EXIT_SUCCESS : EXIT_FAILURE;
}

int run_device(char id, const char* socket_path) {
    spw_device_config_t device =
        SPW_DEVICE_CONFIG_INITIALIZER(id == 'A' ? 0U : 1U);
    const std::size_t endpoint_length = std::strlen(socket_path);
    if (endpoint_length >= sizeof(device.endpoint)) {
        std::fprintf(stderr, "VSPD socket path is too long\n");
        return EXIT_FAILURE;
    }
    std::memcpy(device.endpoint, socket_path, endpoint_length + 1U);

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    spwkit::Port port;
    if (spwkit::Port::open(config, port) != SPW_OK ||
        port.start() != SPW_OK ||
        !wait_for_run(port)) {
        std::fprintf(stderr, "failed to open/start DEVICE SpWKit port\n");
        return EXIT_FAILURE;
    }

    return exchange(port, id, "device") ? EXIT_SUCCESS : EXIT_FAILURE;
}

void usage(const char* program) {
    std::fprintf(stderr,
                 "usage:\n"
                 "  %s udp A|B LOCAL_PORT REMOTE_PORT\n"
                 "  %s device A|B VSPD_SOCKET\n"
                 "\n"
                 "UDP addresses default to 127.0.0.1 and may be overridden with\n"
                 "SPWKIT_LOCAL_ADDRESS and SPWKIT_REMOTE_ADDRESS.\n",
                 program,
                 program);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 4 || (argv[2][0] != 'A' && argv[2][0] != 'B') ||
        argv[2][1] != '\0') {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char id = argv[2][0];
    if (std::strcmp(argv[1], "udp") == 0 && argc == 5) {
        return run_udp(id, argv[3], argv[4]);
    }
    if (std::strcmp(argv[1], "device") == 0 && argc == 4) {
        return run_device(id, argv[3]);
    }

    usage(argv[0]);
    return EXIT_FAILURE;
}
