// SPDX-License-Identifier: Apache-2.0

#include "tools/vspd_management_client.h"
#include "vspwd/server.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s [--socket PATH] list\n"
            "       %s [--socket PATH] show PORT\n"
            "       %s [--socket PATH] stats PORT\n"
            "       %s [--socket PATH] clear-stats PORT\n",
            program, program, program, program);
}

static const char* state_name(uint32_t state) {
    switch (state) {
        case VSPD_LINK_ERROR_RESET: return "ERROR_RESET";
        case VSPD_LINK_ERROR_WAIT: return "ERROR_WAIT";
        case VSPD_LINK_READY: return "READY";
        case VSPD_LINK_STARTED: return "STARTED";
        case VSPD_LINK_CONNECTING: return "CONNECTING";
        case VSPD_LINK_RUN: return "RUN";
        default: return "UNKNOWN";
    }
}

static const char* yes_no(uint32_t flags, uint32_t bit) {
    return (flags & bit) != 0u ? "yes" : "no";
}

static const char* status_name(int32_t status) {
    switch (status) {
        case VSPD_STATUS_OK: return "ok";
        case VSPD_STATUS_INVALID_ARGUMENT: return "invalid argument";
        case VSPD_STATUS_INVALID_STATE: return "invalid state";
        case VSPD_STATUS_TIMEOUT: return "timeout";
        case VSPD_STATUS_UNSUPPORTED: return "unsupported";
        case VSPD_STATUS_RESOURCE_EXHAUSTED: return "resource exhausted";
        case VSPD_STATUS_LINK_UNAVAILABLE: return "link unavailable";
        case VSPD_STATUS_BUFFER_TOO_SMALL: return "buffer too small";
        case VSPD_STATUS_INVALID_PACKET: return "invalid packet";
        case VSPD_STATUS_BACKEND: return "daemon/transport error";
        default: return "unknown error";
    }
}

static int fail_status(const char* operation, int32_t status) {
    fprintf(stderr, "spwctl: %s: %s (%" PRId32 ")\n",
            operation, status_name(status), status);
    return 1;
}

static int parse_port(const char* text, uint32_t* out_port) {
    char* end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return 0;
    }
    *out_port = (uint32_t)value;
    return 1;
}

static int command_list(vspd_management_client_t* client) {
    vspd_server_info_payload_t server;
    uint32_t port_id;
    int32_t status = vspd_management_get_server_info(client, &server);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get server info", status);
    }
    printf("PORT BRIDGED ATTACHED STARTED RESET STATE PACKETS TIMECODES\n");
    for (port_id = 0u; port_id < server.port_count; ++port_id) {
        vspd_port_info_payload_t info;
        status = vspd_management_get_port_info(client, port_id, &info);
        if (status != VSPD_STATUS_OK) {
            return fail_status("get port info", status);
        }
        printf("%" PRIu32 " %s %s %s %s %s %" PRIu32 "/%" PRIu32
               " %" PRIu32 "/%" PRIu32 "\n",
               port_id,
               yes_no(info.flags, VSPD_PORT_INFO_BRIDGED),
               yes_no(info.flags, VSPD_PORT_INFO_ATTACHED),
               yes_no(info.flags, VSPD_PORT_INFO_STARTED),
               yes_no(info.flags, VSPD_PORT_INFO_RESET_LATCHED),
               state_name(info.link_state),
               info.packet_queue_count,
               server.packet_queue_depth,
               info.time_code_queue_count,
               server.time_code_queue_depth);
    }
    return 0;
}

