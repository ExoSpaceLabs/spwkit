// SPDX-License-Identifier: Apache-2.0

#include <hardrt.h>
#include <spwkit/spwkit.h>

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define FIRMWARE_WORKSPACE_BYTES 65536u
#define FIRMWARE_TIMEOUT_US ((spw_timeout_us_t)50000u)

extern uint32_t __StackTop;
void Reset_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

/* Required by the current HardRT Cortex-M port. The first contract targets a
 * Cortex-M7-class system clock without claiming a specific STM32 board setup. */
uint32_t SystemCoreClock = 480000000u;

/* Keep the architecture handlers referenced so the final firmware link proves
 * that the HardRT Cortex-M port, including PendSV assembly, is present. */
__attribute__((section(".isr_vector"), used))
const uintptr_t g_vector_table[16] = {
    (uintptr_t)&__StackTop,
    (uintptr_t)&Reset_Handler,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    0u,
    (uintptr_t)&PendSV_Handler,
    (uintptr_t)&SysTick_Handler,
};

static uint32_t g_task_stack[512];
static alignas(max_align_t) uint8_t g_workspace[FIRMWARE_WORKSPACE_BYTES];
static volatile uint32_t g_contract_result = 0xffffffffu;

static int bytes_equal(const uint8_t* lhs, const uint8_t* rhs, size_t size) {
    size_t index = 0u;
    for (index = 0u; index < size; ++index) {
        if (lhs[index] != rhs[index]) {
            return 0;
        }
    }
    return 1;
}

static int run_spwkit_contract(void) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    spw_port_t* port = NULL;
    uint8_t payload[6] = {0x53u, 0x70u, 0x57u, 0x4bu, 0x4du, 0x37u};
    uint8_t received[sizeof(payload)] = {0u};
    spw_packet_t tx = {
        payload,
        sizeof(payload),
        sizeof(payload),
        SPW_TERMINATOR_EEP,
    };
    spw_packet_t rx = {
        received,
        0u,
        sizeof(received),
        SPW_TERMINATOR_EOP,
    };
    spw_time_code_t tx_time = {31u, 0u};
    spw_time_code_t rx_time = {0u, 0u};
    spw_statistics_t statistics = {0};

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(g_workspace) ||
        requirements.alignment > alignof(max_align_t)) {
        return 0;
    }

    if (spw_port_open_in_place(&config,
                               g_workspace,
                               sizeof(g_workspace),
                               &port) != SPW_OK ||
        port == NULL) {
        return 0;
    }

    if (spw_port_start(port) != SPW_OK ||
        spw_port_send(port, &tx, FIRMWARE_TIMEOUT_US) != SPW_OK ||
        spw_port_receive(port, &rx, FIRMWARE_TIMEOUT_US) != SPW_OK ||
        rx.length != sizeof(payload) ||
        rx.terminator != SPW_TERMINATOR_EEP ||
        !bytes_equal(payload, received, sizeof(payload)) ||
        spw_port_send_time_code(port, &tx_time, FIRMWARE_TIMEOUT_US) != SPW_OK ||
        spw_port_receive_time_code(port, &rx_time, FIRMWARE_TIMEOUT_US) != SPW_OK ||
        rx_time.time_count != tx_time.time_count ||
        spw_port_get_statistics(port, &statistics) != SPW_OK ||
        statistics.tx_packets != 1u ||
        statistics.rx_packets != 1u ||
        statistics.tx_time_codes != 1u ||
        statistics.rx_time_codes != 1u) {
        (void)spw_port_close(port);
        return 0;
    }

    return spw_port_close(port) == SPW_OK;
}

static void integration_task(void* argument) {
    (void)argument;
    g_contract_result = run_spwkit_contract() ? 0u : 1u;

    /* Preserve the result for debugger/HIL inspection. The contract in hosted
     * CI is compile/link/ELF evidence only; it does not pretend to execute an
     * STM32H7 electrical target. */
    for (;;) {
        hrt_yield();
    }
}

void Reset_Handler(void) {
    const hrt_config_t config = {
        .tick_hz = 1000u,
        .policy = HRT_SCHED_PRIORITY_RR,
        .default_slice = 1u,
        .core_hz = SystemCoreClock,
        .tick_src = HRT_TICK_SYSTICK,
    };
    const hrt_task_attr_t attributes = {
        .priority = HRT_PRIO1,
        .timeslice = 1u,
    };

    if (hrt_init(&config) != 0 ||
        hrt_create_task(integration_task,
                        NULL,
                        g_task_stack,
                        sizeof(g_task_stack) / sizeof(g_task_stack[0]),
                        &attributes) < 0) {
        g_contract_result = 2u;
        for (;;) {
            __asm volatile ("wfi");
        }
    }

    hrt_start();
    for (;;) {
        __asm volatile ("wfi");
    }
}
