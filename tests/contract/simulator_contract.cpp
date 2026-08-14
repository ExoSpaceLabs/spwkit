// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

[[noreturn]] void fixture_fail(const char* operation, spw_result_t result) {
    std::cerr << "[contract][fixture][simulator] " << operation
              << " failed with result " << result << '\n';
    std::exit(EXIT_FAILURE);
}

void require_ok(const char* operation, spw_result_t result) {
    if (result != SPW_OK) {
        fixture_fail(operation, result);
    }
}

void require_true(bool condition, const char* operation) {
    if (!condition) {
        fixture_fail(operation, SPW_ERR_BACKEND);
    }
}

spw_port_t* open_endpoint(std::uint64_t link_id, spw_simulator_endpoint_t endpoint) {
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = link_id;
    simulator.endpoint = endpoint;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);

    spw_port_t* port = nullptr;
    require_ok("open", spw_port_open(&config, &port));
    if (port == nullptr) {
        fixture_fail("open returned null port", SPW_ERR_BACKEND);
    }
    return port;
}

class SimulatorContractFixture final : public spwkit::test::BackendContractFixture {
public:
    SimulatorContractFixture()
        : a_(open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_A)),
          b_(open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B)) {}

    ~SimulatorContractFixture() override {
        if (b_ != nullptr) {
            (void)spw_port_close(b_);
        }
        if (a_ != nullptr) {
            (void)spw_port_close(a_);
        }
    }

    const char* name() const noexcept override { return "simulator-local-peer"; }
    spw_port_t* endpoint_a() const noexcept override { return a_; }
    spw_port_t* endpoint_b() const noexcept override { return b_; }

    void start_link() override {
        require_ok("start endpoint A", spw_port_start(a_));
        require_ok("start endpoint B", spw_port_start(b_));
    }

    void stop_link() override {
        require_ok("stop endpoint A", spw_port_stop(a_));
        require_ok("stop endpoint B", spw_port_stop(b_));
    }

    void reset_link() override {
        require_ok("reset endpoint A", spw_port_reset(a_));
        require_ok("reset endpoint B", spw_port_reset(b_));
    }

    bool has_zero_copy_contract() const noexcept override { return true; }

    void run_zero_copy_contract() override {
        reset_link();
        start_link();

        spw_capabilities_t caps{};
        require_ok("zero-copy capabilities", spw_port_get_capabilities(a_, &caps));
        require_true((caps.bits & SPW_CAP_ZERO_COPY) != 0u,
                     "ZERO_COPY capability missing");

        spw_buffer_t* tx = nullptr;
        require_ok("acquire TX", spw_port_acquire_tx_buffer(
                                    a_, 64u, SPW_TIMEOUT_IMMEDIATE, &tx));
        require_true(tx != nullptr, "acquire TX returned null");

        spw_buffer_view_t tx_view{};
        require_ok("view TX", spw_buffer_get_view(tx, &tx_view));
        require_true(tx_view.capacity >= 64u, "TX capacity below request");
        require_true(tx_view.data != nullptr, "TX data is null");
        if (caps.buffer_alignment > 1u) {
            require_true((reinterpret_cast<std::uintptr_t>(tx_view.data) %
                          caps.buffer_alignment) == 0u,
                         "TX buffer alignment mismatch");
        }

        constexpr std::array<std::uint8_t, 7> payload{
            0x53u, 0x50u, 0x57u, 0x2du, 0x5au, 0x43u, 0x31u};
        std::memcpy(tx_view.data, payload.data(), payload.size());
        require_ok("set TX packet",
                   spw_buffer_set_packet(tx, payload.size(), SPW_TERMINATOR_EEP));

        require_ok("submit TX",
                   spw_port_submit_tx_buffer(a_, &tx, SPW_TIMEOUT_IMMEDIATE));
        require_true(tx == nullptr, "submit did not transfer TX ownership");

        spw_buffer_t* reclaimed = nullptr;
        require_ok("reclaim TX",
                   spw_port_reclaim_tx_buffer(a_, SPW_TIMEOUT_IMMEDIATE, &reclaimed));
        require_true(reclaimed != nullptr, "reclaim returned null TX buffer");
        require_ok("release TX", spw_port_release_tx_buffer(a_, &reclaimed));
        require_true(reclaimed == nullptr, "release did not transfer TX ownership");

        spw_buffer_t* rx = nullptr;
        require_ok("acquire RX",
                   spw_port_acquire_rx_buffer(b_, SPW_TIMEOUT_IMMEDIATE, &rx));
        require_true(rx != nullptr, "acquire RX returned null");

        spw_buffer_view_t rx_view{};
        require_ok("view RX", spw_buffer_get_view(rx, &rx_view));
        require_true(rx_view.length == payload.size(), "RX length mismatch");
        require_true(rx_view.terminator == SPW_TERMINATOR_EEP,
                     "RX terminator mismatch");
        require_true(std::memcmp(rx_view.data, payload.data(), payload.size()) == 0,
                     "RX payload mismatch");
        require_ok("release RX", spw_port_release_rx_buffer(b_, &rx));
        require_true(rx == nullptr, "release did not transfer RX ownership");

        /* A handle belongs to the backend/port that acquired it. */
        require_ok("acquire owner-test TX", spw_port_acquire_tx_buffer(
                                               a_, 1u, SPW_TIMEOUT_IMMEDIATE, &tx));
        require_true(spw_port_submit_tx_buffer(b_, &tx, SPW_TIMEOUT_IMMEDIATE) ==
                         SPW_ERR_INVALID_STATE,
                     "foreign-port TX submit was accepted");
        require_true(tx != nullptr, "failed submit stole TX ownership");
        require_ok("release owner-test TX", spw_port_release_tx_buffer(a_, &tx));

        /* Exhaust the backend-owned TX pool deterministically. */
        constexpr std::size_t max_test_buffers = 32u;
        require_true(caps.tx_queue_depth > 0u && caps.tx_queue_depth <= max_test_buffers,
                     "TX queue depth outside contract test bound");
        std::array<spw_buffer_t*, max_test_buffers> held{};
        for (std::size_t i = 0; i < caps.tx_queue_depth; ++i) {
            require_ok("acquire TX pool", spw_port_acquire_tx_buffer(
                                              a_, 1u, SPW_TIMEOUT_IMMEDIATE, &held[i]));
        }
        spw_buffer_t* extra = nullptr;
        require_true(spw_port_acquire_tx_buffer(
                         a_, 1u, SPW_TIMEOUT_IMMEDIATE, &extra) ==
                         SPW_ERR_RESOURCE_EXHAUSTED,
                     "exhausted TX pool did not report RESOURCE_EXHAUSTED");
        require_true(extra == nullptr, "failed acquire returned a buffer");
        for (std::size_t i = 0; i < caps.tx_queue_depth; ++i) {
            require_ok("release TX pool", spw_port_release_tx_buffer(a_, &held[i]));
        }

        /* Mandatory copied I/O remains valid after zero-copy use. */
        std::uint8_t copied_tx[] = {1u, 2u, 3u};
        spw_packet_t copied_packet{copied_tx, sizeof(copied_tx), sizeof(copied_tx),
                                   SPW_TERMINATOR_EOP};
        require_ok("copied send after zero-copy",
                   spw_port_send(a_, &copied_packet, SPW_TIMEOUT_IMMEDIATE));
        std::uint8_t copied_rx[sizeof(copied_tx)]{};
        spw_packet_t copied_receive{copied_rx, 0u, sizeof(copied_rx),
                                    SPW_TERMINATOR_EOP};
        require_ok("copied receive after zero-copy",
                   spw_port_receive(b_, &copied_receive, SPW_TIMEOUT_IMMEDIATE));
        require_true(std::memcmp(copied_tx, copied_rx, sizeof(copied_tx)) == 0,
                     "copied path failed after zero-copy use");
    }

private:
    static constexpr std::uint64_t link_id = 0x434f4e5452414354ull;
    spw_port_t* a_{nullptr};
    spw_port_t* b_{nullptr};
};

} // namespace

int main() {
    SimulatorContractFixture fixture;
    return spwkit::test::run_backend_contract(fixture);
}
