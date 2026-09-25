// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <assert.h>
#include <stddef.h>

_Static_assert(SPW_PORT_CONFIG_V1_MIN_SIZE <= sizeof(spw_port_config_t),
               "port config minimum exceeds current structure");
_Static_assert(SPW_SIMULATOR_CONFIG_V1_MIN_SIZE <= sizeof(spw_simulator_config_t),
               "simulator config minimum exceeds current structure");
_Static_assert(SPW_UDP_CONFIG_V3_MIN_SIZE <= sizeof(spw_udp_config_t),
               "UDP config minimum exceeds current structure");
_Static_assert(SPW_DEVICE_CONFIG_V1_MIN_SIZE <= sizeof(spw_device_config_t),
               "device config minimum exceeds current structure");
_Static_assert(SPW_DRIVER_OPS_V2_MIN_SIZE <= sizeof(spw_driver_ops_t),
               "driver ops minimum exceeds current structure");
_Static_assert(SPW_DRIVER_CONFIG_V2_MIN_SIZE <= sizeof(spw_driver_config_t),
               "driver config minimum exceeds current structure");
_Static_assert(SPW_RAW_ETHERNET_IO_OPS_V1_MIN_SIZE <= sizeof(spw_raw_ethernet_io_ops_t),
               "raw Ethernet ops minimum exceeds current structure");
_Static_assert(SPW_RAW_ETHERNET_CONFIG_V1_MIN_SIZE <= sizeof(spw_raw_ethernet_config_t),
               "raw Ethernet config minimum exceeds current structure");
_Static_assert(SPW_RUNTIME_OPS_V1_MIN_SIZE <= sizeof(spw_runtime_ops_t),
               "runtime ops minimum exceeds current structure");

int main(void) {
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    spw_port_config_t config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

    /* The published historical extent remains valid even if this type grows. */
    config.struct_size = SPW_PORT_CONFIG_V1_MIN_SIZE;
    assert(spw_port_workspace_requirements(&config, &requirements) == SPW_OK);

    /* A prefix shorter than the versioned contract is malformed. */
    config.struct_size = SPW_PORT_CONFIG_V1_MIN_SIZE - 1u;
    assert(spw_port_workspace_requirements(&config, &requirements) ==
           SPW_ERR_INVALID_ARGUMENT);

    /* Future appended bytes do not invalidate the known compatible prefix. */
    config = (spw_port_config_t)
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    config.struct_size = sizeof(config) + 64u;
    assert(spw_port_workspace_requirements(&config, &requirements) == SPW_OK);

    /* A different contract generation is not silently reinterpreted. */
    config = (spw_port_config_t)
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    config.version = SPW_PORT_CONFIG_VERSION + 1u;
    assert(spw_port_workspace_requirements(&config, &requirements) ==
           SPW_ERR_UNSUPPORTED);

    return 0;
}
