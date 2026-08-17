from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing marker in {path}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


def append_once(path, marker, block):
    p = Path(path)
    text = p.read_text()
    if marker in text:
        return
    p.write_text(text + block)

# ---------------------------------------------------------------------------
# Private VSPD 1.3 bridge marker.
# ---------------------------------------------------------------------------
replace_once(
    "src/backends/device/vspw_device_protocol.h",
    "#define VSPD_VERSION_MINOR 2u",
    "#define VSPD_VERSION_MINOR 3u")
replace_once(
    "src/backends/device/vspw_device_protocol.h",
    "#define VSPD_PORT_INFO_EVER_ATTACHED UINT32_C(0x08)\n#define VSPD_PORT_INFO_KNOWN_MASK    UINT32_C(0x0f)",
    "#define VSPD_PORT_INFO_EVER_ATTACHED UINT32_C(0x08)\n#define VSPD_PORT_INFO_BRIDGED       UINT32_C(0x10)\n#define VSPD_PORT_INFO_KNOWN_MASK    UINT32_C(0x1f)")
replace_once(
    "tests/vspw_device_protocol.c",
    "0x01u, 0x02u, 0x09u, 0x0eu,",
    "0x01u, 0x03u, 0x09u, 0x0eu,")

# ---------------------------------------------------------------------------
# vspwd private configuration surface.
# ---------------------------------------------------------------------------
Path("src/vspwd/server.h").write_text(r'''// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPWD_SERVER_H
#define SPWKIT_VSPWD_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#include <spwkit/udp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VSPWD_DEFAULT_SOCKET_PATH "/tmp/spwkit-vspwd.sock"

typedef struct vspwd_udp_bridge_config {
    bool enabled;
    uint32_t port_id;
    spw_udp_config_t udp;
} vspwd_udp_bridge_config_t;

typedef struct vspwd_config {
    const char* socket_path;
    vspwd_udp_bridge_config_t udp_bridge;
} vspwd_config_t;

#define VSPWD_UDP_BRIDGE_CONFIG_INITIALIZER \
    { false, 0u, SPW_UDP_CONFIG_INITIALIZER(0u, 0u, 42u) }
#define VSPWD_CONFIG_INITIALIZER \
    { VSPWD_DEFAULT_SOCKET_PATH, VSPWD_UDP_BRIDGE_CONFIG_INITIALIZER }

/* Run until SIGINT/SIGTERM or a fatal server error. Returns 0 on clean stop. */
int vspwd_run_config(const vspwd_config_t* config);
int vspwd_run(const char* socket_path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPWD_SERVER_H */
''')

Path("src/vspwd/main.c").write_text(r'''// SPDX-License-Identifier: Apache-2.0

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
            "  --udp-link-id ID           default 42\n",
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
''')

# ---------------------------------------------------------------------------
# vspwd core bridge integration.
# ---------------------------------------------------------------------------
replace_once(
    "src/vspwd/server.c",
    '#include "backends/device/vspw_device_protocol.h"\n#include "spwkit/types.h"',
    '#include "backends/device/vspw_device_protocol.h"\n#include "spwkit/config.h"\n#include "spwkit/port.h"\n#include "spwkit/types.h"\n#include "spwkit/udp.h"')
replace_once(
    "src/vspwd/server.c",
    "    bool ever_attached;\n    spw_link_state_t state;",
    "    bool ever_attached;\n    bool bridged;\n    spw_link_state_t state;")
replace_once(
    "src/vspwd/server.c",
    "typedef struct vspwd_server {\n    int listener_fd;",
    "typedef struct vspwd_udp_bridge {\n    bool enabled;\n    int port_id;\n    spw_port_t* udp_port;\n    spw_link_state_t state;\n    uint8_t rx_packet[VSPD_MAX_LOGICAL_PACKET];\n} vspwd_udp_bridge_t;\n\ntypedef struct vspwd_server {\n    int listener_fd;")
replace_once(
    "src/vspwd/server.c",
    "    vspwd_client_t clients[VSPWD_CLIENT_COUNT];\n    vspwd_port_t ports[VSPWD_PORT_COUNT];\n} vspwd_server_t;",
    "    vspwd_client_t clients[VSPWD_CLIENT_COUNT];\n    vspwd_port_t ports[VSPWD_PORT_COUNT];\n    vspwd_udp_bridge_t bridge;\n} vspwd_server_t;")

