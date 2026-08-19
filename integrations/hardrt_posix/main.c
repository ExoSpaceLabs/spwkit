// SPDX-License-Identifier: Apache-2.0

#include <hardrt.h>
#include <spwkit/spwkit.h>

#include <stdalign.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTEGRATION_WORKSPACE_BYTES 65536u
#define INTEGRATION_TIMEOUT_US ((spw_timeout_us_t)50000u)

typedef struct integration_task_context {
    const char* name;
    spw_terminator_t terminator;
    uint8_t time_count;
    uint8_t payload[8];
    alignas(max_align_t) uint8_t workspace[INTEGRATION_WORKSPACE_BYTES];
} integration_task_context_t;

static atomic_int g_completed = ATOMIC_VAR_INIT(0);
static atomic_int g_failures = ATOMIC_VAR_INIT(0);

static uint32_t g_stack_a[4096];
static uint32_t g_stack_b[4096];

static integration_task_context_t g_task_a = {
    .name = "hardrt-a",
    .terminator = SPW_TERMINATOR_EOP,
    .time_count = 7u,
    .payload = {0x53u, 0x70u, 0x57u, 0x4bu, 0x69u, 0x74u, 0x41u, 0x00u},
};

static integration_task_context_t g_task_b = {
    .name = "hardrt-b",
    .terminator = SPW_TERMINATOR_EEP,
    .time_count = 23u,
    .payload = {0x53u, 0x70u, 0x57u, 0x4bu, 0x69u, 0x74u, 0x42u, 0x00u},
};

static int run_spwkit_contract(integration_task_context_t* ctx) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    spw_port_t* port = NULL;
    spw_capabilities_t capabilities = {0};
    spw_statistics_t statistics = {0};
    uint8_t received[sizeof(ctx->payload)] = {0};
    spw_packet_t tx = {
        ctx->payload,
        sizeof(ctx->payload),
        sizeof(ctx->payload),
        ctx->terminator,
    };
    spw_packet_t rx = {
        received,
        0u,
        sizeof(received),
        SPW_TERMINATOR_EOP,
    };
    spw_time_code_t tx_time = {ctx->time_count, 0u};
    spw_time_code_t rx_time = {0u, 0u};

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(ctx->workspace) ||
        requirements.alignment > alignof(max_align_t)) {
        fprintf(stderr, "%s: workspace requirements rejected\n", ctx->name);
        return 0;
    }

    if (spw_port_open_in_place(&config,
                               ctx->workspace,
                               sizeof(ctx->workspace),
                               &port) != SPW_OK ||
        port == NULL) {
        fprintf(stderr, "%s: open_in_place failed\n", ctx->name);
        return 0;
    }

    if (spw_port_start(port) != SPW_OK ||
        spw_port_get_capabilities(port, &capabilities) != SPW_OK) {
        fprintf(stderr, "%s: start/capabilities failed\n", ctx->name);
        (void)spw_port_close(port);
        return 0;
    }

    if ((capabilities.bits & (SPW_CAP_EEP | SPW_CAP_TIME_CODE |
                              SPW_CAP_LINK_CONTROL | SPW_CAP_STATISTICS)) !=
        (SPW_CAP_EEP | SPW_CAP_TIME_CODE |
         SPW_CAP_LINK_CONTROL | SPW_CAP_STATISTICS)) {
        fprintf(stderr, "%s: expected loopback capabilities missing\n", ctx->name);
        (void)spw_port_close(port);
        return 0;
    }

    if (spw_port_send(port, &tx, INTEGRATION_TIMEOUT_US) != SPW_OK ||
        spw_port_receive(port, &rx, INTEGRATION_TIMEOUT_US) != SPW_OK ||
        rx.length != sizeof(ctx->payload) ||
        rx.terminator != ctx->terminator ||
        memcmp(received, ctx->payload, sizeof(ctx->payload)) != 0) {
        fprintf(stderr, "%s: packet contract failed\n", ctx->name);
        (void)spw_port_close(port);
        return 0;
    }

    if (spw_port_send_time_code(port, &tx_time, INTEGRATION_TIMEOUT_US) != SPW_OK ||
        spw_port_receive_time_code(port, &rx_time, INTEGRATION_TIMEOUT_US) != SPW_OK ||
        rx_time.time_count != tx_time.time_count ||
        rx_time.control_flags != 0u) {
        fprintf(stderr, "%s: time-code contract failed\n", ctx->name);
        (void)spw_port_close(port);
        return 0;
    }

    if (spw_port_get_statistics(port, &statistics) != SPW_OK ||
        statistics.tx_packets != 1u ||
        statistics.rx_packets != 1u ||
        statistics.tx_time_codes != 1u ||
        statistics.rx_time_codes != 1u) {
        fprintf(stderr, "%s: statistics contract failed\n", ctx->name);
        (void)spw_port_close(port);
        return 0;
    }

    if (spw_port_close(port) != SPW_OK) {
        fprintf(stderr, "%s: close failed\n", ctx->name);
        return 0;
    }

    return 1;
}

static void integration_task(void* argument) {
    integration_task_context_t* ctx = (integration_task_context_t*)argument;
    const int ok = run_spwkit_contract(ctx);

    if (!ok) {
        (void)atomic_fetch_add_explicit(&g_failures, 1, memory_order_relaxed);
    }

    printf("%s: %s\n", ctx->name, ok ? "PASS" : "FAIL");
    fflush(stdout);

    if (atomic_fetch_add_explicit(&g_completed, 1, memory_order_acq_rel) + 1 == 2) {
        const int failures = atomic_load_explicit(&g_failures, memory_order_acquire);
        fflush(stdout);
        fflush(stderr);
        _Exit(failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
}

int main(void) {
    const hrt_config_t config = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 1u,
        .core_hz = 0u,
        .tick_src = HRT_TICK_SYSTICK,
    };
    const hrt_task_attr_t attributes = {
        .priority = HRT_PRIO1,
        .timeslice = 1u,
    };

    printf("HardRT %s / %s, SpWKit API %u.%u.%u\n",
           hrt_version_string(),
           hrt_port_name(),
           (unsigned)SPWKIT_API_VERSION_MAJOR,
           (unsigned)SPWKIT_API_VERSION_MINOR,
           (unsigned)SPWKIT_API_VERSION_PATCH);

    if (hrt_init(&config) != 0 ||
        hrt_create_task(integration_task,
                        &g_task_a,
                        g_stack_a,
                        sizeof(g_stack_a) / sizeof(g_stack_a[0]),
                        &attributes) < 0 ||
        hrt_create_task(integration_task,
                        &g_task_b,
                        g_stack_b,
                        sizeof(g_stack_b) / sizeof(g_stack_b[0]),
                        &attributes) < 0) {
        fprintf(stderr, "HardRT integration setup failed\n");
        return EXIT_FAILURE;
    }

    hrt_start();
    return EXIT_FAILURE;
}
