// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

enum class Scenario {
    single,
    initial,
    survivor,
    restart,
};

struct Options {
    std::array<char, SPW_UDP_ADDRESS_MAX> local_address{};
    std::array<char, SPW_UDP_ADDRESS_MAX> remote_address{};
    std::uint16_t local_port{};
    std::uint16_t remote_port{};
    std::uint32_t link_id{42u};
    char id{};
    Scenario scenario{Scenario::single};
};

constexpr spwkit::Timeout io_timeout_us = UINT64_C(5000000);
constexpr unsigned state_timeout_ms = 15000u;
constexpr std::size_t payload_size = 8192u;

void usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s --id A|B --local-port PORT --remote-port PORT [options]\n"
                 "\n"
                 "options:\n"
                 "  --local-address IPv4    default 127.0.0.1\n"
                 "  --remote-address IPv4   default 127.0.0.1\n"
                 "  --link-id ID            default 42\n"
                 "  --scenario NAME         single|initial|survivor|restart\n",
                 program);
}

bool parse_u32(const char* text, std::uint32_t& value) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 0);
    if (text[0] == '\0' || end == nullptr || *end != '\0' || parsed > UINT32_MAX) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_port(const char* text, std::uint16_t& value) {
    std::uint32_t parsed = 0u;
    if (!parse_u32(text, parsed) || parsed == 0u || parsed > UINT16_MAX) {
        return false;
    }
    value = static_cast<std::uint16_t>(parsed);
    return true;
}

bool parse_scenario(const char* text, Scenario& value) {
    if (std::strcmp(text, "single") == 0) {
        value = Scenario::single;
    } else if (std::strcmp(text, "initial") == 0) {
        value = Scenario::initial;
    } else if (std::strcmp(text, "survivor") == 0) {
        value = Scenario::survivor;
    } else if (std::strcmp(text, "restart") == 0) {
        value = Scenario::restart;
    } else {
        return false;
    }
    return true;
}

const char* scenario_name(Scenario scenario) {
    switch (scenario) {
    case Scenario::single:
        return "single";
    case Scenario::initial:
        return "initial";
    case Scenario::survivor:
        return "survivor";
    case Scenario::restart:
        return "restart";
    }
    return "unknown";
}

bool copy_address(std::array<char, SPW_UDP_ADDRESS_MAX>& destination,
                  const char* source) {
    return std::snprintf(destination.data(), destination.size(), "%s", source) <
           static_cast<int>(destination.size());
}