static int command_show(vspd_management_client_t* client, uint32_t port_id) {
    vspd_server_info_payload_t server;
    vspd_port_info_payload_t info;
    int32_t status = vspd_management_get_server_info(client, &server);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get server info", status);
    }
    status = vspd_management_get_port_info(client, port_id, &info);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get port info", status);
    }
    printf("port: %" PRIu32 "\n", port_id);
    printf("bridged: %s\n", yes_no(info.flags, VSPD_PORT_INFO_BRIDGED));
    printf("attached: %s\n", yes_no(info.flags, VSPD_PORT_INFO_ATTACHED));
    printf("started: %s\n", yes_no(info.flags, VSPD_PORT_INFO_STARTED));
    printf("reset_latched: %s\n",
           yes_no(info.flags, VSPD_PORT_INFO_RESET_LATCHED));
    printf("ever_attached: %s\n",
           yes_no(info.flags, VSPD_PORT_INFO_EVER_ATTACHED));
    printf("state: %s\n", state_name(info.link_state));
    printf("packet_queue: %" PRIu32 "/%" PRIu32 "\n",
           info.packet_queue_count, server.packet_queue_depth);
    printf("time_code_queue: %" PRIu32 "/%" PRIu32 "\n",
           info.time_code_queue_count, server.time_code_queue_depth);
    printf("max_logical_packet: %" PRIu32 "\n", server.max_logical_packet);
    return 0;
}

static int command_stats(vspd_management_client_t* client, uint32_t port_id) {
    vspd_statistics_payload_t stats;
    int32_t status = vspd_management_get_port_statistics(client, port_id, &stats);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get port statistics", status);
    }
    printf("port: %" PRIu32 "\n", port_id);
    printf("tx_packets: %" PRIu64 "\n", stats.tx_packets);
    printf("rx_packets: %" PRIu64 "\n", stats.rx_packets);
    printf("tx_bytes: %" PRIu64 "\n", stats.tx_bytes);
    printf("rx_bytes: %" PRIu64 "\n", stats.rx_bytes);
    printf("tx_time_codes: %" PRIu64 "\n", stats.tx_time_codes);
    printf("rx_time_codes: %" PRIu64 "\n", stats.rx_time_codes);
    printf("eep_packets: %" PRIu64 "\n", stats.eep_packets);
    printf("link_errors: %" PRIu64 "\n", stats.link_errors);
    printf("dropped_packets: %" PRIu64 "\n", stats.dropped_packets);
    return 0;
}

int main(int argc, char** argv) {
    const char* socket_path = VSPWD_DEFAULT_SOCKET_PATH;
    const char* command;
    uint32_t port_id = 0u;
    vspd_management_client_t client;
    int32_t status;
    int argument = 1;
    int result;

    if (argument < argc && strcmp(argv[argument], "--socket") == 0) {
        if (argument + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }
        socket_path = argv[argument + 1];
        argument += 2;
    }
    if (argument < argc &&
        (strcmp(argv[argument], "--help") == 0 ||
         strcmp(argv[argument], "-h") == 0)) {
        usage(argv[0]);
        return 0;
    }
    if (argument >= argc) {
        usage(argv[0]);
        return 2;
    }
    command = argv[argument++];

    if (strcmp(command, "list") == 0) {
        if (argument != argc) {
            usage(argv[0]);
            return 2;
        }
    } else if (strcmp(command, "show") == 0 ||
               strcmp(command, "stats") == 0 ||
               strcmp(command, "clear-stats") == 0) {
        if (argument + 1 != argc || !parse_port(argv[argument], &port_id)) {
            usage(argv[0]);
            return 2;
        }
    } else {
        usage(argv[0]);
        return 2;
    }

    status = vspd_management_open(&client, socket_path);
    if (status != VSPD_STATUS_OK) {
        return fail_status("connect", status);
    }

    if (strcmp(command, "list") == 0) {
        result = command_list(&client);
    } else if (strcmp(command, "show") == 0) {
        result = command_show(&client, port_id);
    } else if (strcmp(command, "stats") == 0) {
        result = command_stats(&client, port_id);
    } else {
        status = vspd_management_clear_port_statistics(&client, port_id);
        if (status != VSPD_STATUS_OK) {
            result = fail_status("clear port statistics", status);
        } else {
            printf("cleared statistics for port %" PRIu32 "\n", port_id);
            result = 0;
        }
    }

    vspd_management_close(&client);
    return result;
}
