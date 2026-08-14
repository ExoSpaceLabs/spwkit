// SPDX-License-Identifier: Apache-2.0

#include "backends/virtual/simulator_backend.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

namespace spwkit::detail {
namespace {

template <typename Predicate>
bool wait_for_buffer(std::condition_variable& condition,
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
        condition.wait(lock, std::forward<Predicate>(predicate));
        return true;
    }

    using microseconds = std::chrono::microseconds;
    using rep = microseconds::rep;
    const auto max_rep = static_cast<std::uint64_t>(std::numeric_limits<rep>::max());
    const auto bounded = std::min<std::uint64_t>(timeout_us, max_rep);
    return condition.wait_for(lock,
                              microseconds(static_cast<rep>(bounded)),
                              std::forward<Predicate>(predicate));
}

} // namespace

void SimulatorBackend::initialize_zero_copy_buffers() noexcept {
    if (zero_copy_initialized_) {
        return;
    }

    for (std::size_t index = 0; index < tx_buffers_.size(); ++index) {
        auto& slot = tx_buffers_[index];
        slot.descriptor.data = slot.storage.data();
        slot.descriptor.length = 0u;
        slot.descriptor.capacity = slot.storage.size();
        slot.descriptor.terminator = SPW_TERMINATOR_EOP;
        slot.descriptor.owner = this;
        slot.descriptor.direction = BufferDirection::tx;
        slot.descriptor.state = BufferState::free;
        slot.descriptor.token = index;
    }

    rx_buffer_.data = rx_storage_.data();
    rx_buffer_.length = 0u;
    rx_buffer_.capacity = rx_storage_.size();
    rx_buffer_.terminator = SPW_TERMINATOR_EOP;
    rx_buffer_.owner = this;
    rx_buffer_.direction = BufferDirection::rx;
    rx_buffer_.state = BufferState::free;
    rx_buffer_.token = 0u;
    zero_copy_initialized_ = true;
}

spw_result_t SimulatorBackend::acquire_tx_buffer(std::size_t min_capacity,
                                                 spw_timeout_us_t timeout_us,
                                                 spw_buffer_t*& out_buffer) noexcept {
    out_buffer = nullptr;
    if (min_capacity > max_packet_size) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    std::unique_lock<std::mutex> lock(zero_copy_mutex_);
    initialize_zero_copy_buffers();

    const auto find_free = [this]() -> spw_buffer* {
        for (auto& slot : tx_buffers_) {
            if (slot.descriptor.state == BufferState::free) {
                return &slot.descriptor;
            }
        }
        return nullptr;
    };

    spw_buffer* buffer = find_free();
    if (buffer == nullptr) {
        const bool ready = wait_for_buffer(
            zero_copy_condition_, lock, timeout_us,
            [&] { return find_free() != nullptr; });
        if (!ready) {
            return timeout_us == SPW_TIMEOUT_IMMEDIATE
                       ? SPW_ERR_RESOURCE_EXHAUSTED
                       : SPW_ERR_TIMEOUT;
        }
        buffer = find_free();
    }

    if (buffer == nullptr) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    buffer->length = 0u;
    buffer->terminator = SPW_TERMINATOR_EOP;
    buffer->state = BufferState::application;
    out_buffer = buffer;
    return SPW_OK;
}

spw_result_t SimulatorBackend::submit_tx_buffer(spw_buffer_t& public_buffer,
                                                spw_timeout_us_t timeout_us) noexcept {
    auto& buffer = static_cast<spw_buffer&>(public_buffer);
    {
        std::lock_guard<std::mutex> lock(zero_copy_mutex_);
        initialize_zero_copy_buffers();
        if (buffer.owner != this || buffer.direction != BufferDirection::tx ||
            buffer.state != BufferState::application) {
            return SPW_ERR_INVALID_STATE;
        }
        if (buffer.length > buffer.capacity || buffer.length > max_packet_size ||
            !valid_terminator(buffer.terminator)) {
            return SPW_ERR_INVALID_PACKET;
        }
        buffer.state = BufferState::backend;
    }

    spw_packet_t packet{
        buffer.data,
        buffer.length,
        buffer.capacity,
        buffer.terminator,
    };
    const spw_result_t result = send(packet, timeout_us);

    {
        std::lock_guard<std::mutex> lock(zero_copy_mutex_);
        buffer.state = result == SPW_OK ? BufferState::completed
                                        : BufferState::application;
    }
    zero_copy_condition_.notify_all();
    return result;
}

