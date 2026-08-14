// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_PORT_H
#define SPWKIT_PORT_H

#include "spwkit/api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Open a SpaceWire port using a backend-independent configuration object. */
spw_result_t spw_port_open(const spw_port_config_t* config, spw_port_t** out_port);

/** Release a port and any backend resources owned by it. */
spw_result_t spw_port_close(spw_port_t* port);

/** Request link startup. */
spw_result_t spw_port_start(spw_port_t* port);

/** Request link stop/disable. */
spw_result_t spw_port_stop(spw_port_t* port);

/** Reset the software-visible link state and backend state where supported. */
spw_result_t spw_port_reset(spw_port_t* port);

/** Query the current software-visible SpaceWire link state. */
spw_result_t spw_port_get_link_state(const spw_port_t* port,
                                     spw_link_state_t* out_state);

/** Query backend capabilities without exposing backend-specific types. */
spw_result_t spw_port_get_capabilities(const spw_port_t* port,
                                       spw_capabilities_t* out_capabilities);

/**
 * Send one SpaceWire packet.
 *
 * Packet ownership remains with the caller. The packet terminator is part of
 * the packet representation and must be preserved by conforming backends.
 */
spw_result_t spw_port_send(spw_port_t* port,
                           const spw_packet_t* packet,
                           spw_timeout_us_t timeout_us);

/**
 * Receive one complete SpaceWire packet.
 *
 * The concrete packet type defines how caller-provided storage and received
 * length/terminator information are represented.
 */
spw_result_t spw_port_receive(spw_port_t* port,
                              spw_packet_t* packet,
                              spw_timeout_us_t timeout_us);

/** Send a SpaceWire time code where the backend advertises support. */
spw_result_t spw_port_send_time_code(spw_port_t* port,
                                     const spw_time_code_t* time_code,
                                     spw_timeout_us_t timeout_us);

/** Receive a SpaceWire time code where the backend advertises support. */
spw_result_t spw_port_receive_time_code(spw_port_t* port,
                                        spw_time_code_t* time_code,
                                        spw_timeout_us_t timeout_us);

/** Read backend-independent counters and diagnostic statistics. */
spw_result_t spw_port_get_statistics(const spw_port_t* port,
                                     spw_statistics_t* out_statistics);

/** Clear resettable statistics where supported. */
spw_result_t spw_port_clear_statistics(spw_port_t* port);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_PORT_H */