old_calc = '''static spw_link_state_t vspwd_calculate_state(const vspwd_server_t* server,
                                               int port_id) {
    const vspwd_port_t* port = &server->ports[port_id];
    const vspwd_port_t* peer = &server->ports[vspwd_peer_port(port_id)];

    if (port->client_index < 0) {
        return SPW_LINK_ERROR_RESET;
    }
    if (port->reset_latched) {
        return SPW_LINK_ERROR_RESET;
    }
    if (!port->started) {
        return SPW_LINK_READY;
    }
    if (peer->client_index >= 0 && peer->started && !peer->reset_latched) {
        return SPW_LINK_RUN;
    }
    if (peer->client_index >= 0) {
        return SPW_LINK_CONNECTING;
    }
    return peer->ever_attached ? SPW_LINK_ERROR_WAIT : SPW_LINK_CONNECTING;
}'''
new_calc = '''static spw_link_state_t vspwd_calculate_state(const vspwd_server_t* server,
                                               int port_id) {
    const vspwd_port_t* port = &server->ports[port_id];
    const vspwd_port_t* peer = &server->ports[vspwd_peer_port(port_id)];

    if (port->bridged) {
        return server->bridge.state;
    }
    if (port->client_index < 0) {
        return SPW_LINK_ERROR_RESET;
    }
    if (port->reset_latched) {
        return SPW_LINK_ERROR_RESET;
    }
    if (!port->started) {
        return SPW_LINK_READY;
    }
    if (peer->bridged) {
        if (peer->state == SPW_LINK_RUN) {
            return SPW_LINK_RUN;
        }
        if (peer->state == SPW_LINK_ERROR_WAIT) {
            return SPW_LINK_ERROR_WAIT;
        }
        return SPW_LINK_CONNECTING;
    }
    if (peer->client_index >= 0 && peer->started && !peer->reset_latched) {
        return SPW_LINK_RUN;
    }
    if (peer->client_index >= 0) {
        return SPW_LINK_CONNECTING;
    }
    return peer->ever_attached ? SPW_LINK_ERROR_WAIT : SPW_LINK_CONNECTING;
}'''
replace_once("src/vspwd/server.c", old_calc, new_calc)
replace_once(
    "src/vspwd/server.c",
    "        if (port->client_index < 0) {\n            port->state = SPW_LINK_ERROR_RESET;",
    "        if (port->client_index < 0 && !port->bridged) {\n            port->state = SPW_LINK_ERROR_RESET;")
replace_once(
    "src/vspwd/server.c",
    "    server->listener_fd = -1;",
    "    server->listener_fd = -1;\n    server->bridge.port_id = -1;\n    server->bridge.udp_port = NULL;\n    server->bridge.state = SPW_LINK_ERROR_RESET;")
replace_once(
    "src/vspwd/server.c",
    "    if (port->ever_attached) {\n        info->flags |= VSPD_PORT_INFO_EVER_ATTACHED;\n    }",
    "    if (port->ever_attached) {\n        info->flags |= VSPD_PORT_INFO_EVER_ATTACHED;\n    }\n    if (port->bridged) {\n        info->flags |= VSPD_PORT_INFO_BRIDGED;\n    }")
replace_once(
    "src/vspwd/server.c",
    "            } else if (port_id < 0 || port_id >= VSPWD_PORT_COUNT) {\n                status = VSPD_STATUS_INVALID_ARGUMENT;\n            } else if (server->ports[port_id].client_index >= 0) {",
    "            } else if (port_id < 0 || port_id >= VSPWD_PORT_COUNT) {\n                status = VSPD_STATUS_INVALID_ARGUMENT;\n            } else if (server->ports[port_id].bridged) {\n                status = VSPD_STATUS_RESOURCE_EXHAUSTED;\n            } else if (server->ports[port_id].client_index >= 0) {")

