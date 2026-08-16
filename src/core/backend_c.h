// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_INTERNAL_BACKEND_C_H
#define SPWKIT_INTERNAL_BACKEND_C_H

#include <stdbool.h>
#include <stddef.h>

#include <spwkit/config.h>
#include <spwkit/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spw_backend_ops {
    spw_result_t (*start)(void* context);
    spw_result_t (*stop)(void* context);
    spw_result_t (*reset)(void* context);

    spw_result_t (*get_link_state)(const void* context,
                                   spw_link_state_t* out_state);
    spw_result_t (*get_capabilities)(const void* context,
                                     spw_capabilities_t* out_capabilities);
    bool (*supports_zero_copy)(const void* context);

    spw_result_t (*send)(void* context,
                         const spw_packet_t* packet,
                         spw_timeout_us_t timeout_us);
    spw_result_t (*receive)(void* context,
                            spw_packet_t* packet,
                            spw_timeout_us_t timeout_us);

    spw_result_t (*send_time_code)(void* context,
                                   const spw_time_code_t* time_code,
                                   spw_timeout_us_t timeout_us);
    spw_result_t (*receive_time_code)(void* context,
                                      spw_time_code_t* time_code,
                                      spw_timeout_us_t timeout_us);

    spw_result_t (*get_statistics)(const void* context,
                                   spw_statistics_t* out_statistics);
    spw_result_t (*clear_statistics)(void* context);

    spw_result_t (*get_fault_statistics)(
        const void* context,
        spw_fault_statistics_t* out_statistics);
    spw_result_t (*clear_fault_statistics)(void* context);

    spw_result_t (*acquire_tx_buffer)(void* context,
                                      size_t min_capacity,
                                      spw_timeout_us_t timeout_us,
                                      spw_buffer_t** out_buffer);
    spw_result_t (*submit_tx_buffer)(void* context,
                                     spw_buffer_t* buffer,
                                     spw_timeout_us_t timeout_us);
    spw_result_t (*reclaim_tx_buffer)(void* context,
                                      spw_timeout_us_t timeout_us,
                                      spw_buffer_t** out_buffer);
    spw_result_t (*release_tx_buffer)(void* context,
                                      spw_buffer_t* buffer);
    spw_result_t (*acquire_rx_buffer)(void* context,
                                      spw_timeout_us_t timeout_us,
                                      spw_buffer_t** out_buffer);
    spw_result_t (*release_rx_buffer)(void* context,
                                      spw_buffer_t* buffer);
} spw_backend_ops_t;

typedef struct spw_backend_factory {
    size_t context_size;
    size_t context_alignment;
    spw_result_t (*construct)(void* context,
                              const spw_port_config_t* config);
    void (*destroy)(void* context);
    const spw_backend_ops_t* ops;
} spw_backend_factory_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_INTERNAL_BACKEND_C_H */