bool parse_options(int argc, char** argv, Options& options) {
    if (!copy_address(options.local_address, "127.0.0.1") ||
        !copy_address(options.remote_address, "127.0.0.1")) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            options.id = argv[++i][0];
            if ((options.id != 'A' && options.id != 'B') || argv[i][1] != '\0') {
                return false;
            }
        } else if (std::strcmp(argv[i], "--local-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], options.local_port)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--remote-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], options.remote_port)) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--link-id") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], options.link_id) || options.link_id == 0u) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--local-address") == 0 && i + 1 < argc) {
            if (!copy_address(options.local_address, argv[++i])) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--remote-address") == 0 && i + 1 < argc) {
            if (!copy_address(options.remote_address, argv[++i])) {
                return false;
            }
        } else if (std::strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            if (!parse_scenario(argv[++i], options.scenario)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return options.id != 0 && options.local_port != 0u && options.remote_port != 0u;
}

bool wait_for_state(spwkit::Port& port,
                    spw_link_state_t expected,
                    unsigned timeout_ms) {
    const unsigned attempts = timeout_ms / 10u + 1u;
    for (unsigned i = 0u; i < attempts; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        const spwkit::Result result = port.link_state(state);
        if (result != SPW_OK) {
            std::fprintf(stderr, "get_link_state failed: %d\n", static_cast<int>(result));
            return false;
        }
        if (state == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::uint8_t pattern_byte(char id, unsigned round, std::size_t index) {
    const unsigned base = id == 'A' ? 0x31u : 0x97u;
    return static_cast<std::uint8_t>(
        (base + round * 17u + static_cast<unsigned>(index * 13u)) & 0xffu);
}

void fill_payload(std::array<std::uint8_t, payload_size>& payload,
                  char id,
                  unsigned round) {
    for (std::size_t i = 0u; i < payload.size(); ++i) {
        payload[i] = pattern_byte(id, round, i);
    }
}

bool verify_payload(const std::array<std::uint8_t, payload_size>& payload,
                    char id,
                    unsigned round) {
    for (std::size_t i = 0u; i < payload.size(); ++i) {
        if (payload[i] != pattern_byte(id, round, i)) {
            std::fprintf(stderr,
                         "payload mismatch id=%c round=%u offset=%zu expected=%u actual=%u\n",
                         id,
                         round,
                         i,
                         static_cast<unsigned>(pattern_byte(id, round, i)),
                         static_cast<unsigned>(payload[i]));
            return false;
        }
    }
    return true;
}

bool exchange_round(spwkit::Port& port, char id, unsigned round) {
    const char peer_id = id == 'A' ? 'B' : 'A';
    const spw_terminator_t local_terminator =
        id == 'A' ? SPW_TERMINATOR_EOP : SPW_TERMINATOR_EEP;
    const spw_terminator_t peer_terminator =
        peer_id == 'A' ? SPW_TERMINATOR_EOP : SPW_TERMINATOR_EEP;

    std::array<std::uint8_t, payload_size> tx_payload{};
    std::array<std::uint8_t, payload_size> rx_payload{};
    fill_payload(tx_payload, id, round);

    spw_packet_t tx{
        tx_payload.data(), tx_payload.size(), tx_payload.size(), local_terminator};
    spwkit::Result result = port.send(tx, io_timeout_us);
    if (result != SPW_OK) {
        std::fprintf(stderr,
                     "round %u packet send failed: %d\n",
                     round,
                     static_cast<int>(result));
        return false;
    }

    spw_packet_t rx{
        rx_payload.data(), 0u, rx_payload.size(), SPW_TERMINATOR_EOP};
    result = port.receive(rx, io_timeout_us);
    if (result != SPW_OK) {
        std::fprintf(stderr,
                     "round %u packet receive failed: %d\n",
                     round,
                     static_cast<int>(result));
        return false;
    }
    if (rx.length != payload_size || rx.terminator != peer_terminator ||
        !verify_payload(rx_payload, peer_id, round)) {
        std::fprintf(stderr, "round %u received packet metadata/content mismatch\n", round);
        return false;
    }

    const spw_time_code_t tx_time{
        static_cast<std::uint8_t>(
            (round * 10u + (id == 'A' ? 1u : 2u)) & 0x3fu),
        0u,
    };
    result = port.send_time_code(tx_time, io_timeout_us);
    if (result != SPW_OK) {
        std::fprintf(stderr,
                     "round %u time-code send failed: %d\n",
                     round,
                     static_cast<int>(result));
        return false;
    }

    spw_time_code_t rx_time{0u, 0u};
    result = port.receive_time_code(rx_time, io_timeout_us);
    if (result != SPW_OK) {
        std::fprintf(stderr,
                     "round %u time-code receive failed: %d\n",
                     round,
                     static_cast<int>(result));
        return false;
    }
    const std::uint8_t expected_time = static_cast<std::uint8_t>(
        (round * 10u + (peer_id == 'A' ? 1u : 2u)) & 0x3fu);
    if (rx_time.time_count != expected_time || rx_time.control_flags != 0u) {
        std::fprintf(stderr, "round %u time-code mismatch\n", round);
        return false;
    }

    std::printf("ROUND %u OK id=%c bytes=%zu terminator=%s time=%u\n",
                round,
                id,
                payload_size,
                local_terminator == SPW_TERMINATOR_EEP ? "EEP" : "EOP",
                static_cast<unsigned>(tx_time.time_count));
    return true;
}

bool run_scenario(spwkit::Port& port, const Options& options) {
    if (!wait_for_state(port, SPW_LINK_RUN, state_timeout_ms)) {
        std::fprintf(stderr, "peer did not reach RUN\n");
        return false;
    }

    if (options.scenario == Scenario::restart) {
        return exchange_round(port, options.id, 2u);
    }

    if (!exchange_round(port, options.id, 1u)) {
        return false;
    }

    if (options.scenario != Scenario::survivor) {
        return true;
    }

    if (!wait_for_state(port, SPW_LINK_ERROR_WAIT, state_timeout_ms)) {
        std::fprintf(stderr, "survivor did not observe peer loss\n");
        return false;
    }
    std::printf("PEER_LOST id=%c\n", options.id);

    if (!wait_for_state(port, SPW_LINK_RUN, state_timeout_ms)) {
        std::fprintf(stderr, "survivor did not recover peer RUN state\n");
        return false;
    }
    std::printf("PEER_RECOVERED id=%c\n", options.id);

    return exchange_round(port, options.id, 2u);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    Options options;
    if (!parse_options(argc, argv, options)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(
        options.local_port, options.remote_port, options.link_id);
    std::snprintf(udp.local_address,
                  sizeof(udp.local_address),
                  "%s",
                  options.local_address.data());
    std::snprintf(udp.remote_address,
                  sizeof(udp.remote_address),
                  "%s",
                  options.remote_address.data());
    udp.ack_timeout_ms = 50u;
    udp.max_retries = 5u;
    udp.keepalive_interval_ms = 100u;
    udp.peer_timeout_ms = 500u;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spwkit::Port port;
    spwkit::Result result = spwkit::Port::open(config, port);
    if (result != SPW_OK || !port.valid()) {
        std::fprintf(stderr, "spwkit::Port::open failed: %d\n", static_cast<int>(result));
        return EXIT_FAILURE;
    }

    result = port.start();
    if (result != SPW_OK) {
        std::fprintf(stderr, "port.start failed: %d\n", static_cast<int>(result));
        return EXIT_FAILURE;
    }

    std::printf("START id=%c scenario=%s local=%s:%u remote=%s:%u link=%u api=cpp\n",
                options.id,
                scenario_name(options.scenario),
                options.local_address.data(),
                static_cast<unsigned>(options.local_port),
                options.remote_address.data(),
                static_cast<unsigned>(options.remote_port),
                static_cast<unsigned>(options.link_id));

    const bool scenario_ok = run_scenario(port, options);

    spw_statistics_t statistics{};
    result = port.statistics(statistics);
    if (result == SPW_OK) {
        std::printf("STATS id=%c tx=%llu rx=%llu tx_time=%llu rx_time=%llu link_errors=%llu\n",
                    options.id,
                    static_cast<unsigned long long>(statistics.tx_packets),
                    static_cast<unsigned long long>(statistics.rx_packets),
                    static_cast<unsigned long long>(statistics.tx_time_codes),
                    static_cast<unsigned long long>(statistics.rx_time_codes),
                    static_cast<unsigned long long>(statistics.link_errors));
    }

    if (!scenario_ok) {
        return EXIT_FAILURE;
    }

    std::printf("PASS id=%c scenario=%s\n", options.id, scenario_name(options.scenario));
    return EXIT_SUCCESS;
}