bridge_code = r'''

static void vspwd_bridge_refresh_state(vspwd_server_t* server) {
    spw_link_state_t state = SPW_LINK_ERROR_WAIT;
    spw_result_t result;
    if (!server->bridge.enabled || server->bridge.udp_port == NULL) {
        return;
    }
    result = spw_port_get_link_state(server->bridge.udp_port, &state);
    if (result != SPW_OK) {
        state = SPW_LINK_ERROR_WAIT;
    }
    if (state != server->bridge.state) {
        server->bridge.state = state;
        vspwd_update_states(server);
    }
}

static void vspwd_bridge_pop_packet(vspwd_server_t* server) {
    vspwd_port_t* bridge_port = &server->ports[server->bridge.port_id];
    vspwd_packet_slot_t* slot;
    spw_packet_t packet;
    spw_result_t result;
    if (bridge_port->packets.count == 0u || server->bridge.state != SPW_LINK_RUN) {
        return;
    }
    slot = &bridge_port->packets.slots[bridge_port->packets.head];
    packet.data = slot->data;
    packet.length = slot->length;
    packet.capacity = slot->length;
    packet.terminator = slot->terminator;
    result = spw_port_send(server->bridge.udp_port, &packet, SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }
    ++bridge_port->statistics.rx_packets;
    bridge_port->statistics.rx_bytes += slot->length;
    bridge_port->packets.head =
        (bridge_port->packets.head + 1u) % VSPWD_PACKET_QUEUE_DEPTH;
    --bridge_port->packets.count;
    vspwd_mark_port_changed(server, server->bridge.port_id);
}

static void vspwd_bridge_pop_time_code(vspwd_server_t* server) {
    vspwd_port_t* bridge_port = &server->ports[server->bridge.port_id];
    const spw_time_code_t* time_code;
    spw_result_t result;
    if (bridge_port->time_codes.count == 0u || server->bridge.state != SPW_LINK_RUN) {
        return;
    }
    time_code = &bridge_port->time_codes.slots[bridge_port->time_codes.head];
    result = spw_port_send_time_code(server->bridge.udp_port,
                                     time_code,
                                     SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }
    ++bridge_port->statistics.rx_time_codes;
    bridge_port->time_codes.head =
        (bridge_port->time_codes.head + 1u) % VSPWD_TIME_CODE_QUEUE_DEPTH;
    --bridge_port->time_codes.count;
    vspwd_mark_port_changed(server, server->bridge.port_id);
}

static void vspwd_bridge_push_packet(vspwd_server_t* server) {
    const int bridge_id = server->bridge.port_id;
    const int local_id = vspwd_peer_port(bridge_id);
    vspwd_port_t* bridge_port = &server->ports[bridge_id];
    vspwd_port_t* local_port = &server->ports[local_id];
    vspwd_packet_slot_t* slot;
    spw_packet_t packet;
    spw_result_t result;

    if (server->bridge.state != SPW_LINK_RUN || local_port->state != SPW_LINK_RUN ||
        local_port->packets.count >= VSPWD_PACKET_QUEUE_DEPTH) {
        return;
    }
    packet.data = server->bridge.rx_packet;
    packet.length = 0u;
    packet.capacity = sizeof(server->bridge.rx_packet);
    packet.terminator = SPW_TERMINATOR_EOP;
    result = spw_port_receive(server->bridge.udp_port, &packet, SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }

    slot = &local_port->packets.slots[local_port->packets.tail];
    if (packet.length != 0u) {
        memcpy(slot->data, packet.data, packet.length);
    }
    slot->length = (uint32_t)packet.length;
    slot->offset = 0u;
    slot->terminator = packet.terminator;
    slot->message_id = vspwd_next_message_id(local_port);
    local_port->packets.tail =
        (local_port->packets.tail + 1u) % VSPWD_PACKET_QUEUE_DEPTH;
    ++local_port->packets.count;
    ++bridge_port->statistics.tx_packets;
    bridge_port->statistics.tx_bytes += packet.length;
    if (packet.terminator == SPW_TERMINATOR_EEP) {
        ++bridge_port->statistics.eep_packets;
    }
    vspwd_mark_port_changed(server, bridge_id);
    vspwd_mark_port_changed(server, local_id);
}

static void vspwd_bridge_push_time_code(vspwd_server_t* server) {
    const int bridge_id = server->bridge.port_id;
    const int local_id = vspwd_peer_port(bridge_id);
    vspwd_port_t* bridge_port = &server->ports[bridge_id];
    vspwd_port_t* local_port = &server->ports[local_id];
    spw_time_code_t time_code;
    spw_result_t result;

    if (server->bridge.state != SPW_LINK_RUN || local_port->state != SPW_LINK_RUN ||
        local_port->time_codes.count >= VSPWD_TIME_CODE_QUEUE_DEPTH) {
        return;
    }
    result = spw_port_receive_time_code(server->bridge.udp_port,
                                        &time_code,
                                        SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }
    local_port->time_codes.slots[local_port->time_codes.tail] = time_code;
    local_port->time_codes.tail =
        (local_port->time_codes.tail + 1u) % VSPWD_TIME_CODE_QUEUE_DEPTH;
    ++local_port->time_codes.count;
    ++bridge_port->statistics.tx_time_codes;
    vspwd_mark_port_changed(server, bridge_id);
    vspwd_mark_port_changed(server, local_id);
}

static void vspwd_service_bridge(vspwd_server_t* server) {
    if (!server->bridge.enabled) {
        return;
    }
    vspwd_bridge_refresh_state(server);
    if (server->bridge.state != SPW_LINK_RUN) {
        return;
    }
    vspwd_bridge_pop_packet(server);
    vspwd_bridge_pop_time_code(server);
    vspwd_bridge_push_packet(server);
    vspwd_bridge_push_time_code(server);
    vspwd_bridge_refresh_state(server);
}

static int vspwd_open_bridge(vspwd_server_t* server,
                             const vspwd_udp_bridge_config_t* config) {
    spw_port_config_t port_config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    spw_result_t result;
    vspwd_port_t* port;
    if (!config->enabled) {
        return 0;
    }
    if (config->port_id >= VSPWD_PORT_COUNT) {
        return -1;
    }
    port_config.backend_config = &config->udp;
    port_config.backend_config_size = sizeof(config->udp);
    result = spw_port_open(&port_config, &server->bridge.udp_port);
    if (result != SPW_OK || server->bridge.udp_port == NULL) {
        fprintf(stderr, "vspwd: failed to open UDP bridge backend: %d\n", (int)result);
        return -1;
    }
    result = spw_port_start(server->bridge.udp_port);
    if (result != SPW_OK) {
        fprintf(stderr, "vspwd: failed to start UDP bridge backend: %d\n", (int)result);
        (void)spw_port_close(server->bridge.udp_port);
        server->bridge.udp_port = NULL;
        return -1;
    }
    server->bridge.enabled = true;
    server->bridge.port_id = (int)config->port_id;
    server->bridge.state = SPW_LINK_CONNECTING;
    port = &server->ports[server->bridge.port_id];
    port->bridged = true;
    port->started = true;
    port->reset_latched = false;
    port->state = SPW_LINK_CONNECTING;
    port->next_message_id = 1u;
    vspwd_bridge_refresh_state(server);
    return 0;
}
'''
replace_once(
    "src/vspwd/server.c",
    "static bool vspwd_handle_request(vspwd_server_t* server,",
    bridge_code + "\nstatic bool vspwd_handle_request(vspwd_server_t* server,")

