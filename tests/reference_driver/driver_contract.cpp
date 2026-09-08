// SPDX-License-Identifier: Apache-2.0

#include "../contract/contract_suite.hpp"
#include "reference_driver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

[[noreturn]] void fail(const char* message, spw_result_t result = SPW_OK) {
    std::cerr << "[reference-driver][FAIL] " << message;
    if (result != SPW_OK) {
        std::cerr << " result=" << result;
    }
    std::cerr << '\n';
    std::exit(EXIT_FAILURE);
}

void require_result(spw_result_t result, const char* message) {
    if (result != SPW_OK) {
        fail(message, result);
    }
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

class ReferenceDriverFixture final : public spwkit::test::BackendContractFixture {
public:
    ReferenceDriverFixture() {
        spw_reference_link_init(&link_);
        open_endpoint(0u, &port_a_, workspace_a_);
        open_endpoint(1u, &port_b_, workspace_b_);
    }

    ~ReferenceDriverFixture() override {
        if (port_a_ != nullptr) {
            (void)spw_port_close(port_a_);
        }
        if (port_b_ != nullptr) {
            (void)spw_port_close(port_b_);
        }
    }

    const char* name() const noexcept override {
        return "driver-reference";
    }

    spw_port_t* endpoint_a() const noexcept override {
        return port_a_;
    }

    spw_port_t* endpoint_b() const noexcept override {
        return port_b_;
    }

    void start_link() override {
        require_result(spw_port_start(port_a_), "start endpoint A");
        require_result(spw_port_start(port_b_), "start endpoint B");
    }

    void stop_link() override {
        require_result(spw_port_stop(port_a_), "stop endpoint A");
        require_result(spw_port_stop(port_b_), "stop endpoint B");
    }

    void reset_link() override {
        require_result(spw_port_reset(port_a_), "reset endpoint A");
        require_result(spw_port_reset(port_b_), "reset endpoint B");
    }

    bool has_zero_copy_contract() const noexcept override {
        return true;
    }

    void run_zero_copy_contract() override {
        reset_link();
        start_link();

        auto* endpoint_a = spw_reference_link_endpoint(&link_, 0u);
        auto* endpoint_b = spw_reference_link_endpoint(&link_, 1u);
        require(endpoint_a != nullptr && endpoint_b != nullptr,
                "reference endpoints unavailable");

        spw_buffer_t* tx = nullptr;
        require_result(spw_port_acquire_tx_buffer(port_a_, 32u,
                                                  SPW_TIMEOUT_IMMEDIATE, &tx),
                       "acquire TX buffer");
        require(tx != nullptr, "TX acquisition returned null");

        spw_buffer_view_t tx_view{};
        require_result(spw_buffer_get_view(tx, &tx_view), "get TX buffer view");
        require(spw_reference_owns_tx_pointer(endpoint_a, tx_view.data),
                "TX view is not driver-owned storage");
        require(tx_view.capacity >= 4u, "TX buffer capacity too small");

        const std::array<std::uint8_t, 4> payload{0xdeu, 0xadu, 0xbeu, 0xefu};
        std::memcpy(tx_view.data, payload.data(), payload.size());
        auto* submitted_pointer = tx_view.data;
        require_result(spw_buffer_set_packet(tx, payload.size(),
                                             SPW_TERMINATOR_EEP),
                       "set TX packet metadata");
        require_result(spw_port_submit_tx_buffer(port_a_, &tx,
                                                 SPW_TIMEOUT_IMMEDIATE),
                       "submit TX buffer");
        require(tx == nullptr, "successful TX submit retained application handle");
        require(endpoint_a->sync_to_device_count != 0u,
                "TO_DEVICE coherency hook did not run");

        spw_buffer_t* rx = nullptr;
        require_result(spw_port_acquire_rx_buffer(port_b_,
                                                  SPW_TIMEOUT_IMMEDIATE, &rx),
                       "acquire RX buffer");
        require(rx != nullptr, "RX acquisition returned null");

        spw_buffer_view_t rx_view{};
        require_result(spw_buffer_get_view(rx, &rx_view), "get RX buffer view");
        require(spw_reference_owns_rx_pointer(endpoint_b, rx_view.data),
                "RX view is not driver-owned storage");
        require(rx_view.length == payload.size(), "RX length mismatch");
        require(rx_view.terminator == SPW_TERMINATOR_EEP,
                "RX terminator mismatch");
        require(std::memcmp(rx_view.data, payload.data(), payload.size()) == 0,
                "RX payload mismatch");
        require(endpoint_b->sync_from_device_count != 0u,
                "FROM_DEVICE coherency hook did not run");
        require_result(spw_port_release_rx_buffer(port_b_, &rx),
                       "release RX buffer");
        require(rx == nullptr, "successful RX release retained application handle");

        spw_buffer_t* reclaimed = nullptr;
        require_result(spw_port_reclaim_tx_buffer(port_a_,
                                                  SPW_TIMEOUT_IMMEDIATE,
                                                  &reclaimed),
                       "reclaim TX buffer");
        require(reclaimed != nullptr, "TX reclaim returned null");
        spw_buffer_view_t reclaimed_view{};
        require_result(spw_buffer_get_view(reclaimed, &reclaimed_view),
                       "get reclaimed TX view");
        require(reclaimed_view.data == submitted_pointer,
                "reclaimed TX buffer changed backing storage");
        require_result(spw_port_release_tx_buffer(port_a_, &reclaimed),
                       "release reclaimed TX buffer");
        require(reclaimed == nullptr,
                "successful TX release retained application handle");
    }

private:
    static constexpr std::size_t workspace_size = 16384u;
    using Workspace = std::array<unsigned char, workspace_size>;

    void open_endpoint(unsigned index,
                       spw_port_t** out_port,
                       Workspace& workspace) {
        auto* endpoint = spw_reference_link_endpoint(&link_, index);
        require(endpoint != nullptr, "invalid reference endpoint index");

        driver_configs_[index] =
            SPW_DRIVER_CONFIG_INITIALIZER(spw_reference_driver_ops(), endpoint);
        driver_configs_[index].tx_buffer_slots = SPW_REFERENCE_DMA_SLOTS;
        driver_configs_[index].rx_buffer_slots = SPW_REFERENCE_DMA_SLOTS;

        spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
        config.backend_config = &driver_configs_[index];
        config.backend_config_size = sizeof(driver_configs_[index]);

        spw_port_workspace_requirements_t requirements{};
        require_result(spw_port_workspace_requirements(&config, &requirements),
                       "query driver workspace requirements");
        require(requirements.size <= workspace.size(),
                "reference driver workspace is too small");
        require(reinterpret_cast<std::uintptr_t>(workspace.data()) %
                    requirements.alignment == 0u,
                "reference driver workspace alignment is insufficient");
        require_result(spw_port_open_in_place(&config, workspace.data(),
                                              workspace.size(), out_port),
                       "open reference driver endpoint");
        require(*out_port != nullptr, "reference driver open returned null port");
    }

    spw_reference_link_t link_{};
    spw_driver_config_t driver_configs_[2]{};
    alignas(std::max_align_t) Workspace workspace_a_{};
    alignas(std::max_align_t) Workspace workspace_b_{};
    spw_port_t* port_a_{nullptr};
    spw_port_t* port_b_{nullptr};
};

} // namespace

int main() {
    ReferenceDriverFixture fixture;
    return spwkit::test::run_backend_contract(fixture);
}
