// SPDX-License-Identifier: Apache-2.0

#include "backends/virtual/simulator_backend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>

namespace spwkit::detail {
namespace {

struct PacketSlot {
    std::array<std::uint8_t, SimulatorBackend::max_packet_size> data{};
    std::size_t length{0};
    spw_terminator_t terminator{SPW_TERMINATOR_EOP};
};

struct PacketQueue {
    std::array<PacketSlot, SimulatorBackend::packet_queue_depth> slots{};
    std::size_t head{0};
    std::size_t tail{0};
    std::size_t count{0};
};

struct TimeCodeQueue {
    std::array<spw_time_code_t, SimulatorBackend::time_code_queue_depth> slots{};
    std::size_t head{0};
    std::size_t tail{0};
    std::size_t count{0};
};

struct EndpointState {
    bool attached{false};
    bool started{false};
    spw_link_state_t state{SPW_LINK_ERROR_RESET};
    PacketQueue packets{};
    TimeCodeQueue time_codes{};
    spw_statistics_t statistics{};
};

void reset_endpoint(EndpointState& endpoint) noexcept {
    endpoint = EndpointState{};
}

void clear_receive_queues(EndpointState& endpoint) noexcept {
    endpoint.statistics.dropped_packets += endpoint.packets.count;
    endpoint.packets = PacketQueue{};
    endpoint.time_codes = TimeCodeQueue{};
}

void refresh_link_states(EndpointState& a, EndpointState& b) noexcept {
    if (a.started && a.attached && b.started && b.attached) {
        a.state = SPW_LINK_RUN;
        b.state = SPW_LINK_RUN;
        return;
    }

    if (a.started && a.attached) {
        a.state = SPW_LINK_CONNECTING;
    }
    if (b.started && b.attached) {
        b.state = SPW_LINK_CONNECTING;
    }
}

bool peer_available(const EndpointState& peer) noexcept {
    return peer.attached && peer.started;
}

template <typename Predicate>
bool wait_for_condition(std::condition_variable& cv,
                        std::unique_lock<std::mutex>& lock,
                        spw_timeout_us_t timeout_us,
                        Predicate&& predicate) {
    if (predicate()) {
        return true;
    }
    if (timeout_us == SPW_TIMEOUT_IMMEDIATE) {
        return false;
    }

    if (timeout_us == SPW_TIMEOUT_INFINITE) {
        cv.wait(lock, std::forward<Predicate>(predicate));
        return true;
    }

    using microseconds = std::chrono::microseconds;
    using rep = microseconds::rep;
    const auto max_rep = static_cast<std::uint64_t>(std::numeric_limits<rep>::max());
    const auto bounded = std::min<std::uint64_t>(timeout_us, max_rep);
    return cv.wait_for(lock,
                       microseconds(static_cast<rep>(bounded)),
                       std::forward<Predicate>(predicate));
}

} // namespace

struct VirtualLink {
    std::mutex mutex{};
    std::condition_variable condition{};
    bool allocated{false};
    std::uint64_t link_id{0};
    std::array<EndpointState, 2> endpoints{};
};

namespace {

std::mutex registry_mutex;
std::array<VirtualLink, SimulatorBackend::max_local_links> registry{};

} // namespace

SimulatorBackend::SimulatorBackend(const spw_simulator_config_t& config) noexcept
    : link_id_(config.link_id),
      endpoint_index_(config.endpoint == SPW_SIMULATOR_ENDPOINT_B ? 1u : 0u) {}

SimulatorBackend::~SimulatorBackend() {
    detach();
}

bool SimulatorBackend::valid_terminator(spw_terminator_t terminator) noexcept {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

bool SimulatorBackend::valid_time_code(const spw_time_code_t& time_code) noexcept {
    return time_code.time_count <= 63u && time_code.control_flags == 0u;
}

spw_result_t SimulatorBackend::attach() noexcept {
    if (link_ != nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> registry_lock(registry_mutex);

    VirtualLink* target = nullptr;
    VirtualLink* free_slot = nullptr;
    for (auto& candidate : registry) {
        if (candidate.allocated && candidate.link_id == link_id_) {
            target = &candidate;
            break;
        }
        if (!candidate.allocated && free_slot == nullptr) {
            free_slot = &candidate;
        }
    }

    if (target == nullptr) {
        target = free_slot;
        if (target == nullptr) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
    }

    std::lock_guard<std::mutex> link_lock(target->mutex);
    if (!target->allocated) {
        target->allocated = true;
        target->link_id = link_id_;
        reset_endpoint(target->endpoints[0]);
        reset_endpoint(target->endpoints[1]);
    }

    EndpointState& local = target->endpoints[endpoint_index_];
    if (local.attached) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    reset_endpoint(local);
    local.attached = true;
    link_ = target;
    refresh_link_states(target->endpoints[0], target->endpoints[1]);
    target->condition.notify_all();
    return SPW_OK;
}

void SimulatorBackend::detach() noexcept {
    if (link_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> registry_lock(registry_mutex);
    std::lock_guard<std::mutex> link_lock(link_->mutex);

    EndpointState& local = link_->endpoints[endpoint_index_];
    EndpointState& peer = link_->endpoints[1u - endpoint_index_];

    reset_endpoint(local);
    if (peer.attached && peer.started) {
        peer.state = SPW_LINK_CONNECTING;
        ++peer.statistics.link_errors;
    }

    if (!link_->endpoints[0].attached && !link_->endpoints[1].attached) {
        link_->allocated = false;
        link_->link_id = 0u;
        reset_endpoint(link_->endpoints[0]);
        reset_endpoint(link_->endpoints[1]);
    }

    link_->condition.notify_all();
    link_ = nullptr;
}

spw_result_t SimulatorBackend::start() noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(link_->mutex);
    EndpointState& local = link_->endpoints[endpoint_index_];
    if (!local.attached) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    local.started = true;
    local.state = SPW_LINK_CONNECTING;
    refresh_link_states(link_->endpoints[0], link_->endpoints[1]);
    link_->condition.notify_all();
    return SPW_OK;
}

spw_result_t SimulatorBackend::stop() noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(link_->mutex);
    EndpointState& local = link_->endpoints[endpoint_index_];
    EndpointState& peer = link_->endpoints[1u - endpoint_index_];
    if (!local.attached) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    local.started = false;
    local.state = SPW_LINK_READY;
    if (peer.attached && peer.started) {
        peer.state = SPW_LINK_CONNECTING;
    }
    link_->condition.notify_all();
    return SPW_OK;
}

spw_result_t SimulatorBackend::reset() noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(link_->mutex);
    EndpointState& local = link_->endpoints[endpoint_index_];
    EndpointState& peer = link_->endpoints[1u - endpoint_index_];
    if (!local.attached) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    local.started = false;
    local.state = SPW_LINK_ERROR_RESET;
    clear_receive_queues(local);
    if (peer.attached && peer.started) {
        peer.state = SPW_LINK_CONNECTING;
        ++peer.statistics.link_errors;
    }
    link_->condition.notify_all();
    return SPW_OK;
}

spw_result_t SimulatorBackend::get_link_state(spw_link_state_t& state) const noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(link_->mutex);
    const EndpointState& local = link_->endpoints[endpoint_index_];
    if (!local.attached) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    state = local.state;
    return SPW_OK;
}

