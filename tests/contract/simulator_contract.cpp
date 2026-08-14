// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <cstdlib>
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
