// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include "tools/vspd_management_client.h"
#include "vspwd/server.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stop_requested = 0;

static void signal_handler(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s [--socket PATH] [--port PORT] [--count N] [--json]\n"
            "\n"
            "Continuously observes vspwd port metadata without ATTACHing to an\n"
            "application port. Without --port, all daemon ports are monitored.\n",
            program);
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
    fprintf(stderr,
            "spwmon: %s: %s (%" PRId32 ")\n",
            operation,
            status_name(status),
            status);
    return 1;
}

static int parse_u32(const char* text, uint32_t* out_value) {
    char* end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return 0;
    }
    *out_value = (uint32_t)value;
    return 1;
}

static int parse_u64(const char* text, uint64_t* out_value) {
    char* end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *out_value = (uint64_t)value;
    return 1;
}

static void timestamp_utc(char out[32]) {
    struct timespec now;
    struct tm utc;
    size_t length;
    unsigned int milliseconds;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
        gmtime_r(&now.tv_sec, &utc) == NULL) {
        memcpy(out, "unknown", sizeof("unknown"));
        return;
    }
    length = strftime(out, 32u, "%Y-%m-%dT%H:%M:%S", &utc);
    if (length == 0u || length + 5u >= 32u) {
        memcpy(out, "unknown", sizeof("unknown"));
        return;
    }
    milliseconds = (unsigned int)(now.tv_nsec / 1000000L);
    out[length++] = '.';
    out[length++] = (char)('0' + (milliseconds / 100u) % 10u);
    out[length++] = (char)('0' + (milliseconds / 10u) % 10u);
    out[length++] = (char)('0' + milliseconds % 10u);
    out[length++] = 'Z';
    out[length] = '\0';
}

static const char* json_bool(uint32_t flags, uint32_t bit) {
    return (flags & bit) != 0u ? "true" : "false";
}

static void print_snapshot(uint32_t port_id,
                           const vspd_port_snapshot_payload_t* snapshot,
                           const vspd_server_info_payload_t* server,
                           bool json) {
    char timestamp[32];
    timestamp_utc(timestamp);
    if (json) {
        printf("{\"timestamp\":\"%s\",\"port\":%" PRIu32
               ",\"bridged\":%s,\"attached\":%s,\"started\":%s,\"reset_latched\":%s"
               ",\"ever_attached\":%s,\"state\":\"%s\""
               ",\"packet_queue\":%" PRIu32 ",\"packet_queue_depth\":%" PRIu32
               ",\"time_code_queue\":%" PRIu32 ",\"time_code_queue_depth\":%" PRIu32
               ",\"tx_packets\":%" PRIu64 ",\"rx_packets\":%" PRIu64
               ",\"tx_bytes\":%" PRIu64 ",\"rx_bytes\":%" PRIu64
               ",\"tx_time_codes\":%" PRIu64 ",\"rx_time_codes\":%" PRIu64
               ",\"eep_packets\":%" PRIu64 ",\"link_errors\":%" PRIu64
               ",\"dropped_packets\":%" PRIu64 "}\n",
               timestamp,
               port_id,
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_BRIDGED),
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_ATTACHED),
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_STARTED),
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_RESET_LATCHED),
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_EVER_ATTACHED),
               state_name(snapshot->info.link_state),
               snapshot->info.packet_queue_count,
               server->packet_queue_depth,
               snapshot->info.time_code_queue_count,
               server->time_code_queue_depth,
               snapshot->statistics.tx_packets,
               snapshot->statistics.rx_packets,
               snapshot->statistics.tx_bytes,
               snapshot->statistics.rx_bytes,
               snapshot->statistics.tx_time_codes,
               snapshot->statistics.rx_time_codes,
               snapshot->statistics.eep_packets,
               snapshot->statistics.link_errors,
               snapshot->statistics.dropped_packets);
    } else {
        printf("%s port=%" PRIu32 " state=%s bridged=%s attached=%s started=%s "
               "packets=%" PRIu32 "/%" PRIu32 " timecodes=%" PRIu32 "/%" PRIu32
               " tx_packets=%" PRIu64 " rx_packets=%" PRIu64
               " link_errors=%" PRIu64 " dropped=%" PRIu64 "\n",
               timestamp,
               port_id,
               state_name(snapshot->info.link_state),
               (snapshot->info.flags & VSPD_PORT_INFO_BRIDGED) != 0u ? "yes" : "no",
               (snapshot->info.flags & VSPD_PORT_INFO_ATTACHED) != 0u ? "yes" : "no",
               (snapshot->info.flags & VSPD_PORT_INFO_STARTED) != 0u ? "yes" : "no",
               snapshot->info.packet_queue_count,
               server->packet_queue_depth,
               snapshot->info.time_code_queue_count,
               server->time_code_queue_depth,
               snapshot->statistics.tx_packets,
               snapshot->statistics.rx_packets,
               snapshot->statistics.link_errors,
               snapshot->statistics.dropped_packets);
    }
    fflush(stdout);
}

