// SPDX-License-Identifier: Apache-2.0
/* Private in-memory VSPW CPU profiling; not an on-wire throughput benchmark. */
#include "backends/ethernet/in_memory_transport.h"
#include "backends/ethernet/vspw_engine.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { MAX_TEST_PAYLOAD = 8192u, MAX_FRAGMENTS = 8u };
typedef struct fake_clock { uint64_t now_us; } fake_clock_t;
static uint64_t fake_now(void* ctx) {
    return ((fake_clock_t*)ctx)->now_us;
}
static spw_result_t fake_delay(void* ctx, uint64_t us,
                               spw_timeout_us_t timeout) {
    fake_clock_t* c = (fake_clock_t*)ctx;
    if (timeout != SPW_TIMEOUT_INFINITE && timeout < us) {
        return SPW_ERR_TIMEOUT;
    }
    c->now_us += us;
    return SPW_OK;
}
static const spw_vspw_runtime_ops_t OPS = { fake_now, fake_delay };

static spw_vspw_engine_t a;
static spw_vspw_engine_t b;
static spw_in_memory_transport_link_t link;
static uint8_t tx_data[MAX_TEST_PAYLOAD];
static uint8_t rx_data[MAX_TEST_PAYLOAD];

static int parse_u32(const char* raw, uint32_t* out) {
    char* end = NULL;
    unsigned long n;
    errno = 0;
    if (raw == NULL || *raw == '\0' || *raw == '-') return 0;
    n = strtoul(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0' || n > UINT32_MAX) return 0;
    *out = (uint32_t)n;
    return 1;
}
static int fail(const char* where, spw_result_t code) {
    fprintf(stderr, "vspw_profile: %s failed (spw_result=%d)\n",
            where, (int)code);
    return EXIT_FAILURE;
}
int main(int argc, char** argv) {
    uint32_t payload = 4096u, iterations = 1000u, fragment = 1400u;
    bool deferred = false;
    spw_transport_peer_id_t peer_a = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_transport_peer_id_t peer_b = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_in_memory_transport_endpoint_t ep_a, ep_b;
    spw_transport_provider_t provider_a = SPW_TRANSPORT_PROVIDER_INITIALIZER;
    spw_transport_provider_t provider_b = SPW_TRANSPORT_PROVIDER_INITIALIZER;
    fake_clock_t clock_a = { 100u }, clock_b = { 200u };
    spw_vspw_runtime_t runtime_a = { &OPS, &clock_a };
    spw_vspw_runtime_t runtime_b = { &OPS, &clock_b };
    spw_vspw_engine_config_t cfg_a = { 0 };
    spw_vspw_engine_config_t cfg_b = { 0 };
    spw_packet_t tx, rx;
    spw_link_state_t state = SPW_LINK_ERROR_RESET;
    const uint8_t aid = 0xa1u, bid = 0xb2u;
    clock_t cpu_start, cpu_end;
    size_t fragments;
    uint32_t i;
    double cpu_seconds;

    for (i = 1u; i < (uint32_t)argc; ++i) {
        if (i + 1u >= (uint32_t)argc) {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            return EXIT_FAILURE;
        }
        if (strcmp(argv[i], "--payload") == 0) {
            if (!parse_u32(argv[++i], &payload)) return EXIT_FAILURE;
        } else if (strcmp(argv[i], "--iterations") == 0) {
            if (!parse_u32(argv[++i], &iterations)) return EXIT_FAILURE;
        } else if (strcmp(argv[i], "--fragment") == 0) {
            if (!parse_u32(argv[++i], &fragment)) return EXIT_FAILURE;
        } else if (strcmp(argv[i], "--ack-pump") == 0) {
            ++i;
            if (strcmp(argv[i], "deferred") == 0) deferred = true;
            else if (strcmp(argv[i], "immediate") != 0) return EXIT_FAILURE;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (payload > MAX_TEST_PAYLOAD || iterations == 0u ||
        fragment == 0u ||
        fragment > SPW_IN_MEMORY_TRANSPORT_MAX_MESSAGE_SIZE -
                   SPW_VSPW_TP_HEADER_SIZE) {
        fprintf(stderr, "Invalid fixture payload/iterations/fragment\n");
        return EXIT_FAILURE;
    }
    fragments = payload == 0u ? 1u : (payload + fragment - 1u) / fragment;
    if (fragments > MAX_FRAGMENTS) {
        fprintf(stderr, "Payload requires more than fixture queue depth\n");
        return EXIT_FAILURE;
    }

    cfg_a.link_id = cfg_b.link_id = 7u;
    cfg_a.fragment_payload_size = cfg_b.fragment_payload_size = (uint16_t)fragment;
    cfg_a.max_retries = cfg_b.max_retries = 3u;
    cfg_a.ack_timeout_ms = cfg_b.ack_timeout_ms = 100u;
    cfg_a.keepalive_interval_ms = cfg_b.keepalive_interval_ms = 1000u;
    cfg_a.peer_timeout_ms = cfg_b.peer_timeout_ms = 3000u;
    cfg_a.session_nonce = 0x1111u;
    cfg_b.session_nonce = 0x2222u;

    spw_in_memory_transport_link_init(&link);
    if (!spw_transport_peer_id_set(&peer_a, &aid, 1u) ||
        !spw_transport_peer_id_set(&peer_b, &bid, 1u)) {
        return EXIT_FAILURE;
    }
    if (spw_in_memory_transport_endpoint_init(
            &ep_a, &link, 0u, &peer_a, &peer_b) != SPW_OK ||
        spw_in_memory_transport_endpoint_init(
            &ep_b, &link, 1u, &peer_b, &peer_a) != SPW_OK) return EXIT_FAILURE;
    spw_in_memory_transport_provider(&ep_a, &provider_a);
    spw_in_memory_transport_provider(&ep_b, &provider_b);
    if (spw_vspw_engine_init(&a, &cfg_a, &provider_a, &peer_b,
                             &runtime_a, NULL) != SPW_OK ||
        spw_vspw_engine_init(&b, &cfg_b, &provider_b, &peer_a,
                             &runtime_b, NULL) != SPW_OK) return EXIT_FAILURE;
    if (spw_vspw_engine_start(&a) != SPW_OK ||
        spw_vspw_engine_start(&b) != SPW_OK) return EXIT_FAILURE;
    if (spw_vspw_engine_get_link_state(&a, &state) != SPW_OK ||
        state != SPW_LINK_RUN ||
        spw_vspw_engine_get_link_state(&b, &state) != SPW_OK ||
        state != SPW_LINK_RUN) return EXIT_FAILURE;

    memset(&a.diagnostics, 0, sizeof(a.diagnostics));
    memset(&b.diagnostics, 0, sizeof(b.diagnostics));
    for (i = 0u; i < payload; ++i) tx_data[i] = (uint8_t)(i * 13u + 7u);
    memset(rx_data, 0, sizeof(rx_data));
    tx = (spw_packet_t){ tx_data, payload, payload, SPW_TERMINATOR_EOP };
    rx = (spw_packet_t){ rx_data, 0u, payload, SPW_TERMINATOR_EOP };

    cpu_start = clock();
    if (cpu_start == (clock_t)-1) return EXIT_FAILURE;
    for (i = 0u; i < iterations; ++i) {
        spw_result_t result;
        result = spw_vspw_engine_send(&a, &tx, 100000u);
        if (result != SPW_OK) return fail("send", result);
        rx.length = 0u;
        result = spw_vspw_engine_receive(&b, &rx, 100000u);
        if (result != SPW_OK) return fail("receive", result);
        if (rx.length != payload || rx.terminator != SPW_TERMINATOR_EOP ||
            memcmp(tx_data, rx_data, payload) != 0) {
            fprintf(stderr, "VSPW payload corruption at iteration %u\n", i);
            return EXIT_FAILURE;
        }
        if (!deferred) {
            result = spw_vspw_engine_get_link_state(&a, &state);
            if (result != SPW_OK || state != SPW_LINK_RUN) {
                return fail("pump ACK", result);
            }
        }
    }
    /* A deferred final ACK must still be consumed before accounting. */
    if (deferred &&
        (spw_vspw_engine_get_link_state(&a, &state) != SPW_OK ||
         state != SPW_LINK_RUN)) return EXIT_FAILURE;
    cpu_end = clock();
    if (cpu_end == (clock_t)-1 || cpu_end < cpu_start) return EXIT_FAILURE;
    cpu_seconds = (double)(cpu_end - cpu_start) / (double)CLOCKS_PER_SEC;

    if (a.diagnostics.tx_data_frames != (uint64_t)iterations * fragments ||
        b.diagnostics.rx_data_frames != (uint64_t)iterations * fragments ||
        b.diagnostics.tx_ack_frames != iterations ||
        a.diagnostics.ack_matched != iterations ||
        a.diagnostics.retry_attempts != 0u ||
        a.diagnostics.ack_timeouts != 0u ||
        b.diagnostics.duplicate_messages != 0u ||
        a.diagnostics.staged_bytes != (uint64_t)iterations * payload ||
        a.diagnostics.fragmented_bytes != (uint64_t)iterations * payload ||
        b.diagnostics.delivered_copy_bytes !=
            (uint64_t)iterations * payload * (fragments > 1u ? 2u : 1u) ||
        a.diagnostics.tx_slot_wait_calls != (deferred ? iterations - 1u : 0u)) {
        fprintf(stderr, "VSPW diagnostic accounting mismatch\n");
        fprintf(stderr, "got frames=%" PRIu64 " rx=%" PRIu64
                " ack=%" PRIu64 " matched=%" PRIu64
                " slots=%" PRIu64 " staged=%" PRIu64
                " fragmented=%" PRIu64 " delivered=%" PRIu64 "\n",
                a.diagnostics.tx_data_frames,b.diagnostics.rx_data_frames,
                b.diagnostics.tx_ack_frames,a.diagnostics.ack_matched,
                a.diagnostics.tx_slot_wait_calls,
                a.diagnostics.staged_bytes,a.diagnostics.fragmented_bytes,
                b.diagnostics.delivered_copy_bytes);
        return EXIT_FAILURE;
    }
    printf("{\"fixture\":\"in_memory\",\"ack_pump\":\"%s\","
           "\"payload_bytes\":%u,\"fragment_bytes\":%u,\"iterations\":%u,"
           "\"logical_bytes\":%" PRIu64 ","
           "\"data_frames\":%" PRIu64 ",\"ack_frames\":%" PRIu64 ","
           "\"retries\":%" PRIu64 ",\"ack_timeouts\":%" PRIu64 ","
           "\"tx_slot_wait_calls\":%" PRIu64 ","
           "\"staged_bytes\":%" PRIu64 ",\"fragmented_bytes\":%" PRIu64 ","
           "\"delivered_copy_bytes\":%" PRIu64 ","
           "\"cpu_seconds\":%.9f,\"cpu_ns_per_packet\":%.3f}\n",
           deferred ? "deferred" : "immediate",
           payload, fragment, iterations,
           (uint64_t)iterations * payload,
           a.diagnostics.tx_data_frames,
           b.diagnostics.tx_ack_frames,
           a.diagnostics.retry_attempts, a.diagnostics.ack_timeouts,
           a.diagnostics.tx_slot_wait_calls,
           a.diagnostics.staged_bytes,a.diagnostics.fragmented_bytes,
           b.diagnostics.delivered_copy_bytes,
           cpu_seconds, cpu_seconds * 1.0e9 / (double)iterations);
    return EXIT_SUCCESS;
}
