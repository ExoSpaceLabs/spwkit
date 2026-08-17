// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    spw_device_config_t device;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    unsigned long port_id;
    size_t endpoint_length;
    spw_result_t result;

    if (argc != 3) {
        return 2;
    }

    port_id = strtoul(argv[2], NULL, 10);
    if (port_id > UINT32_MAX) {
        return 2;
    }

    device = (spw_device_config_t)SPW_DEVICE_CONFIG_INITIALIZER((uint32_t)port_id);
    endpoint_length = strlen(argv[1]);
    if (endpoint_length == 0u || endpoint_length >= sizeof(device.endpoint)) {
        return 2;
    }
    memcpy(device.endpoint, argv[1], endpoint_length + 1u);

    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    result = spw_port_open(&config, &port);
    if (result != SPW_OK || port == NULL) {
        return 1;
    }

    (void)spw_port_close(port);
    return 0;
}