replace_once(
    "src/vspwd/server.c",
    "static void vspwd_cleanup(vspwd_server_t* server) {\n    int i;",
    "static void vspwd_cleanup(vspwd_server_t* server) {\n    int i;\n    if (server->bridge.udp_port != NULL) {\n        (void)spw_port_close(server->bridge.udp_port);\n        server->bridge.udp_port = NULL;\n    }")

old_run = '''int vspwd_run(const char* socket_path) {
    vspwd_server_t* server = (vspwd_server_t*)calloc(1u, sizeof(vspwd_server_t));
    struct sigaction action;
    int result = 0;

    if (server == NULL) {
        return 1;
    }
    vspwd_init_server(server);
    if (vspwd_open_listener(server, socket_path) != 0) {'''
new_run = '''int vspwd_run_config(const vspwd_config_t* config) {
    vspwd_server_t* server;
    struct sigaction action;
    int result = 0;

    if (config == NULL || config->socket_path == NULL) {
        return 1;
    }
    server = (vspwd_server_t*)calloc(1u, sizeof(vspwd_server_t));
    if (server == NULL) {
        return 1;
    }
    vspwd_init_server(server);
    if (vspwd_open_bridge(server, &config->udp_bridge) != 0) {
        vspwd_cleanup(server);
        free(server);
        return 1;
    }
    if (vspwd_open_listener(server, config->socket_path) != 0) {'''
replace_once("src/vspwd/server.c", old_run, new_run)
replace_once(
    "src/vspwd/server.c",
    "    while (!vspwd_stop_requested) {\n        struct pollfd descriptors[1 + VSPWD_CLIENT_COUNT];",
    "    while (!vspwd_stop_requested) {\n        struct pollfd descriptors[1 + VSPWD_CLIENT_COUNT];\n        vspwd_service_bridge(server);")
