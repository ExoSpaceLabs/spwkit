// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/device.h>
#include <spwkit/spwkit.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void sleep_ms(long milliseconds) {
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000 * 1000;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

int main(int argc, char** argv) {
    spw_device_config_t device;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    unsigned long parsed_port;
    char* end = NULL;
    int64_t deadline;

    if (argc != 4) {
        fprintf(stderr, "usage: %s SOCKET PORT STOP_FILE\n", argv[0]);
        return 2;
    }
    parsed_port = strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || parsed_port > UINT32_MAX) {
        return 2;
    }

    device = (spw_device_config_t)SPW_DEVICE_CONFIG_INITIALIZER((uint32_t)parsed_port);
    if (snprintf(device.endpoint, sizeof(device.endpoint), "%s", argv[1]) < 0 ||
        strlen(argv[1]) >= sizeof(device.endpoint)) {
        return 2;
    }
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    if (spw_port_open(&config, &port) != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK) {
        if (port != NULL) {
            (void)spw_port_close(port);
        }
        return 1;
    }

    deadline = monotonic_ms() + 5000;
    while (monotonic_ms() < deadline) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) == SPW_OK &&
            state == SPW_LINK_RUN) {
            printf("RUN\n");
            fflush(stdout);
            break;
        }
        sleep_ms(20);
    }
    {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK ||
            state != SPW_LINK_RUN) {
            (void)spw_port_close(port);
            return 1;
        }
    }

    deadline = monotonic_ms() + 20000;
    while (access(argv[3], F_OK) != 0 && monotonic_ms() < deadline) {
        sleep_ms(20);
    }
    if (access(argv[3], F_OK) != 0) {
        (void)spw_port_close(port);
        return 1;
    }

    return spw_port_close(port) == SPW_OK ? 0 : 1;
}
