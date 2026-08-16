// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_INTERNAL_BUFFER_H
#define SPWKIT_INTERNAL_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include <spwkit/types.h>

#ifdef __cplusplus
namespace spwkit::detail {
enum class BufferDirection : uint8_t {
    tx = 1u,
    rx = 2u,
};
enum class BufferState : uint8_t {
    free = 0u,
    application = 1u,
    backend = 2u,
    completed = 3u,
};
} // namespace spwkit::detail

typedef spwkit::detail::BufferDirection spw_buffer_direction_internal_t;
typedef spwkit::detail::BufferState spw_buffer_state_internal_t;

#define SPW_BUFFER_DIRECTION_TX spwkit::detail::BufferDirection::tx
#define SPW_BUFFER_DIRECTION_RX spwkit::detail::BufferDirection::rx
#define SPW_BUFFER_STATE_FREE spwkit::detail::BufferState::free
#define SPW_BUFFER_STATE_APPLICATION spwkit::detail::BufferState::application
#define SPW_BUFFER_STATE_BACKEND spwkit::detail::BufferState::backend
#define SPW_BUFFER_STATE_COMPLETED spwkit::detail::BufferState::completed
#else
typedef uint8_t spw_buffer_direction_internal_t;
typedef uint8_t spw_buffer_state_internal_t;

#define SPW_BUFFER_DIRECTION_TX ((spw_buffer_direction_internal_t)1u)
#define SPW_BUFFER_DIRECTION_RX ((spw_buffer_direction_internal_t)2u)
#define SPW_BUFFER_STATE_FREE ((spw_buffer_state_internal_t)0u)
#define SPW_BUFFER_STATE_APPLICATION ((spw_buffer_state_internal_t)1u)
#define SPW_BUFFER_STATE_BACKEND ((spw_buffer_state_internal_t)2u)
#define SPW_BUFFER_STATE_COMPLETED ((spw_buffer_state_internal_t)3u)
#endif

/* Internal representation of the opaque public handle. */
struct spw_buffer {
    uint8_t* data;
    size_t length;
    size_t capacity;
    spw_terminator_t terminator;
    void* owner;
    spw_buffer_direction_internal_t direction;
    spw_buffer_state_internal_t state;
    size_t token;
};

#endif /* SPWKIT_INTERNAL_BUFFER_H */
