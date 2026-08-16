// SPDX-License-Identifier: Apache-2.0
#pragma once

/*
 * Optional C++17 convenience layer.
 *
 * This header contains no backend implementation. Every operation delegates to
 * the public C API so C and C++ applications share exactly one runtime and one
 * semantic contract.
 */
#include <spwkit/spwkit.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace spwkit {

struct Version {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;
};

inline constexpr Version version{
    SPWKIT_API_VERSION_MAJOR,
    SPWKIT_API_VERSION_MINOR,
    SPWKIT_API_VERSION_PATCH,
};

using Result = spw_result_t;
using Timeout = spw_timeout_us_t;

inline constexpr Timeout immediate = SPW_TIMEOUT_IMMEDIATE;
inline constexpr Timeout infinite = SPW_TIMEOUT_INFINITE;

class Port {
public:
    constexpr Port() noexcept = default;
    ~Port() noexcept { (void)close(); }

    Port(const Port&) = delete;
    Port& operator=(const Port&) = delete;

    Port(Port&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}

    Port& operator=(Port&& other) noexcept {
        if (this != &other) {
            (void)close();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    static Result open(const spw_port_config_t& config, Port& out) noexcept {
        if (out.handle_ != nullptr) {
            return SPW_ERR_INVALID_STATE;
        }
        spw_port_t* handle = nullptr;
        const Result result = spw_port_open(&config, &handle);
        if (result == SPW_OK) {
            out.handle_ = handle;
        }
        return result;
    }

    static Result open_in_place(const spw_port_config_t& config,
                                void* workspace,
                                std::size_t workspace_size,
                                Port& out) noexcept {
        if (out.handle_ != nullptr) {
            return SPW_ERR_INVALID_STATE;
        }
        spw_port_t* handle = nullptr;
        const Result result = spw_port_open_in_place(
            &config, workspace, workspace_size, &handle);
        if (result == SPW_OK) {
            out.handle_ = handle;
        }
        return result;
    }

    Result close() noexcept {
        if (handle_ == nullptr) {
            return SPW_OK;
        }
        spw_port_t* handle = std::exchange(handle_, nullptr);
        return spw_port_close(handle);
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return handle_ != nullptr;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return valid();
    }

    [[nodiscard]] constexpr spw_port_t* native_handle() noexcept {
        return handle_;
    }

    [[nodiscard]] constexpr const spw_port_t* native_handle() const noexcept {
        return handle_;
    }

    Result start() noexcept { return invoke(spw_port_start); }
    Result stop() noexcept { return invoke(spw_port_stop); }
    Result reset() noexcept { return invoke(spw_port_reset); }

    Result link_state(spw_link_state_t& state) const noexcept {
        return handle_ != nullptr
                   ? spw_port_get_link_state(handle_, &state)
                   : SPW_ERR_INVALID_STATE;
    }

    Result capabilities(spw_capabilities_t& capabilities) const noexcept {
        return handle_ != nullptr
                   ? spw_port_get_capabilities(handle_, &capabilities)
                   : SPW_ERR_INVALID_STATE;
    }

    Result wait(spw_ready_events_t interests,
                spw_ready_events_t& ready,
                Timeout timeout = immediate) noexcept {
        return handle_ != nullptr
                   ? spw_port_wait(handle_, interests, timeout, &ready)
                   : SPW_ERR_INVALID_STATE;
    }

    Result send(const spw_packet_t& packet,
                Timeout timeout = immediate) noexcept {
        return handle_ != nullptr
                   ? spw_port_send(handle_, &packet, timeout)
                   : SPW_ERR_INVALID_STATE;
    }

    Result send(std::uint8_t* data,
                std::size_t length,
                spw_terminator_t terminator = SPW_TERMINATOR_EOP,
                Timeout timeout = immediate) noexcept {
        spw_packet_t packet{data, length, length, terminator};
        return send(packet, timeout);
    }

    Result receive(spw_packet_t& packet,
                   Timeout timeout = immediate) noexcept {
        return handle_ != nullptr
                   ? spw_port_receive(handle_, &packet, timeout)
                   : SPW_ERR_INVALID_STATE;
    }

    Result send_time_code(const spw_time_code_t& time_code,
                          Timeout timeout = immediate) noexcept {
        return handle_ != nullptr
                   ? spw_port_send_time_code(handle_, &time_code, timeout)
                   : SPW_ERR_INVALID_STATE;
    }

    Result receive_time_code(spw_time_code_t& time_code,
                             Timeout timeout = immediate) noexcept {
        return handle_ != nullptr
                   ? spw_port_receive_time_code(handle_, &time_code, timeout)
                   : SPW_ERR_INVALID_STATE;
    }

    Result statistics(spw_statistics_t& statistics) const noexcept {
        return handle_ != nullptr
                   ? spw_port_get_statistics(handle_, &statistics)
                   : SPW_ERR_INVALID_STATE;
    }

    Result clear_statistics() noexcept {
        return handle_ != nullptr
                   ? spw_port_clear_statistics(handle_)
                   : SPW_ERR_INVALID_STATE;
    }

    Result fault_statistics(spw_fault_statistics_t& statistics) const noexcept {
        return handle_ != nullptr
                   ? spw_port_get_fault_statistics(handle_, &statistics)
                   : SPW_ERR_INVALID_STATE;
    }

    Result clear_fault_statistics() noexcept {
        return handle_ != nullptr
                   ? spw_port_clear_fault_statistics(handle_)
                   : SPW_ERR_INVALID_STATE;
    }

private:
    using LifecycleFn = spw_result_t (*)(spw_port_t*);

    Result invoke(LifecycleFn function) noexcept {
        return handle_ != nullptr ? function(handle_) : SPW_ERR_INVALID_STATE;
    }

    spw_port_t* handle_{nullptr};
};

} // namespace spwkit