replace_once(
    "src/vspwd/server.c",
    "        poll_result = poll(descriptors, count, 100);",
    "        poll_result = poll(descriptors, count, server->bridge.enabled ? 10 : 100);")
replace_once(
    "src/vspwd/server.c",
    "    vspwd_cleanup(server);\n    free(server);\n    return result;\n}",
    "    vspwd_cleanup(server);\n    free(server);\n    return result;\n}\n\nint vspwd_run(const char* socket_path) {\n    vspwd_config_t config = VSPWD_CONFIG_INITIALIZER;\n    config.socket_path = socket_path;\n    return vspwd_run_config(&config);\n}")

# ---------------------------------------------------------------------------
# Management visibility.
# ---------------------------------------------------------------------------
replace_once(
    "src/tools/spwctl.c",
    'printf("PORT ATTACHED STARTED RESET STATE PACKETS TIMECODES\\n");',
    'printf("PORT BRIDGED ATTACHED STARTED RESET STATE PACKETS TIMECODES\\n");')
replace_once(
    "src/tools/spwctl.c",
    'printf("%" PRIu32 " %s %s %s %s %" PRIu32 "/%" PRIu32\n               " %" PRIu32 "/%" PRIu32 "\\n",\n               port_id,\n               yes_no(info.flags, VSPD_PORT_INFO_ATTACHED),',
    'printf("%" PRIu32 " %s %s %s %s %s %" PRIu32 "/%" PRIu32\n               " %" PRIu32 "/%" PRIu32 "\\n",\n               port_id,\n               yes_no(info.flags, VSPD_PORT_INFO_BRIDGED),\n               yes_no(info.flags, VSPD_PORT_INFO_ATTACHED),')
replace_once(
    "src/tools/spwctl.c",
    'printf("attached: %s\\n", yes_no(info.flags, VSPD_PORT_INFO_ATTACHED));',
    'printf("bridged: %s\\n", yes_no(info.flags, VSPD_PORT_INFO_BRIDGED));\n    printf("attached: %s\\n", yes_no(info.flags, VSPD_PORT_INFO_ATTACHED));')

replace_once(
    "src/tools/spwmon.c",
    '",\\\"attached\\\":%s,\\\"started\\\":%s,\\\"reset_latched\\\":%s"',
    '",\\\"bridged\\\":%s,\\\"attached\\\":%s,\\\"started\\\":%s,\\\"reset_latched\\\":%s"')
replace_once(
    "src/tools/spwmon.c",
    "               port_id,\n               json_bool(snapshot->info.flags, VSPD_PORT_INFO_ATTACHED),",
    "               port_id,\n               json_bool(snapshot->info.flags, VSPD_PORT_INFO_BRIDGED),\n               json_bool(snapshot->info.flags, VSPD_PORT_INFO_ATTACHED),")
replace_once(
    "src/tools/spwmon.c",
    'printf("%s port=%" PRIu32 " state=%s attached=%s started=%s "',
    'printf("%s port=%" PRIu32 " state=%s bridged=%s attached=%s started=%s "')
replace_once(
    "src/tools/spwmon.c",
    "               state_name(snapshot->info.link_state),\n               (snapshot->info.flags & VSPD_PORT_INFO_ATTACHED) != 0u ? \"yes\" : \"no\",",
    "               state_name(snapshot->info.link_state),\n               (snapshot->info.flags & VSPD_PORT_INFO_BRIDGED) != 0u ? \"yes\" : \"no\",\n               (snapshot->info.flags & VSPD_PORT_INFO_ATTACHED) != 0u ? \"yes\" : \"no\",")

