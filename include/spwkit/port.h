// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_PORT_H
#define SPWKIT_PORT_H

#include "spwkit/types.h"

#ifdef __cplusplus
extern "C" {
#endif

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
