// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(SPW_PORT_CONFIG_V1_MIN_SIZE == sizeof(spw_port_config_t),
               "port config extension boundary must equal current structure size");
_Static_assert(SPW_SIMULATOR_CONFIG_V1_MIN_SIZE == sizeof(spw_simulator_config_t),
               "simulator config extension boundary must equal current structure size");
_Static_assert(SPW_UDP_CONFIG_V4_MIN_SIZE == sizeof(spw_udp_config_t),
               "UDP config extension boundary must equal current structure size");
_Static_assert(SPW_DEVICE_CONFIG_V1_MIN_SIZE == sizeof(spw_device_config_t),
               "device config extension boundary must equal current structure size");
_Static_assert(SPW_DRIVER_OPS_V2_MIN_SIZE == sizeof(spw_driver_ops_t),
               "driver ops extension boundary must equal current structure size");
_Static_assert(SPW_DRIVER_CONFIG_V2_MIN_SIZE == sizeof(spw_driver_config_t),
               "driver config extension boundary must equal current structure size");
_Static_assert(SPW_RAW_ETHERNET_IO_OPS_V1_MIN_SIZE == sizeof(spw_raw_ethernet_io_ops_t),
               "raw Ethernet ops extension boundary must equal current structure size");
_Static_assert(SPW_RAW_ETHERNET_CONFIG_V1_MIN_SIZE == sizeof(spw_raw_ethernet_config_t),
               "raw Ethernet config extension boundary must equal current structure size");
_Static_assert(SPW_RUNTIME_OPS_V1_MIN_SIZE == sizeof(spw_runtime_ops_t),
               "runtime ops extension boundary must equal current structure size");

#ifdef SPWKIT_TEST_SIMULATOR_ENABLED
typedef struct future_simulator_config {
    spw_simulator_config_t base;
    uint64_t future_tail;
} future_simulator_config_t;

static void test_backend_declared_size_contract(void) {
    future_simulator_config_t future;
    spw_port_config_t port =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    spw_port_workspace_requirements_t requirements = {0u, 0u};

    memset(&future, 0, sizeof(future));
    future.base = (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    future.base.link_id = UINT64_C(0xabc);
    future.base.endpoint = SPW_SIMULATOR_ENDPOINT_A;
    future.base.struct_size = sizeof(future);
    port.backend_config = &future.base;
    port.backend_config_size = sizeof(future);

    assert(spw_port_workspace_requirements(&port, &requirements) == SPW_OK);

    /* A structure may not advertise bytes beyond the containing byte count. */
    future.base.struct_size = sizeof(future) + sizeof(uint64_t);
    assert(spw_port_workspace_requirements(&port, &requirements) ==
           SPW_ERR_INVALID_ARGUMENT);

    future.base =
        (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    future.base.link_id = UINT64_C(0xabc);
    future.base.struct_size = SPW_SIMULATOR_CONFIG_V1_MIN_SIZE - 1u;
    port.backend_config_size = SPW_SIMULATOR_CONFIG_V1_MIN_SIZE - 1u;
    assert(spw_port_workspace_requirements(&port, &requirements) ==
           SPW_ERR_INVALID_ARGUMENT);

    future.base =
        (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    future.base.link_id = UINT64_C(0xabc);
    future.base.version = SPW_SIMULATOR_CONFIG_VERSION + 1u;
    port.backend_config_size = sizeof(future.base);
    assert(spw_port_workspace_requirements(&port, &requirements) ==
           SPW_ERR_UNSUPPORTED);
}
#endif

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

#ifdef SPWKIT_TEST_SIMULATOR_ENABLED
    test_backend_declared_size_contract();
#endif
    return 0;
}
