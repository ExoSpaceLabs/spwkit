// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <spwkit/device.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

[[noreturn]] void fixture_fail(const char* operation, spw_result_t result) {
    std::cerr << "[contract][fixture][device] " << operation
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

spw_port_t* open_endpoint(const char* endpoint, std::uint32_t port_id) {
    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(port_id);
    const std::size_t endpoint_length = std::strlen(endpoint);
    require_true(endpoint_length < sizeof(device.endpoint), "endpoint path too long");
    std::memcpy(device.endpoint, endpoint, endpoint_length + 1u);

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    spw_port_t* port = nullptr;
    require_ok("open", spw_port_open(&config, &port));
    require_true(port != nullptr, "open returned null port");
    require_ok("initial reset", spw_port_reset(port));
    return port;
}

class DeviceContractFixture final
    : public spwkit::test::DistributedBackendContractFixture {
public:
    explicit DeviceContractFixture(const char* endpoint)
        : endpoint_(endpoint) {
        open_pair();
    }

    ~DeviceContractFixture() override {
        close_b();
        close_a();
    }

    const char* name() const noexcept override { return "vspd-device"; }
    spw_port_t* endpoint_a() const noexcept override { return a_; }
    spw_port_t* endpoint_b() const noexcept override { return b_; }

    spw_timeout_us_t transfer_timeout_us() const noexcept override {
        return UINT64_C(2000000);
    }

    spw_timeout_us_t link_transition_timeout_us() const noexcept override {
        return UINT64_C(5000000);
    }

    void start_link() override {
        require_true(a_ != nullptr && b_ != nullptr, "start with missing endpoint");
        require_ok("start endpoint A", spw_port_start(a_));
        require_ok("start endpoint B", spw_port_start(b_));
        require_true(wait_running_pair(), "peer pair did not establish RUN");
    }

    void stop_link() override {
        require_true(a_ != nullptr && b_ != nullptr, "stop with missing endpoint");
        require_ok("stop endpoint A", spw_port_stop(a_));
        require_ok("stop endpoint B", spw_port_stop(b_));
    }

    void reset_link() override {
        if (a_ != nullptr) {
            require_ok("reset endpoint A", spw_port_reset(a_));
        }
        if (b_ != nullptr) {
            require_ok("reset endpoint B", spw_port_reset(b_));
        }
        close_b();
        close_a();
        open_pair();
    }

    void disconnect_endpoint_b() override {
        require_true(b_ != nullptr, "disconnect missing endpoint B");
        close_b();
    }

    void restart_endpoint_b() override {
        require_true(a_ != nullptr && b_ == nullptr, "restart endpoint B state");
        b_ = open_endpoint(endpoint_, 1u);
        require_ok("restart/start endpoint B", spw_port_start(b_));
    }

private:
    void open_pair() {
        a_ = open_endpoint(endpoint_, 0u);
        b_ = open_endpoint(endpoint_, 1u);
    }

    void close_a() noexcept {
        if (a_ != nullptr) {
            (void)spw_port_close(a_);
            a_ = nullptr;
        }
    }

    void close_b() noexcept {
        if (b_ != nullptr) {
            (void)spw_port_close(b_);
            b_ = nullptr;
        }
    }

    bool wait_running_pair() {
        const auto deadline = Clock::now() +
            std::chrono::microseconds(link_transition_timeout_us());
        do {
            spw_link_state_t a_state = SPW_LINK_ERROR_RESET;
            spw_link_state_t b_state = SPW_LINK_ERROR_RESET;
            require_ok("query endpoint A state",
                       spw_port_get_link_state(a_, &a_state));
            require_ok("query endpoint B state",
                       spw_port_get_link_state(b_, &b_state));
            if (a_state == SPW_LINK_RUN && b_state == SPW_LINK_RUN) {
                return true;
            }
            (void)::usleep(1000u);
        } while (Clock::now() < deadline);
        return false;
    }

    const char* endpoint_;
    spw_port_t* a_{nullptr};
    spw_port_t* b_{nullptr};
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " SOCKET\n";
        return EXIT_FAILURE;
    }

    DeviceContractFixture fixture(argv[1]);
    const int common_result = spwkit::test::run_backend_contract(fixture);
    if (common_result != EXIT_SUCCESS) {
        return common_result;
    }
    return spwkit::test::run_distributed_backend_contract(fixture);
}
