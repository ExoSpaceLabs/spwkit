// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_BUFFER_H
#define SPWKIT_BUFFER_H

#include "spwkit/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Read the current application-owned view of an opaque buffer. */
spw_result_t spw_buffer_get_view(const spw_buffer_t* buffer,
                                 spw_buffer_view_t* out_view);

/**
 * Set TX packet metadata before submission.
 *
 * Valid only for an application-owned TX buffer. `length` must not exceed the
 * acquired capacity and terminator must be EOP or EEP.
 */
spw_result_t spw_buffer_set_packet(spw_buffer_t* buffer,
                                   size_t length,
                                   spw_terminator_t terminator);

/** Acquire an application-owned TX buffer with at least min_capacity bytes. */
spw_result_t spw_port_acquire_tx_buffer(spw_port_t* port,
                                        size_t min_capacity,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer);

/**
 * Transfer TX-buffer ownership to the backend.
 *
 * On success `*inout_buffer` is set to NULL. On failure ownership remains with
 * the application and the pointer is unchanged.
 */
spw_result_t spw_port_submit_tx_buffer(spw_port_t* port,
                                       spw_buffer_t** inout_buffer,
                                       spw_timeout_us_t timeout_us);

/** Reclaim one completed TX buffer back into application ownership. */
spw_result_t spw_port_reclaim_tx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer);

/** Return an application-owned TX buffer to the backend pool. */
spw_result_t spw_port_release_tx_buffer(spw_port_t* port,
                                        spw_buffer_t** inout_buffer);

/** Acquire the next received packet as an application-owned RX buffer. */
spw_result_t spw_port_acquire_rx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer);

/**
 * Release an application-owned RX buffer and consume that received packet.
 * On success `*inout_buffer` is set to NULL.
 */
spw_result_t spw_port_release_rx_buffer(spw_port_t* port,
                                        spw_buffer_t** inout_buffer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_BUFFER_H */
