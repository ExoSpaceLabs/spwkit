// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace spwkit::test {
namespace {

using Clock = std::chrono::steady_clock;

[[noreturn]] void fail(const char* test, const char* message) {
    std::cerr << "[contract][distributed][FAIL] " << test << ": " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* test, const char* message) {
    if (!condition) {
        fail(test, message);
    }
}

void require_result(spw_result_t actual,
                    spw_result_t expected,
                    const char* test,
                    const char* message) {
    if (actual != expected) {
        std::cerr << "[contract][distributed] expected result " << expected
                  << ", got " << actual << '\n';
        fail(test, message);
    }
}

spw_link_state_t link_state(spw_port_t* port, const char* test) {
    spw_link_state_t state = 0xffu;
    require(port != nullptr, test, "endpoint is null");
    require_result(spw_port_get_link_state(port, &state), SPW_OK, test,
                   "failed to query link state");
    return state;
}

bool wait_for_state(spw_port_t* port,
                    spw_link_state_t expected,
                    spw_timeout_us_t timeout_us,
                    const char* test) {
    const auto deadline = Clock::now() + std::chrono::microseconds(timeout_us);
    do {
        if (link_state(port, test) == expected) {
            return true;
        }
    } while (Clock::now() < deadline);
    return link_state(port, test) == expected;
}

bool wait_for_running_pair(DistributedBackendContractFixture& fixture,
                           spw_timeout_us_t timeout_us,
                           const char* test) {
    const auto deadline = Clock::now() + std::chrono::microseconds(timeout_us);
    do {
        const bool a_running = link_state(fixture.endpoint_a(), test) == SPW_LINK_RUN;
        const bool b_running = link_state(fixture.endpoint_b(), test) == SPW_LINK_RUN;
        if (a_running && b_running) {
            return true;
        }
    } while (Clock::now() < deadline);
    return link_state(fixture.endpoint_a(), test) == SPW_LINK_RUN &&
           link_state(fixture.endpoint_b(), test) == SPW_LINK_RUN;
}

void test_peer_loss_and_restart(DistributedBackendContractFixture& fixture) {
    constexpr const char* test = "peer-loss-restart";

    fixture.reset_link();
    fixture.start_link();
    require(wait_for_running_pair(fixture, fixture.link_transition_timeout_us(), test),
            test, "peer pair did not enter RUN");

    fixture.disconnect_endpoint_b();
    require(wait_for_state(fixture.endpoint_a(), SPW_LINK_ERROR_WAIT,
                           fixture.link_transition_timeout_us(), test),
            test, "surviving endpoint did not report peer loss");

    std::uint8_t unavailable_payload = 0x55u;
    spw_packet_t unavailable{&unavailable_payload, 1u, 1u, SPW_TERMINATOR_EOP};
    require_result(spw_port_send(fixture.endpoint_a(), &unavailable,
                                 SPW_TIMEOUT_IMMEDIATE),
                   SPW_ERR_LINK_UNAVAILABLE, test,
                   "send did not report LINK_UNAVAILABLE after peer loss");

    fixture.restart_endpoint_b();
    require(wait_for_running_pair(fixture, fixture.link_transition_timeout_us(), test),
            test, "peer restart did not recover RUN");

    std::uint8_t tx[] = {0x52u, 0x45u, 0x53u, 0x54u, 0x41u, 0x52u, 0x54u};
    spw_packet_t outgoing{tx, sizeof(tx), sizeof(tx), SPW_TERMINATOR_EOP};
    require_result(spw_port_send(fixture.endpoint_a(), &outgoing,
                                 fixture.transfer_timeout_us()),
                   SPW_OK, test, "send failed after peer restart");

    std::uint8_t rx[sizeof(tx)]{};
    spw_packet_t incoming{rx, 0u, sizeof(rx), SPW_TERMINATOR_EEP};
    require_result(spw_port_receive(fixture.endpoint_b(), &incoming,
                                    fixture.transfer_timeout_us()),
                   SPW_OK, test, "receive failed after peer restart");
    require(incoming.length == sizeof(tx), test, "restart packet length mismatch");
    require(incoming.terminator == SPW_TERMINATOR_EOP, test,
            "restart packet terminator mismatch");
    require(std::memcmp(tx, rx, sizeof(tx)) == 0, test,
            "restart packet payload mismatch");
}

} // namespace

int run_distributed_backend_contract(DistributedBackendContractFixture& fixture) {
    std::cout << "[contract][distributed] backend=" << fixture.name()
              << " transferTimeoutUs=" << fixture.transfer_timeout_us()
              << " transitionTimeoutUs=" << fixture.link_transition_timeout_us()
              << '\n';

    test_peer_loss_and_restart(fixture);

    fixture.reset_link();
    std::cout << "[contract][distributed][PASS] backend=" << fixture.name() << '\n';
    return EXIT_SUCCESS;
}

} // namespace spwkit::test
