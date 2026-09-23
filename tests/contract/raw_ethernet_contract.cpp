// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <spwkit/raw_ethernet.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kFrameCapacity = 1514u;
constexpr std::size_t kQueueDepth = 32u;

struct Frame {
    std::array<std::uint8_t, kFrameCapacity> bytes{};
    std::size_t size{0u};
};

struct Queue {
    std::array<Frame, kQueueDepth> frames{};
    std::size_t head{0u};
    std::size_t count{0u};
};

struct Link {
    std::array<Queue, 2u> inbox{};
};

struct Endpoint {
    Link* link{nullptr};
    std::size_t index{0u};
    bool started{false};
    bool link_up{true};
};

[[noreturn]] void fixture_fail(const char* operation, spw_result_t result) {
    std::cerr << "[contract][fixture][raw-ethernet] " << operation
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

std::size_t other_index(const Endpoint& endpoint) {
    return endpoint.index == 0u ? 1u : 0u;
}

spw_result_t io_start(void* context) {
    auto* endpoint = static_cast<Endpoint*>(context);
    if (endpoint == nullptr || endpoint->link == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->started = true;
    return SPW_OK;
}

spw_result_t io_stop(void* context) {
    auto* endpoint = static_cast<Endpoint*>(context);
    if (endpoint == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->started = false;
    return SPW_OK;
}

spw_result_t io_reset(void* context) {
    auto* endpoint = static_cast<Endpoint*>(context);
    if (endpoint == nullptr || endpoint->link == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->started = false;
    Queue& inbox = endpoint->link->inbox[endpoint->index];
    inbox.head = 0u;
    inbox.count = 0u;
    return SPW_OK;
}

spw_result_t io_send_frame(void* context,
                           const std::uint8_t* frame,
                           std::size_t frame_size,
                           spw_timeout_us_t timeout_us) {
    auto* endpoint = static_cast<Endpoint*>(context);
    (void)timeout_us;
    if (endpoint == nullptr || endpoint->link == nullptr ||
        (frame_size != 0u && frame == nullptr) ||
        frame_size > kFrameCapacity) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint->started) {
        return SPW_ERR_INVALID_STATE;
    }

    Queue& queue = endpoint->link->inbox[other_index(*endpoint)];
    if (queue.count == kQueueDepth) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    const std::size_t slot_index =
        (queue.head + queue.count) % kQueueDepth;
    Frame& slot = queue.frames[slot_index];
    if (frame_size != 0u) {
        std::memcpy(slot.bytes.data(), frame, frame_size);
    }
    slot.size = frame_size;
    ++queue.count;
    return SPW_OK;
}

spw_result_t io_receive_frame(void* context,
                              std::uint8_t* frame,
                              std::size_t frame_capacity,
                              std::size_t* out_frame_size,
                              spw_timeout_us_t timeout_us) {
    auto* endpoint = static_cast<Endpoint*>(context);
    if (endpoint == nullptr || endpoint->link == nullptr ||
        out_frame_size == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = 0u;
    if (!endpoint->started) {
        return SPW_ERR_INVALID_STATE;
    }

    Queue& queue = endpoint->link->inbox[endpoint->index];
    if (queue.count == 0u) {
        if (timeout_us != SPW_TIMEOUT_IMMEDIATE &&
            timeout_us != SPW_TIMEOUT_INFINITE) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(timeout_us));
        }
        return SPW_ERR_TIMEOUT;
    }

    Frame& slot = queue.frames[queue.head];
    *out_frame_size = slot.size;
    if (frame_capacity < slot.size ||
        (slot.size != 0u && frame == nullptr)) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (slot.size != 0u) {
        std::memcpy(frame, slot.bytes.data(), slot.size);
    }
    queue.head = (queue.head + 1u) % kQueueDepth;
    --queue.count;
    return SPW_OK;
}

spw_result_t io_wait(void* context,
                     spw_raw_ethernet_ready_t interests,
                     spw_timeout_us_t timeout_us,
                     spw_raw_ethernet_ready_t* out_ready) {
    auto* endpoint = static_cast<Endpoint*>(context);
    if (endpoint == nullptr || endpoint->link == nullptr ||
        out_ready == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint->started) {
        *out_ready = SPW_RAW_ETHERNET_READY_NONE;
        return SPW_ERR_INVALID_STATE;
    }

    spw_raw_ethernet_ready_t ready = SPW_RAW_ETHERNET_READY_NONE;
    if ((interests & SPW_RAW_ETHERNET_READY_RX) != 0u &&
        endpoint->link->inbox[endpoint->index].count != 0u) {
        ready |= SPW_RAW_ETHERNET_READY_RX;
    }
    if ((interests & SPW_RAW_ETHERNET_READY_TX) != 0u &&
        endpoint->link->inbox[other_index(*endpoint)].count < kQueueDepth) {
        ready |= SPW_RAW_ETHERNET_READY_TX;
    }
    if (ready == SPW_RAW_ETHERNET_READY_NONE &&
        timeout_us != SPW_TIMEOUT_IMMEDIATE &&
        timeout_us != SPW_TIMEOUT_INFINITE) {
        std::this_thread::sleep_for(std::chrono::microseconds(timeout_us));
    }
    *out_ready = ready;
    return ready == SPW_RAW_ETHERNET_READY_NONE
               ? SPW_ERR_TIMEOUT
               : SPW_OK;
}

spw_result_t io_get_max_frame_size(const void* context,
                                   std::size_t* out_frame_size) {
    (void)context;
    if (out_frame_size == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = kFrameCapacity;
    return SPW_OK;
}

spw_result_t io_get_link_up(const void* context, bool* out_link_up) {
    const auto* endpoint = static_cast<const Endpoint*>(context);
    if (endpoint == nullptr || out_link_up == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_link_up = endpoint->link_up;
    return SPW_OK;
}

std::uint64_t runtime_now_us(const void* context) {
    (void)context;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
}

spw_result_t runtime_delay_us(void* context,
                              std::uint64_t delay_us,
                              spw_timeout_us_t timeout_us) {
    (void)context;
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    if (delay_us != 0u) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
    return SPW_OK;
}

const spw_raw_ethernet_io_ops_t kIoOps = {
    sizeof(spw_raw_ethernet_io_ops_t),
    SPW_RAW_ETHERNET_IO_OPS_VERSION,
    io_start,
    io_stop,
    io_reset,
    io_send_frame,
    io_receive_frame,
    io_wait,
    io_get_max_frame_size,
    io_get_link_up
};

const spw_runtime_ops_t kRuntimeOps = {
    sizeof(spw_runtime_ops_t),
    SPW_RUNTIME_OPS_VERSION,
    runtime_now_us,
    runtime_delay_us
};

spw_port_t* open_endpoint(
    Endpoint* endpoint,
    const std::array<std::uint8_t, SPW_RAW_ETHERNET_MAC_SIZE>& local_mac,
    const std::array<std::uint8_t, SPW_RAW_ETHERNET_MAC_SIZE>& remote_mac,
    std::uint32_t link_id) {
    spw_raw_ethernet_config_t raw =
        SPW_RAW_ETHERNET_CONFIG_INITIALIZER(
            &kIoOps, endpoint, &kRuntimeOps, nullptr, link_id);
    std::memcpy(raw.local_mac, local_mac.data(), local_mac.size());
    std::memcpy(raw.remote_mac, remote_mac.data(), remote_mac.size());
    raw.ether_type = SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE;
    raw.fragment_payload_size = 1400u;
    raw.ack_timeout_ms = 20u;
    raw.max_retries = 3u;
    raw.keepalive_interval_ms = 20u;
    raw.peer_timeout_ms = 80u;

    spw_port_config_t config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    config.backend_config = &raw;
    config.backend_config_size = sizeof(raw);

    spw_port_t* port = nullptr;
    require_ok("open", spw_port_open(&config, &port));
    require_true(port != nullptr, "open returned null port");
    require_ok("initial reset", spw_port_reset(port));
    return port;
}

class RawEthernetContractFixture final
    : public spwkit::test::BackendContractFixture {
public:
    RawEthernetContractFixture() {
        endpoint_a_.link = &link_;
        endpoint_a_.index = 0u;
        endpoint_b_.link = &link_;
        endpoint_b_.index = 1u;
        open_pair();
    }

    ~RawEthernetContractFixture() override {
        close_port(b_);
        close_port(a_);
    }

    const char* name() const noexcept override {
        return "vspw-tp-raw-ethernet";
    }

    spw_port_t* endpoint_a() const noexcept override { return a_; }
    spw_port_t* endpoint_b() const noexcept override { return b_; }

    spw_timeout_us_t transfer_timeout_us() const noexcept override {
        return 500000u;
    }

    void start_link() override {
        require_true(a_ != nullptr && b_ != nullptr,
                     "start with missing endpoint");
        require_ok("start endpoint A", spw_port_start(a_));
        require_ok("start endpoint B", spw_port_start(b_));
        require_true(wait_running_pair(), "peer pair did not establish RUN");
    }

    void stop_link() override {
        require_true(a_ != nullptr && b_ != nullptr,
                     "stop with missing endpoint");
        require_ok("stop endpoint A", spw_port_stop(a_));
        require_ok("stop endpoint B", spw_port_stop(b_));
    }

    void reset_link() override {
        require_true(a_ != nullptr && b_ != nullptr,
                     "reset with missing endpoint");
        require_ok("reset endpoint A", spw_port_reset(a_));
        require_ok("reset endpoint B", spw_port_reset(b_));
    }

private:
    static constexpr std::uint32_t kLinkId = UINT32_C(0x52415743);
    const std::array<std::uint8_t, SPW_RAW_ETHERNET_MAC_SIZE> mac_a_{
        0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0xa1u};
    const std::array<std::uint8_t, SPW_RAW_ETHERNET_MAC_SIZE> mac_b_{
        0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0xb2u};

    void open_pair() {
        a_ = open_endpoint(&endpoint_a_, mac_a_, mac_b_, kLinkId);
        b_ = open_endpoint(&endpoint_b_, mac_b_, mac_a_, kLinkId);
    }

    static void close_port(spw_port_t*& port) noexcept {
        if (port != nullptr) {
            (void)spw_port_close(port);
            port = nullptr;
        }
    }

    bool wait_running_pair() {
        const auto deadline =
            Clock::now() + std::chrono::milliseconds(250);
        do {
            spw_link_state_t state_a = SPW_LINK_ERROR_RESET;
            spw_link_state_t state_b = SPW_LINK_ERROR_RESET;
            require_ok("query endpoint A state",
                       spw_port_get_link_state(a_, &state_a));
            require_ok("query endpoint B state",
                       spw_port_get_link_state(b_, &state_b));
            if (state_a == SPW_LINK_RUN && state_b == SPW_LINK_RUN) {
                return true;
            }
        } while (Clock::now() < deadline);
        return false;
    }

    Link link_{};
    Endpoint endpoint_a_{};
    Endpoint endpoint_b_{};
    spw_port_t* a_{nullptr};
    spw_port_t* b_{nullptr};
};

} // namespace

int main() {
    RawEthernetContractFixture fixture;
    return spwkit::test::run_backend_contract(fixture);
}