spw_result_t SimulatorBackend::get_capabilities(spw_capabilities_t& capabilities) const noexcept {
    capabilities.bits = SPW_CAP_EEP |
                        SPW_CAP_TIME_CODE |
                        SPW_CAP_LINK_CONTROL |
                        SPW_CAP_STATISTICS;
    capabilities.max_packet_size = max_packet_size;
    capabilities.tx_queue_depth = packet_queue_depth;
    capabilities.rx_queue_depth = packet_queue_depth;
    capabilities.buffer_alignment = alignof(std::max_align_t);
    return SPW_OK;
}

spw_result_t SimulatorBackend::send(const spw_packet_t& packet,
                                    spw_timeout_us_t timeout_us) noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_terminator(packet.terminator)) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packet.length > max_packet_size) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packet.length > 0u && packet.data == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (packet.capacity != 0u && packet.capacity < packet.length) {
        return SPW_ERR_INVALID_PACKET;
    }

    try {
        std::unique_lock<std::mutex> lock(link_->mutex);
        EndpointState& local = link_->endpoints[endpoint_index_];
        EndpointState& peer = link_->endpoints[1u - endpoint_index_];

        if (!local.attached || !local.started) {
            return SPW_ERR_INVALID_STATE;
        }
        if (!peer_available(peer)) {
            return SPW_ERR_LINK_UNAVAILABLE;
        }

        const bool ready = wait_for_condition(
            link_->condition, lock, timeout_us,
            [&] {
                return !local.started || !peer_available(peer) ||
                       peer.packets.count < packet_queue_depth;
            });
        if (!ready) {
            return timeout_us == SPW_TIMEOUT_IMMEDIATE
                       ? SPW_ERR_RESOURCE_EXHAUSTED
                       : SPW_ERR_TIMEOUT;
        }
        if (!local.started) {
            return SPW_ERR_INVALID_STATE;
        }
        if (!peer_available(peer)) {
            return SPW_ERR_LINK_UNAVAILABLE;
        }
        if (peer.packets.count == packet_queue_depth) {
            return SPW_ERR_TIMEOUT;
        }

        PacketSlot& slot = peer.packets.slots[peer.packets.tail];
        if (packet.length > 0u) {
            std::memcpy(slot.data.data(), packet.data, packet.length);
        }
        slot.length = packet.length;
        slot.terminator = packet.terminator;
        peer.packets.tail = (peer.packets.tail + 1u) % packet_queue_depth;
        ++peer.packets.count;

        ++local.statistics.tx_packets;
        local.statistics.tx_bytes += packet.length;
        if (packet.terminator == SPW_TERMINATOR_EEP) {
            ++local.statistics.eep_packets;
        }

        link_->condition.notify_all();
        return SPW_OK;
    } catch (...) {
        return SPW_ERR_BACKEND;
    }
}

