// SPDX-License-Identifier: Apache-2.0
#include "spwkit/spwkit.h"

static spw_result_t (*open_fn)(const spw_port_config_t*, spw_port_t**) = spw_port_open;
static spw_result_t (*close_fn)(spw_port_t*) = spw_port_close;
static spw_result_t (*send_fn)(spw_port_t*, const spw_packet_t*, spw_timeout_us_t) = spw_port_send;
static spw_result_t (*receive_fn)(spw_port_t*, spw_packet_t*, spw_timeout_us_t) = spw_port_receive;

int spwkit_api_c_compile_probe(void) {
    return (open_fn != 0 && close_fn != 0 && send_fn != 0 && receive_fn != 0) ? 0 : 1;
}