# ---------------------------------------------------------------------------
# Device-side survivor fixture compatible with examples/distributed/udp_peer.c.
# ---------------------------------------------------------------------------
Path("tests/device_bridge_peer.c").write_text(r'''// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/device.h>
#include <spwkit/port.h>
#include <spwkit/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const spw_timeout_us_t IO_TIMEOUT_US = 5000000u;
static const unsigned STATE_TIMEOUT_MS = 15000u;
static const size_t PAYLOAD_SIZE = 8192u;

static void sleep_ms(unsigned milliseconds) {
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static int wait_for_state(spw_port_t* port, spw_link_state_t expected) {
    const unsigned attempts = STATE_TIMEOUT_MS / 10u + 1u;
    unsigned i;
    for (i = 0u; i < attempts; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK) {
            return 0;
        }
        if (state == expected) {
            return 1;
        }
        sleep_ms(10u);
    }
    return 0;
}

static uint8_t pattern_byte(char id, unsigned round, size_t index) {
    const unsigned base = id == 'A' ? 0x31u : 0x97u;
    return (uint8_t)((base + round * 17u + (unsigned)(index * 13u)) & 0xffu);
}

static void fill_payload(uint8_t* payload, char id, unsigned round) {
    size_t i;
    for (i = 0u; i < PAYLOAD_SIZE; ++i) {
        payload[i] = pattern_byte(id, round, i);
    }
}

static int verify_payload(const uint8_t* payload, char id, unsigned round) {
    size_t i;
    for (i = 0u; i < PAYLOAD_SIZE; ++i) {
        if (payload[i] != pattern_byte(id, round, i)) {
            return 0;
        }
    }
    return 1;
}

static int exchange_round(spw_port_t* port, unsigned round) {
    uint8_t tx_payload[8192];
    uint8_t rx_payload[8192];
    spw_packet_t tx;
    spw_packet_t rx;
    spw_time_code_t tx_time;
    spw_time_code_t rx_time;
    spw_result_t result;

    fill_payload(tx_payload, 'A', round);
    memset(rx_payload, 0, sizeof(rx_payload));
    tx.data = tx_payload;
    tx.length = PAYLOAD_SIZE;
    tx.capacity = PAYLOAD_SIZE;
    tx.terminator = SPW_TERMINATOR_EOP;
    result = spw_port_send(port, &tx, IO_TIMEOUT_US);
    if (result != SPW_OK) {
        fprintf(stderr, "device round %u send failed: %d\n", round, (int)result);
        return 0;
    }

    rx.data = rx_payload;
    rx.length = 0u;
    rx.capacity = PAYLOAD_SIZE;
    rx.terminator = SPW_TERMINATOR_EOP;
    result = spw_port_receive(port, &rx, IO_TIMEOUT_US);
    if (result != SPW_OK || rx.length != PAYLOAD_SIZE ||
        rx.terminator != SPW_TERMINATOR_EEP ||
        !verify_payload(rx_payload, 'B', round)) {
        fprintf(stderr, "device round %u receive mismatch result=%d\n", round, (int)result);
        return 0;
    }

    tx_time.time_count = (uint8_t)((round * 10u + 1u) & 0x3fu);
    tx_time.control_flags = 0u;
    if (spw_port_send_time_code(port, &tx_time, IO_TIMEOUT_US) != SPW_OK) {
        return 0;
    }
    if (spw_port_receive_time_code(port, &rx_time, IO_TIMEOUT_US) != SPW_OK ||
        rx_time.time_count != (uint8_t)((round * 10u + 2u) & 0x3fu) ||
        rx_time.control_flags != 0u) {
        return 0;
    }
    printf("ROUND %u OK device\n", round);
    return 1;
}

int main(int argc, char** argv) {
    spw_device_config_t device;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    unsigned long parsed_port;
    char* end = NULL;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET PORT\n", argv[0]);
        return 2;
    }
    parsed_port = strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || parsed_port > UINT32_MAX) {
        return 2;
    }
    device = (spw_device_config_t)SPW_DEVICE_CONFIG_INITIALIZER((uint32_t)parsed_port);
    if (snprintf(device.endpoint, sizeof(device.endpoint), "%s", argv[1]) >=
        (int)sizeof(device.endpoint)) {
        return 2;
    }
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);
    if (spw_port_open(&config, &port) != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK) {
        return 1;
    }
    if (!wait_for_state(port, SPW_LINK_RUN) || !exchange_round(port, 1u)) {
        (void)spw_port_close(port);
        return 1;
    }
    if (!wait_for_state(port, SPW_LINK_ERROR_WAIT)) {
        fprintf(stderr, "device did not observe remote peer loss\n");
        (void)spw_port_close(port);
        return 1;
    }
    printf("PEER_LOST\n");
    if (!wait_for_state(port, SPW_LINK_RUN)) {
        fprintf(stderr, "device did not recover remote peer\n");
        (void)spw_port_close(port);
        return 1;
    }
    printf("PEER_RECOVERED\n");
    if (!exchange_round(port, 2u)) {
        (void)spw_port_close(port);
        return 1;
    }
    (void)spw_port_close(port);
    printf("PASS device survivor\n");
    return 0;
}
''')

