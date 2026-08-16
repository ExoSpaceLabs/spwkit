// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <spwkit/udp.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

[[noreturn]] void fixture_fail(const char* operation, spw_result_t result) {
    std::cerr << "[contract][fixture][udp] " << operation
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

spw_port_t* open_endpoint(std::uint16_t local_port,
                          std::uint16_t remote_port,
                          std::uint32_t link_id) {
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);

    /*
     * The shared contract's largest packet is 4 KiB. Keep it in one VSPW-TP
     * datagram here so the common immediate-receive assertions stay strict.
     * Dedicated UDP D2D tests separately verify MTU-scale fragmentation,
     * arbitrary fragment ordering and fault/retry behavior.
     */
    udp.fragment_payload_size = 8192u;
    udp.ack_timeout_ms = 20u;
    udp.max_retries = 3u;
    udp.keepalive_interval_ms = 20u;
    udp.peer_timeout_ms = 80u;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spw_port_t* port = nullptr;
    require_ok("open", spw_port_open(&config, &port));
    require_true(port != nullptr, "open returned null port");
    require_ok("initial reset", spw_port_reset(port));
    return port;
}

class UdpContractFixture final : public spwkit::test::DistributedBackendContractFixture {
public:
    UdpContractFixture()
        : base_port_(static_cast<std::uint16_t>(50000u +
              (static_cast<unsigned>(::getpid()) % 500u) * 2u)) {
        open_pair();
    }

    ~UdpContractFixture() override {
        close_b();
        close_a();
    }

    const char* name() const noexcept override { return "vspw-tp-udp"; }
    spw_port_t* endpoint_a() const noexcept override { return a_; }
    spw_port_t* endpoint_b() const noexcept override { return b_; }

    spw_timeout_us_t transfer_timeout_us() const noexcept override {
        return 500000u;
    }

    spw_timeout_us_t link_transition_timeout_us() const noexcept override {
        return 750000u;
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
        /* Exercise public reset before rebuilding sockets to discard stale UDP. */
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
        b_ = open_endpoint(static_cast<std::uint16_t>(base_port_ + 1u),
                           base_port_, link_id);
        require_ok("restart/start endpoint B", spw_port_start(b_));
    }

private:
    static constexpr std::uint32_t link_id = 0x434f4e54u;

    void open_pair() {
        a_ = open_endpoint(base_port_, static_cast<std::uint16_t>(base_port_ + 1u),
                           link_id);
        b_ = open_endpoint(static_cast<std::uint16_t>(base_port_ + 1u), base_port_,
                           link_id);
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
        } while (Clock::now() < deadline);
        return false;
    }

    std::uint16_t base_port_{0u};
    spw_port_t* a_{nullptr};
    spw_port_t* b_{nullptr};
};

} // namespace

int main() {
    UdpContractFixture fixture;
    const int common_result = spwkit::test::run_backend_contract(fixture);
    if (common_result != EXIT_SUCCESS) {
        return common_result;
    }
    return spwkit::test::run_distributed_backend_contract(fixture);
}