spw_result_t SimulatorBackend::reclaim_tx_buffer(spw_timeout_us_t timeout_us,
                                                 spw_buffer_t*& out_buffer) noexcept {
    out_buffer = nullptr;
    std::unique_lock<std::mutex> lock(zero_copy_mutex_);
    initialize_zero_copy_buffers();

    const auto find_completed = [this]() -> spw_buffer* {
        for (auto& slot : tx_buffers_) {
            if (slot.descriptor.state == BufferState::completed) {
                return &slot.descriptor;
            }
        }
        return nullptr;
    };

    spw_buffer* buffer = find_completed();
    if (buffer == nullptr) {
        const bool ready = wait_for_buffer(
            zero_copy_condition_, lock, timeout_us,
            [&] { return find_completed() != nullptr; });
        if (!ready) {
            return SPW_ERR_TIMEOUT;
        }
        buffer = find_completed();
    }

    if (buffer == nullptr) {
        return SPW_ERR_TIMEOUT;
    }

    buffer->state = BufferState::application;
    out_buffer = buffer;
    return SPW_OK;
}

spw_result_t SimulatorBackend::release_tx_buffer(spw_buffer_t& public_buffer) noexcept {
    auto& buffer = static_cast<spw_buffer&>(public_buffer);
    std::lock_guard<std::mutex> lock(zero_copy_mutex_);
    initialize_zero_copy_buffers();
    if (buffer.owner != this || buffer.direction != BufferDirection::tx ||
        buffer.state != BufferState::application) {
        return SPW_ERR_INVALID_STATE;
    }

    buffer.length = 0u;
    buffer.terminator = SPW_TERMINATOR_EOP;
    buffer.state = BufferState::free;
    zero_copy_condition_.notify_all();
    return SPW_OK;
}

spw_result_t SimulatorBackend::acquire_rx_buffer(spw_timeout_us_t timeout_us,
                                                 spw_buffer_t*& out_buffer) noexcept {
    out_buffer = nullptr;
    {
        std::unique_lock<std::mutex> lock(zero_copy_mutex_);
        initialize_zero_copy_buffers();
        const bool available = wait_for_buffer(
            zero_copy_condition_, lock, timeout_us,
            [this] { return !rx_buffer_acquired_; });
        if (!available) {
            return timeout_us == SPW_TIMEOUT_IMMEDIATE
                       ? SPW_ERR_RESOURCE_EXHAUSTED
                       : SPW_ERR_TIMEOUT;
        }
        rx_buffer_acquired_ = true;
        rx_buffer_.state = BufferState::backend;
    }

    spw_packet_t packet{
        rx_storage_.data(),
        0u,
        rx_storage_.size(),
        SPW_TERMINATOR_EOP,
    };
    const spw_result_t result = receive(packet, timeout_us);

    std::lock_guard<std::mutex> lock(zero_copy_mutex_);
    if (result != SPW_OK) {
        rx_buffer_.state = BufferState::free;
        rx_buffer_acquired_ = false;
        zero_copy_condition_.notify_all();
        return result;
    }

    rx_buffer_.length = packet.length;
    rx_buffer_.terminator = packet.terminator;
    rx_buffer_.state = BufferState::application;
    out_buffer = &rx_buffer_;
    return SPW_OK;
}

spw_result_t SimulatorBackend::release_rx_buffer(spw_buffer_t& public_buffer) noexcept {
    auto& buffer = static_cast<spw_buffer&>(public_buffer);
    std::lock_guard<std::mutex> lock(zero_copy_mutex_);
    initialize_zero_copy_buffers();
    if (&buffer != &rx_buffer_ || buffer.owner != this ||
        buffer.direction != BufferDirection::rx ||
        buffer.state != BufferState::application || !rx_buffer_acquired_) {
        return SPW_ERR_INVALID_STATE;
    }

    buffer.length = 0u;
    buffer.terminator = SPW_TERMINATOR_EOP;
    buffer.state = BufferState::free;
    rx_buffer_acquired_ = false;
    zero_copy_condition_.notify_all();
    return SPW_OK;
}

} // namespace spwkit::detail