Path("tests/device/run_udp_bridge.sh").write_text(r'''#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
  echo "usage: $0 VSPWD DEVICE_PEER UDP_PEER SPWCTL DEVICE_EXAMPLE" >&2
  exit 2
fi

vspwd="$1"
device_peer="$2"
udp_peer="$3"
spwctl="$4"
device_example="$5"
tmpdir="$(mktemp -d)"
socket="$tmpdir/vspwd.sock"
base=$((42000 + ($$ % 1000) * 2))
bridge_udp_port="$base"
remote_udp_port=$((base + 1))
daemon_pid=""
device_pid=""

cleanup() {
  set +e
  [[ -n "$device_pid" ]] && kill "$device_pid" 2>/dev/null || true
  [[ -n "$device_pid" ]] && wait "$device_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && kill -TERM "$daemon_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && wait "$daemon_pid" 2>/dev/null || true
  rm -rf "$tmpdir"
}
trap cleanup EXIT

"$vspwd" --socket "$socket" \
  --bridge-port 1 \
  --udp-local-port "$bridge_udp_port" \
  --udp-remote-port "$remote_udp_port" \
  --udp-link-id 4242 >"$tmpdir/vspwd.log" 2>&1 &
daemon_pid=$!
for _ in $(seq 1 200); do
  [[ -S "$socket" ]] && break
  sleep 0.02
done
[[ -S "$socket" ]] || { cat "$tmpdir/vspwd.log"; exit 1; }

"$spwctl" --socket "$socket" list >"$tmpdir/list.log"
grep -Eq '^1[[:space:]]+yes[[:space:]]+no[[:space:]]+yes' "$tmpdir/list.log"
if timeout 3s "$device_example" "$socket" 1 >"$tmpdir/reserved.log" 2>&1; then
  echo "bridged port unexpectedly accepted a normal device attachment" >&2
  cat "$tmpdir/reserved.log"
  exit 1
fi

"$device_peer" "$socket" 0 >"$tmpdir/device.log" 2>&1 &
device_pid=$!
sleep 0.1

timeout 20s "$udp_peer" \
  --id B \
  --local-port "$remote_udp_port" \
  --remote-port "$bridge_udp_port" \
  --link-id 4242 \
  --scenario initial >"$tmpdir/udp-initial.log" 2>&1 || {
    cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-initial.log"
    exit 1
  }

for _ in $(seq 1 500); do
  grep -q '^PEER_LOST$' "$tmpdir/device.log" && break
  sleep 0.02
done
grep -q '^PEER_LOST$' "$tmpdir/device.log" || {
  cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-initial.log"
  exit 1
}

timeout 20s "$udp_peer" \
  --id B \
  --local-port "$remote_udp_port" \
  --remote-port "$bridge_udp_port" \
  --link-id 4242 \
  --scenario restart >"$tmpdir/udp-restart.log" 2>&1 || {
    cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-restart.log"
    exit 1
  }

wait "$device_pid" || {
  cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-initial.log" "$tmpdir/udp-restart.log"
  exit 1
}
device_pid=""
grep -q '^PEER_RECOVERED$' "$tmpdir/device.log"
grep -q '^PASS device survivor$' "$tmpdir/device.log"
grep -q '^PASS id=B scenario=initial$' "$tmpdir/udp-initial.log"
grep -q '^PASS id=B scenario=restart$' "$tmpdir/udp-restart.log"
''')
Path("tests/device/run_udp_bridge.sh").chmod(0o755)

# Add integration targets/tests.
replace_once(
    "tests/device/CMakeLists.txt",
    "if(SPWKIT_DEVICE_RUNTIME_SUPPORTED)\n    add_executable(spwkit_device_public_peer ../device_public_peer.c)",
    "if(SPWKIT_DEVICE_RUNTIME_SUPPORTED)\n    add_executable(spwkit_device_public_peer ../device_public_peer.c)")
