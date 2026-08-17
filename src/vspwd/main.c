// SPDX-License-Identifier: Apache-2.0

#include "vspwd/server.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s [--socket PATH] [UDP bridge options]\n"
            "\n"
            "Runs a two-port virtual SpaceWire daemon using VSPD over\n"
            "AF_UNIX/SOCK_SEQPACKET. Without bridge options, ports 0 and 1\n"
            "are equal local peers.\n"
            "\n"
            "UDP bridge options:\n"
            "  --bridge-port 0|1          reserve this daemon port for VSPW-TP/UDP\n"
            "  --udp-local-port PORT      required with --bridge-port\n"
            "  --udp-remote-port PORT     required with --bridge-port\n"
            "  --udp-local-address IPv4   default 127.0.0.1\n"
            "  --udp-remote-address IPv4  default 127.0.0.1\n"
            "  --udp-link-id ID           default 42\n"
            "  --udp-ack-timeout-ms MS    default 100\n"
            "  --udp-keepalive-ms MS      default 1000\n"
            "  --udp-peer-timeout-ms MS   default 3000\n",
            program);
}

static int parse_u32(const char* text, uint32_t* value) {
    char* end = NULL;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || text[0] == '\0' || end == NULL || *end != '\0' ||
        parsed > UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_port(const char* text, uint16_t* value) {
    uint32_t parsed = 0u;
    if (!parse_u32(text, &parsed) || parsed == 0u || parsed > UINT16_MAX) {
        return 0;
    }
    *value = (uint16_t)parsed;
    return 1;
}

static int copy_address(char* destination, size_t capacity, const char* source) {
    const int written = snprintf(destination, capacity, "%s", source);
    return written >= 0 && (size_t)written < capacity;
}

int main(int argc, char** argv) {
    vspwd_config_t config = VSPWD_CONFIG_INITIALIZER;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0) {
            if (++i >= argc) {
                usage(argv[0]);
                return 2;
            }
            config.socket_path = argv[i];
        } else if (strcmp(argv[i], "--bridge-port") == 0) {
            uint32_t port_id = 0u;
            if (++i >= argc || !parse_u32(argv[i], &port_id) || port_id > 1u) {
                usage(argv[0]);
                return 2;
            }
            config.udp_bridge.enabled = true;
            config.udp_bridge.port_id = port_id;
        } else if (strcmp(argv[i], "--udp-local-port") == 0) {
            if (++i >= argc || !parse_port(argv[i], &config.udp_bridge.udp.local_port)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-remote-port") == 0) {
            if (++i >= argc || !parse_port(argv[i], &config.udp_bridge.udp.remote_port)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-local-address") == 0) {
            if (++i >= argc || !copy_address(config.udp_bridge.udp.local_address,
                                              sizeof(config.udp_bridge.udp.local_address),
                                              argv[i])) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-remote-address") == 0) {
            if (++i >= argc || !copy_address(config.udp_bridge.udp.remote_address,
                                              sizeof(config.udp_bridge.udp.remote_address),
                                              argv[i])) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-link-id") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.link_id) ||
                config.udp_bridge.udp.link_id == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-ack-timeout-ms") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.ack_timeout_ms) ||
                config.udp_bridge.udp.ack_timeout_ms == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-keepalive-ms") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.keepalive_interval_ms) ||
                config.udp_bridge.udp.keepalive_interval_ms == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-peer-timeout-ms") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.peer_timeout_ms) ||
                config.udp_bridge.udp.peer_timeout_ms == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (config.udp_bridge.enabled &&
        (config.udp_bridge.udp.local_port == 0u ||
         config.udp_bridge.udp.remote_port == 0u)) {
        usage(argv[0]);
        return 2;
    }

    return vspwd_run_config(&config);
}
