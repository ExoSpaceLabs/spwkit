// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <spwkit/types.h>

#include <cstddef>
#include <cstdint>

namespace spwkit::detail {

enum class BufferDirection : std::uint8_t {
    tx = 1u,
    rx = 2u,
};

enum class BufferState : std::uint8_t {
    free = 0u,
    application = 1u,
    backend = 2u,
    completed = 3u,
};

} // namespace spwkit::detail

/* Internal representation of the opaque public handle. */
struct spw_buffer {
    std::uint8_t* data{nullptr};
    std::size_t length{0u};
    std::size_t capacity{0u};
    spw_terminator_t terminator{SPW_TERMINATOR_EOP};
    void* owner{nullptr};
    spwkit::detail::BufferDirection direction{spwkit::detail::BufferDirection::tx};
    spwkit::detail::BufferState state{spwkit::detail::BufferState::free};
    std::size_t token{0u};
};