insert_marker = "    if(SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)\n        add_executable(spwkit_device_hold_peer ../device_hold_peer.c)"
insert_block = '''    if(SPWKIT_UDP_RUNTIME_SUPPORTED AND SPWKIT_ENABLE_HEAP)\n        add_executable(spwkit_device_bridge_peer ../device_bridge_peer.c)\n        target_link_libraries(spwkit_device_bridge_peer PRIVATE spwkit::spwkit)\n        target_compile_features(spwkit_device_bridge_peer PRIVATE c_std_11)\n        set_target_properties(spwkit_device_bridge_peer PROPERTIES\n            C_STANDARD 11\n            C_STANDARD_REQUIRED YES\n            C_EXTENSIONS OFF)\n\n        add_executable(spwkit_bridge_udp_peer ../../examples/distributed/udp_peer.c)\n        target_link_libraries(spwkit_bridge_udp_peer PRIVATE spwkit::spwkit)\n        target_compile_features(spwkit_bridge_udp_peer PRIVATE c_std_11)\n        set_target_properties(spwkit_bridge_udp_peer PROPERTIES\n            C_STANDARD 11\n            C_STANDARD_REQUIRED YES\n            C_EXTENSIONS OFF)\n    endif()\n\n'''
replace_once("tests/device/CMakeLists.txt", insert_marker, insert_block + insert_marker)

test_marker = "    if(SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)\n        add_test(\n            NAME spwctl_management_non_owning"
test_block = '''    if(SPWKIT_UDP_RUNTIME_SUPPORTED AND SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)\n        add_test(\n            NAME vspwd_udp_bridge_remote_restart\n            COMMAND bash\n                    ${CMAKE_CURRENT_SOURCE_DIR}/run_udp_bridge.sh\n                    $<TARGET_FILE:vspwd>\n                    $<TARGET_FILE:spwkit_device_bridge_peer>\n                    $<TARGET_FILE:spwkit_bridge_udp_peer>\n                    $<TARGET_FILE:spwctl>\n                    $<TARGET_FILE:spwkit_example_c_device_peer>)\n        set_tests_properties(vspwd_udp_bridge_remote_restart PROPERTIES\n            LABELS \"device;bridge;transport;integration;process;restart;c\"\n            TIMEOUT 60)\n    endif()\n\n'''
replace_once("tests/device/CMakeLists.txt", test_marker, test_block + test_marker)

# ---------------------------------------------------------------------------
# Documentation.
# ---------------------------------------------------------------------------
for doc in ["docs/vspw-device-protocol.md"]:
    p = Path(doc)
    text = p.read_text().replace("VSPD v1.2", "VSPD v1.3").replace("VSPD 1.2", "VSPD 1.3")
    text = text.replace("version_minor | `2`", "version_minor | `3`").replace("byte 1  minor = 2", "byte 1  minor = 3")
    p.write_text(text)

append_once("docs/vspw-device-protocol.md", "### Bridged port flag", r'''

### Bridged port flag

VSPD 1.3 adds `VSPD_PORT_INFO_BRIDGED` to the existing fixed-size port-info flags. A bridged port is reserved by daemon topology and cannot be ATTACHed by an ordinary application client. The payload size does not change; management and monitor clients can distinguish a remote transport endpoint from a merely unattached local port.
''')

append_once("docs/vspwd.md", "## VSPW-TP/UDP bridge", r'''

## VSPW-TP/UDP bridge

`vspwd` can reserve either virtual port as a VSPW-TP/UDP endpoint while the opposite port remains a normal `SPW_BACKEND_DEVICE` application port.

```text
application -> SPW_BACKEND_DEVICE -> VSPD -> vspwd port 0
                                              |
                                              +-> port 1 [bridged]
                                                     |
                                                 VSPW-TP/UDP
                                                     |
                                               remote spw_port_*
```

Example:

```sh
vspwd --socket /tmp/vspwd.sock \
  --bridge-port 1 \
  --udp-local-port 46001 \
  --udp-remote-port 46002 \
  --udp-remote-address 127.0.0.1 \
  --udp-link-id 42
```

The bridge reuses the normal `SPW_BACKEND_UDP` implementation internally. `vspwd` does not contain a second VSPW-TP codec/reliability stack. The daemon services that cooperative backend from its event loop, forwards DATA and time codes through the existing bounded per-port queues, and projects the remote UDP link state onto the paired local VSPD port.

The bridged daemon port is topology-owned: ordinary VSPD ATTACH requests for that port are rejected. `spwctl` and `spwmon` expose the `bridged` flag so an operator can distinguish it from an unattached application port.

The v0.4 bridge deliberately supports one bridged endpoint in the two-port reference daemon. Arbitrary routing tables, multi-hop routing and hardware bridging are outside this slice.
''')

print("bridge patch applied")
