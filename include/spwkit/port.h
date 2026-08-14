// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_PORT_H
#define SPWKIT_PORT_H

#include <stddef.h>

#include "spwkit/config.h"
#include "spwkit/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Storage requirements for constructing a port without dynamic allocation.
 *
 * The caller must provide at least `size` bytes whose base address is aligned
 * to `alignment`. Requirements are backend/configuration specific and may
 * change between SpWKit versions, so callers should query them rather than
 * depending on backend object sizes.
 */
struct spw_port_workspace_requirements {
    size_t size;
    size_t alignment;
};

/**
 * Query caller-owned storage requirements for the selected backend.
 */
spw_result_t spw_port_workspace_requirements(
    const spw_port_config_t* config,
    spw_port_workspace_requirements_t* out_requirements);

/**
 * Construct a port entirely inside caller-owned storage.
 *
 * `workspace` remains owned by the caller for the lifetime of the returned
 * port. `spw_port_close()` destroys the port/backend objects but never frees
 * caller-owned workspace. The same storage may be reused after close returns.
 */
spw_result_t spw_port_open_in_place(const spw_port_config_t* config,
                                    void* workspace,
                                    size_t workspace_size,
                                    spw_port_t** out_port);

/**
 * Hosted convenience open.
 *
 * This operation may allocate dynamically. Builds configured with
 * `SPWKIT_ENABLE_HEAP=OFF` keep the symbol for ABI compatibility but return
 * `SPW_ERR_UNSUPPORTED`; use `spw_port_open_in_place()` for portable/no-heap
 * code.
 */
spw_result_t spw_port_open(const spw_port_config_t* config, spw_port_t** out_port);

spw_result_t spw_port_close(spw_port_t* port);
spw_result_t spw_port_start(spw_port_t* port);
spw_result_t spw_port_stop(spw_port_t* port);
spw_result_t spw_port_reset(spw_port_t* port);

spw_result_t spw_port_get_link_state(const spw_port_t* port,
                                     spw_link_state_t* out_state);
spw_result_t spw_port_get_capabilities(const spw_port_t* port,
                                       spw_capabilities_t* out_capabilities);

spw_result_t spw_port_send(spw_port_t* port,
                           const spw_packet_t* packet,
                           spw_timeout_us_t timeout_us);
spw_result_t spw_port_receive(spw_port_t* port,
                              spw_packet_t* packet,
                              spw_timeout_us_t timeout_us);

spw_result_t spw_port_send_time_code(spw_port_t* port,
                                     const spw_time_code_t* time_code,
                                     spw_timeout_us_t timeout_us);
spw_result_t spw_port_receive_time_code(spw_port_t* port,
                                        spw_time_code_t* time_code,
                                        spw_timeout_us_t timeout_us);

spw_result_t spw_port_get_statistics(const spw_port_t* port,
                                     spw_statistics_t* out_statistics);
spw_result_t spw_port_clear_statistics(spw_port_t* port);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_PORT_H */
