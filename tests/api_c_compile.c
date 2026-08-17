// SPDX-License-Identifier: Apache-2.0
#include "spwkit/spwkit.h"

static spw_result_t (*open_fn)(const spw_port_config_t*, spw_port_t**) = spw_port_open;
static spw_result_t (*close_fn)(spw_port_t*) = spw_port_close;
static spw_result_t (*start_fn)(spw_port_t*) = spw_port_start;
static spw_result_t (*wait_fn)(spw_port_t*, spw_ready_events_t,
                               spw_timeout_us_t, spw_ready_events_t*) = spw_port_wait;
static spw_result_t (*send_fn)(spw_port_t*, const spw_packet_t*, spw_timeout_us_t) = spw_port_send;
static spw_result_t (*receive_fn)(spw_port_t*, spw_packet_t*, spw_timeout_us_t) = spw_port_receive;
static spw_result_t (*send_tc_fn)(spw_port_t*, const spw_time_code_t*, spw_timeout_us_t) =
    spw_port_send_time_code;
static spw_result_t (*receive_tc_fn)(spw_port_t*, spw_time_code_t*, spw_timeout_us_t) =
    spw_port_receive_time_code;
static spw_result_t (*stats_fn)(const spw_port_t*, spw_statistics_t*) = spw_port_get_statistics;
static spw_result_t (*workspace_fn)(const spw_port_config_t*,
                                    spw_port_workspace_requirements_t*) =
    spw_port_workspace_requirements;

int spwkit_api_c_compile_probe(void) {
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(42000u, 42001u, 42u);
    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(0u);

    return (open_fn != 0 && close_fn != 0 && start_fn != 0 && wait_fn != 0 &&
            send_fn != 0 && receive_fn != 0 && send_tc_fn != 0 &&
            receive_tc_fn != 0 && stats_fn != 0 && workspace_fn != 0 &&
            simulator.struct_size != 0u && udp.struct_size != 0u &&
            device.struct_size != 0u)
               ? 0
               : 1;
}