spw_result_t SimulatorBackend::receive(spw_packet_t& packet,
                                       spw_timeout_us_t timeout_us) noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    try {
        std::unique_lock<std::mutex> lock(link_->mutex);
        EndpointState& local = link_->endpoints[endpoint_index_];
        EndpointState& peer = link_->endpoints[1u - endpoint_index_];

        if (!local.attached || !local.started) {
            return SPW_ERR_INVALID_STATE;
        }

        const bool ready = wait_for_condition(
            link_->condition, lock, timeout_us,
            [&] {
                return local.packets.count > 0u || !local.started ||
                       !peer_available(peer);
            });
        if (!ready) {
            return SPW_ERR_TIMEOUT;
        }
        if (!local.started) {
            return SPW_ERR_INVALID_STATE;
        }
        if (local.packets.count == 0u) {
            return !peer_available(peer) ? SPW_ERR_LINK_UNAVAILABLE : SPW_ERR_TIMEOUT;
        }

        const PacketSlot& slot = local.packets.slots[local.packets.head];
        packet.length = slot.length;
        packet.terminator = slot.terminator;

        if (slot.length > packet.capacity) {
            return SPW_ERR_BUFFER_TOO_SMALL;
        }
        if (slot.length > 0u && packet.data == nullptr) {
            return SPW_ERR_INVALID_ARGUMENT;
        }

        if (slot.length > 0u) {
            std::memcpy(packet.data, slot.data.data(), slot.length);
        }
        local.packets.head = (local.packets.head + 1u) % packet_queue_depth;
        --local.packets.count;

        ++local.statistics.rx_packets;
        local.statistics.rx_bytes += slot.length;
        link_->condition.notify_all();
        return SPW_OK;
    } catch (...) {
        return SPW_ERR_BACKEND;
    }
}