int main(int argc, char** argv) {
    const char* socket_path = VSPWD_DEFAULT_SOCKET_PATH;
    uint32_t selected_port = 0u;
    bool port_selected = false;
    bool json = false;
    bool count_set = false;
    uint64_t count_limit = 0u;
    uint64_t emitted = 0u;
    vspd_management_client_t client;
    vspd_server_info_payload_t server;
    struct sigaction action;
    int argument;
    int32_t status;
    uint32_t port_id;

    for (argument = 1; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--socket") == 0) {
            if (++argument >= argc) {
                usage(argv[0]);
                return 2;
            }
            socket_path = argv[argument];
        } else if (strcmp(argv[argument], "--port") == 0) {
            if (++argument >= argc || !parse_u32(argv[argument], &selected_port)) {
                usage(argv[0]);
                return 2;
            }
            port_selected = true;
        } else if (strcmp(argv[argument], "--count") == 0) {
            if (++argument >= argc || !parse_u64(argv[argument], &count_limit)) {
                usage(argv[0]);
                return 2;
            }
            count_set = true;
        } else if (strcmp(argv[argument], "--json") == 0) {
            json = true;
        } else if (strcmp(argv[argument], "--help") == 0 ||
                   strcmp(argv[argument], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (count_set && count_limit == 0u) {
        return 0;
    }

    status = vspd_management_open(&client, socket_path);
    if (status != VSPD_STATUS_OK) {
        return fail_status("connect", status);
    }
    status = vspd_management_get_server_info(&client, &server);
    if (status != VSPD_STATUS_OK) {
        vspd_management_close(&client);
        return fail_status("get server info", status);
    }
    if (port_selected && selected_port >= server.port_count) {
        vspd_management_close(&client);
        fprintf(stderr, "spwmon: port %" PRIu32 " does not exist\n", selected_port);
        return 2;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    stop_requested = 0;

    if (port_selected) {
        status = vspd_management_subscribe_port(&client, selected_port);
        if (status != VSPD_STATUS_OK) {
            vspd_management_close(&client);
            return fail_status("subscribe", status);
        }
    } else {
        for (port_id = 0u; port_id < server.port_count; ++port_id) {
            status = vspd_management_subscribe_port(&client, port_id);
            if (status != VSPD_STATUS_OK) {
                vspd_management_close(&client);
                return fail_status("subscribe", status);
            }
        }
    }

    while (!stop_requested && (!count_set || emitted < count_limit)) {
        vspd_port_snapshot_payload_t snapshot;
        status = vspd_management_receive_snapshot(&client, 500, &port_id, &snapshot);
        if (status == VSPD_STATUS_TIMEOUT) {
            continue;
        }
        if (status != VSPD_STATUS_OK) {
            vspd_management_close(&client);
            return fail_status("receive snapshot", status);
        }
        print_snapshot(port_id, &snapshot, &server, json);
        ++emitted;
    }

    if (port_selected) {
        (void)vspd_management_unsubscribe_port(&client, selected_port);
    } else {
        for (port_id = 0u; port_id < server.port_count; ++port_id) {
            (void)vspd_management_unsubscribe_port(&client, port_id);
        }
    }
    vspd_management_close(&client);
    return 0;
}
