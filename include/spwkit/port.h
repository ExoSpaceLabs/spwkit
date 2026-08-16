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

/** Query caller-owned storage requirements for the selected backend. */
spw_result_t spw_port_workspace_requirements(
    const spw_port_config_t* config,
    spw_port_workspace_requirements_t* out_requirements);

/** Construct a port entirely inside caller-owned storage. */
spw_result_t spw_port_open_in_place(const spw_port_config_t* config,
                                    void* workspace,
                                    size_t workspace_size,
                                    spw_port_t** out_port);

/** Hosted convenience open. May allocate dynamically when enabled. */
spw_result_t spw_port_open(const spw_port_config_t* config, spw_port_t** out_port);

spw_result_t spw_port_close(spw_port_t* port);
spw_result_t spw_port_start(spw_port_t* port);
spw_result_t spw_port_stop(spw_port_t* port);
spw_result_t spw_port_reset(spw_port_t* port);

spw_result_t spw_port_get_link_state(const spw_port_t* port,
                                     spw_link_state_t* out_state);
spw_result_t spw_port_get_capabilities(const spw_port_t* port,
                                       spw_capabilities_t* out_capabilities);

/**
 * Wait until one or more requested receive events are ready without consuming
 * them. `interests` must contain only SPW_READY_* bits and must not be zero.
 *
 * On success, `out_ready` contains one or more requested events that remain
 * available to the normal receive APIs. A backend that does not advertise
 * SPW_CAP_READINESS returns SPW_ERR_UNSUPPORTED. Timeout semantics match the
 * rest of the port API, including SPW_TIMEOUT_IMMEDIATE and
 * SPW_TIMEOUT_INFINITE.
 */
spw_result_t spw_port_wait(spw_port_t* port,
                           spw_ready_events_t interests,
                           spw_timeout_us_t timeout_us,
                           spw_ready_events_t* out_ready);

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

/**
 * Read backend-neutral fault-domain diagnostics when supported.
 *
 * A backend that does not implement fault injection returns
 * `SPW_ERR_UNSUPPORTED`. Transport and SpaceWire-visible fault counters are
 * deliberately separate.
 */
spw_result_t spw_port_get_fault_statistics(
    const spw_port_t* port,
    spw_fault_statistics_t* out_statistics);
spw_result_t spw_port_clear_fault_statistics(spw_port_t* port);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_PORT_H */