spw_result_t SimulatorBackend::send_time_code(const spw_time_code_t& time_code,
                                              spw_timeout_us_t timeout_us) noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    try {
        std::unique_lock<std::mutex> lock(link_->mutex);
        EndpointState& local = link_->endpoints[endpoint_index_];
        EndpointState& peer = link_->endpoints[1u - endpoint_index_];

        if (!local.attached || !local.started) {
            return SPW_ERR_INVALID_STATE;
        }
        if (!peer_available(peer)) {
            return SPW_ERR_LINK_UNAVAILABLE;
        }

        const bool ready = wait_for_condition(
            link_->condition, lock, timeout_us,
            [&] {
                return !local.started || !peer_available(peer) ||
                       peer.time_codes.count < time_code_queue_depth;
            });
        if (!ready) {
            return timeout_us == SPW_TIMEOUT_IMMEDIATE
                       ? SPW_ERR_RESOURCE_EXHAUSTED
                       : SPW_ERR_TIMEOUT;
        }
        if (!local.started) {
            return SPW_ERR_INVALID_STATE;
        }
        if (!peer_available(peer)) {
            return SPW_ERR_LINK_UNAVAILABLE;
        }

        peer.time_codes.slots[peer.time_codes.tail] = time_code;
        peer.time_codes.tail = (peer.time_codes.tail + 1u) % time_code_queue_depth;
        ++peer.time_codes.count;
        ++local.statistics.tx_time_codes;
        link_->condition.notify_all();
        return SPW_OK;
    } catch (...) {
        return SPW_ERR_BACKEND;
    }
}

spw_result_t SimulatorBackend::receive_time_code(spw_time_code_t& time_code,
                                                 spw_timeout_us_t timeout_us) noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    try {
        std::unique_lock<std::mutex> lock(link_->mutex);
        EndpointState& local = link_->endpoints[endpoint_index_];
        EndpointState& peer = link_->endpoints[1u - endpoint_index_];

        if (!local.attached || !local.started) {
            return SPW_ERR_INVALID_STATE;
        }

        const bool ready = wait_for_condition(
            link_->condition, lock, timeout_us,
            [&] {
                return local.time_codes.count > 0u || !local.started ||
                       !peer_available(peer);
            });
        if (!ready) {
            return SPW_ERR_TIMEOUT;
        }
        if (!local.started) {
            return SPW_ERR_INVALID_STATE;
        }
        if (local.time_codes.count == 0u) {
            return !peer_available(peer) ? SPW_ERR_LINK_UNAVAILABLE : SPW_ERR_TIMEOUT;
        }

        time_code = local.time_codes.slots[local.time_codes.head];
        local.time_codes.head = (local.time_codes.head + 1u) % time_code_queue_depth;
        --local.time_codes.count;
        ++local.statistics.rx_time_codes;
        link_->condition.notify_all();
        return SPW_OK;
    } catch (...) {
        return SPW_ERR_BACKEND;
    }
}

spw_result_t SimulatorBackend::get_statistics(spw_statistics_t& statistics) const noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(link_->mutex);
    const EndpointState& local = link_->endpoints[endpoint_index_];
    if (!local.attached) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    statistics = local.statistics;
    return SPW_OK;
}

spw_result_t SimulatorBackend::clear_statistics() noexcept {
    if (link_ == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(link_->mutex);
    EndpointState& local = link_->endpoints[endpoint_index_];
    if (!local.attached) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    local.statistics = spw_statistics_t{};
    return SPW_OK;
}

} // namespace spwkit::detail
