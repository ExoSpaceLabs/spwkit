// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fixture_fail(const char* operation, spw_result_t result) {
    std::cerr << "[contract][fixture][loopback] " << operation
              << " failed with result " << result << '\n';
    std::exit(EXIT_FAILURE);
}

void require_ok(const char* operation, spw_result_t result) {
    if (result != SPW_OK) {
        fixture_fail(operation, result);
    }
}

class LoopbackContractFixture final : public spwkit::test::BackendContractFixture {
public:
    LoopbackContractFixture() {
        spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
        require_ok("open", spw_port_open(&config, &port_));
        if (port_ == nullptr) {
            fixture_fail("open returned null port", SPW_ERR_BACKEND);
        }
    }

    ~LoopbackContractFixture() override {
        if (port_ != nullptr) {
            (void)spw_port_close(port_);
        }
    }

    const char* name() const noexcept override { return "loopback"; }
    spw_port_t* endpoint_a() const noexcept override { return port_; }
    spw_port_t* endpoint_b() const noexcept override { return port_; }

    void start_link() override {
        require_ok("start", spw_port_start(port_));
    }

    void stop_link() override {
        require_ok("stop", spw_port_stop(port_));
    }

    void reset_link() override {
        require_ok("reset", spw_port_reset(port_));
    }

private:
    spw_port_t* port_{nullptr};
};

} // namespace

int main() {
    LoopbackContractFixture fixture;
    return spwkit::test::run_backend_contract(fixture);
}
