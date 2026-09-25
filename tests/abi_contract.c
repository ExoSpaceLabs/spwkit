// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <spwkit/config.h>
#include <spwkit/device.h>
#include <spwkit/driver.h>
#include <spwkit/port.h>
#include <spwkit/raw_ethernet.h>
#include <spwkit/runtime.h>
#include <spwkit/simulator.h>
#include <spwkit/udp.h>

_Static_assert(SPW_PORT_CONFIG_MIN_SIZE == sizeof(spw_port_config_t),
               "port config must end at its 1.x extension boundary");
_Static_assert(SPW_SIMULATOR_CONFIG_MIN_SIZE == sizeof(spw_simulator_config_t),
               "simulator config must end at its 1.x extension boundary");
_Static_assert(SPW_UDP_CONFIG_MIN_SIZE == sizeof(spw_udp_config_t),
               "UDP config must end at its 1.x extension boundary");
_Static_assert(SPW_DEVICE_CONFIG_MIN_SIZE == sizeof(spw_device_config_t),
               "device config must end at its 1.x extension boundary");
_Static_assert(SPW_DRIVER_OPS_MIN_SIZE == sizeof(spw_driver_ops_t),
               "driver ops must end at its 1.x extension boundary");
_Static_assert(SPW_DRIVER_CONFIG_MIN_SIZE == sizeof(spw_driver_config_t),
               "driver config must end at its 1.x extension boundary");
_Static_assert(SPW_RAW_ETHERNET_IO_OPS_MIN_SIZE ==
                   sizeof(spw_raw_ethernet_io_ops_t),
               "raw Ethernet ops must end at its 1.x extension boundary");
_Static_assert(SPW_RAW_ETHERNET_CONFIG_MIN_SIZE ==
                   sizeof(spw_raw_ethernet_config_t),
               "raw Ethernet config must end at its 1.x extension boundary");
_Static_assert(SPW_RUNTIME_OPS_MIN_SIZE == sizeof(spw_runtime_ops_t),
               "runtime ops must end at its 1.x extension boundary");

typedef struct future_port_config {
    spw_port_config_t base;
    uint64_t future_tail;
} future_port_config_t;

#ifdef SPWKIT_TEST_SIMULATOR_ENABLED
typedef struct future_simulator_config {
    spw_simulator_config_t base;
    uint64_t future_tail;
} future_simulator_config_t;
#endif

static void test_port_prefix_contract(void) {
    spw_port_workspace_requirements_t requirements;
    future_port_config_t future;
    spw_port_config_t current = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

    memset(&future, 0, sizeof(future));
    future.base = current;
    future.base.struct_size = sizeof(future);

    assert(spw_port_workspace_requirements(&future.base, &requirements) == SPW_OK);
    assert(requirements.size != 0u);
    assert(requirements.alignment != 0u);

    current.struct_size = SPW_PORT_CONFIG_MIN_SIZE - 1u;
    assert(spw_port_workspace_requirements(&current, &requirements) ==
           SPW_ERR_INVALID_ARGUMENT);

    current = (spw_port_config_t)SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    current.version = SPW_PORT_CONFIG_VERSION + 1u;
    assert(spw_port_workspace_requirements(&current, &requirements) ==
           SPW_ERR_UNSUPPORTED);
}

#ifdef SPWKIT_TEST_SIMULATOR_ENABLED
static void test_backend_declared_size_contract(void) {
    future_simulator_config_t future;
    spw_port_config_t port =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    spw_port_workspace_requirements_t requirements;

    memset(&future, 0, sizeof(future));
    future.base = (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    future.base.link_id = UINT64_C(0xabc);
    future.base.endpoint = SPW_SIMULATOR_ENDPOINT_A;
    future.base.struct_size = sizeof(future);

    port.backend_config = &future.base;
    port.backend_config_size = sizeof(future);
    assert(spw_port_workspace_requirements(&port, &requirements) == SPW_OK);

    future.base.struct_size = sizeof(future) + sizeof(uint64_t);
    assert(spw_port_workspace_requirements(&port, &requirements) ==
           SPW_ERR_INVALID_ARGUMENT);

    future.base = (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    future.base.link_id = UINT64_C(0xabc);
    future.base.struct_size = SPW_SIMULATOR_CONFIG_MIN_SIZE - 1u;
    port.backend_config_size = SPW_SIMULATOR_CONFIG_MIN_SIZE - 1u;
    assert(spw_port_workspace_requirements(&port, &requirements) ==
           SPW_ERR_INVALID_ARGUMENT);

    future.base = (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    future.base.link_id = UINT64_C(0xabc);
    future.base.version = SPW_SIMULATOR_CONFIG_VERSION + 1u;
    port.backend_config_size = sizeof(future.base);
    assert(spw_port_workspace_requirements(&port, &requirements) ==
           SPW_ERR_UNSUPPORTED);
}
#endif

int main(void) {
    test_port_prefix_contract();
#ifdef SPWKIT_TEST_SIMULATOR_ENABLED
    test_backend_declared_size_contract();
#endif
    return 0;
}
