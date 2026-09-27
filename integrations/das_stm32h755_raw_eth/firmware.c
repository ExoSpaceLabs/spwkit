// SPDX-License-Identifier: Apache-2.0
#include "das_raw_io.h"
#include "peer_config.h"

#include <das/das.h>
#include <spwkit/raw_ethernet.h>
#include <spwkit/spwkit.h>

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SPWKIT_DAS_WORKSPACE_BYTES 32768u
#define SPWKIT_DAS_PACKET_BYTES 4096u
#define SPWKIT_DAS_LINK_ID UINT32_C(0x44534153)
#define SPWKIT_DAS_EVIDENCE_MAGIC UINT32_C(0x53504441)
#define SPWKIT_DAS_PASS_PHASE UINT32_C(0x0000700d)
#define SPWKIT_DAS_CORE_HZ UINT32_C(400000000)
#define SPWKIT_DAS_PROFILE_ROWS 4u
#define SPWKIT_DAS_PROFILE_SAMPLES 128u
#define SPWKIT_DAS_TEST_HEADER_BYTES 12u
#define SPWKIT_DAS_TEST_FLAG_MEASURE 0x01u

#define SPWKIT_DEMCR (*(volatile uint32_t*)UINT32_C(0xE000EDFC))
#define SPWKIT_DWT_CTRL (*(volatile uint32_t*)UINT32_C(0xE0001000))
#define SPWKIT_DWT_CYCCNT (*(volatile uint32_t*)UINT32_C(0xE0001004))
#define SPWKIT_DEMCR_TRCENA (UINT32_C(1) << 24u)
#define SPWKIT_DWT_CTRL_CYCCNTENA UINT32_C(1)

typedef struct spw_das_profile_row {
    uint32_t payload_bytes;
    volatile uint32_t count;
    volatile uint32_t tx_api_cycles[SPWKIT_DAS_PROFILE_SAMPLES];
    volatile uint32_t tx_das_cycles[SPWKIT_DAS_PROFILE_SAMPLES];
    volatile uint32_t tx_das_calls[SPWKIT_DAS_PROFILE_SAMPLES];
    volatile uint32_t rx_post_das_cycles[SPWKIT_DAS_PROFILE_SAMPLES];
    volatile uint32_t rx_das_cycles[SPWKIT_DAS_PROFILE_SAMPLES];
    volatile uint32_t rx_das_calls[SPWKIT_DAS_PROFILE_SAMPLES];
} spw_das_profile_row_t;

typedef struct spw_das_evidence {
    uint32_t magic;
    volatile uint32_t phase;
    volatile uint32_t result;
    volatile uint32_t workspace_bytes;
    volatile uint32_t max_packet_size;
    volatile uint32_t link_speed_mbps;
    volatile uint32_t link_duplex;
    volatile uint32_t echoed_packets;
    volatile uint32_t tx_packets;
    volatile uint32_t rx_packets;
    volatile uint32_t tx_bytes;
    volatile uint32_t rx_bytes;
    uint32_t core_hz;
    uint32_t profile_row_count;
    spw_das_profile_row_t profile[SPWKIT_DAS_PROFILE_ROWS];
} spw_das_evidence_t;

volatile spw_das_evidence_t g_spwkit_das_raw_evidence = {
    .magic = SPWKIT_DAS_EVIDENCE_MAGIC,
    .phase = 0u,
    .result = UINT32_MAX,
    .core_hz = SPWKIT_DAS_CORE_HZ,
    .profile_row_count = SPWKIT_DAS_PROFILE_ROWS,
    .profile = {
        {.payload_bytes = 64u},
        {.payload_bytes = 256u},
        {.payload_bytes = 1024u},
        {.payload_bytes = 4096u}
    }
};

static alignas(max_align_t) uint8_t g_workspace[SPWKIT_DAS_WORKSPACE_BYTES];
static uint8_t g_rx[SPWKIT_DAS_PACKET_BYTES];
static spw_das_raw_io_t g_raw_io;

static const uint8_t BOARD_MAC[DAS_ETH_MAC_ADDRESS_SIZE] = {
    0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
};
static const uint8_t HOST_MAC[DAS_ETH_MAC_ADDRESS_SIZE] =
    SPWKIT_DAS_HOST_MAC_BYTES;
static const uint8_t DONE_PACKET[8] = {
    'S', 'P', 'W', 'D', 'O', 'N', 'E', '1'
};
static const uint8_t TEST_MAGIC[4] = {'S', 'P', 'W', 'P'};

static void cycle_counter_prepare(void) {
    SPWKIT_DEMCR |= SPWKIT_DEMCR_TRCENA;
    SPWKIT_DWT_CYCCNT = 0u;
    SPWKIT_DWT_CTRL |= SPWKIT_DWT_CTRL_CYCCNTENA;
    __asm__ __volatile__("" ::: "memory");
}

static uint32_t cycle_counter_read(void* context) {
    (void)context;
    __asm__ __volatile__("" ::: "memory");
    return SPWKIT_DWT_CYCCNT;
}

static uint32_t cycle_delta(uint32_t start, uint32_t end) {
    return end - start;
}

static uint32_t u64_delta_u32(uint64_t start, uint64_t end) {
    const uint64_t delta = end - start;
    return delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
}

static void fail(uint32_t code) {
    g_spwkit_das_raw_evidence.result = code;
    g_spwkit_das_raw_evidence.phase = UINT32_C(0xdead0000) | code;
    (void)das_board_led_set(DAS_BOARD_LED_RED, true);
    for (;;) {
        (void)das_delay_ms(100u);
    }
}

static int wait_for_run(spw_port_t* port) {
    uint32_t attempt;
    for (attempt = 0u; attempt < 20000u; ++attempt) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK) {
            return 0;
        }
        if (state == SPW_LINK_RUN) {
            return 1;
        }
        (void)das_delay_ms(1u);
    }
    return 0;
}

static int is_done_packet(const spw_packet_t* packet) {
    return packet->length == sizeof(DONE_PACKET) &&
           memcmp(packet->data, DONE_PACKET, sizeof(DONE_PACKET)) == 0;
}

static int measured_profile_row(const spw_packet_t* packet) {
    uint32_t row;
    if (packet->length < SPWKIT_DAS_TEST_HEADER_BYTES ||
        memcmp(packet->data, TEST_MAGIC, sizeof(TEST_MAGIC)) != 0 ||
        (packet->data[4] & SPWKIT_DAS_TEST_FLAG_MEASURE) == 0u) {
        return -1;
    }
    for (row = 0u; row < SPWKIT_DAS_PROFILE_ROWS; ++row) {
        if (g_spwkit_das_raw_evidence.profile[row].payload_bytes ==
            packet->length) {
            return (int)row;
        }
    }
    return -1;
}

static void store_profile_sample(
    int row_index,
    uint32_t tx_api_cycles,
    const spw_das_raw_io_metrics_t* rx_before,
    const spw_das_raw_io_metrics_t* rx_after,
    uint32_t rx_api_return_cycle,
    const spw_das_raw_io_metrics_t* tx_before,
    const spw_das_raw_io_metrics_t* tx_after) {
    spw_das_profile_row_t* row;
    uint32_t index;

    if (row_index < 0 || row_index >= (int)SPWKIT_DAS_PROFILE_ROWS) {
        return;
    }
    row = (spw_das_profile_row_t*)&g_spwkit_das_raw_evidence.profile[row_index];
    index = row->count;
    if (index >= SPWKIT_DAS_PROFILE_SAMPLES) {
        fail(0x404u);
    }

    row->tx_api_cycles[index] = tx_api_cycles;
    row->tx_das_cycles[index] =
        u64_delta_u32(tx_before->tx_das_cycles, tx_after->tx_das_cycles);
    row->tx_das_calls[index] =
        tx_after->tx_das_calls - tx_before->tx_das_calls;
    row->rx_das_cycles[index] =
        u64_delta_u32(rx_before->rx_ready_das_cycles,
                      rx_after->rx_ready_das_cycles);
    row->rx_das_calls[index] =
        rx_after->rx_ready_das_calls - rx_before->rx_ready_das_calls;
    row->rx_post_das_cycles[index] =
        row->rx_das_calls[index] == 0u
            ? 0u
            : cycle_delta(rx_after->last_rx_ready_cycle,
                          rx_api_return_cycle);
    row->count = index + 1u;
}

int main(void) {
    das_eth_config_t eth_config = {{0}};
    das_eth_t eth = DAS_ETH_INVALID;
    das_eth_link_state_t physical_link = {0};
    spw_raw_ethernet_config_t raw;
    spw_port_config_t port_config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    spw_capabilities_t capabilities = {0};
    spw_port_t* port = NULL;
    uint32_t echoed = 0u;

    g_spwkit_das_raw_evidence.phase = 1u;
    if (das_board_led_init_all(false) != DAS_OK ||
        das_clock_set_frequency(SPWKIT_DAS_CORE_HZ) != DAS_OK ||
        das_time_init() != DAS_OK) {
        fail(0x101u);
    }
    cycle_counter_prepare();

    memcpy(eth_config.mac, BOARD_MAC, sizeof(BOARD_MAC));
    if (das_board_eth_init(DAS_BOARD_ETH_RJ45, &eth_config, &eth) != DAS_OK ||
        !das_eth_is_valid(eth)) {
        fail(0x102u);
    }

    g_spwkit_das_raw_evidence.phase = 2u;
    spw_das_raw_io_init(&g_raw_io, eth);
    spw_das_raw_io_set_cycle_counter(
        &g_raw_io, cycle_counter_read, NULL);
    raw = (spw_raw_ethernet_config_t)
        SPW_RAW_ETHERNET_CONFIG_INITIALIZER(
            &SPW_DAS_RAW_ETHERNET_IO_OPS,
            &g_raw_io,
            &SPW_DAS_RUNTIME_OPS,
            NULL,
            SPWKIT_DAS_LINK_ID);
    memcpy(raw.local_mac, BOARD_MAC, sizeof(BOARD_MAC));
    memcpy(raw.remote_mac, HOST_MAC, sizeof(HOST_MAC));
    raw.ether_type = SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE;
    raw.fragment_payload_size = 1400u;
    raw.ack_timeout_ms = 20u;
    raw.max_retries = 3u;
    raw.keepalive_interval_ms = 1000u;
    raw.peer_timeout_ms = 3000u;

    port_config.backend_config = &raw;
    port_config.backend_config_size = sizeof(raw);

    if (spw_port_workspace_requirements(&port_config, &requirements) != SPW_OK ||
        requirements.size > sizeof(g_workspace) ||
        requirements.alignment > alignof(max_align_t)) {
        fail(0x201u);
    }
    g_spwkit_das_raw_evidence.workspace_bytes = (uint32_t)requirements.size;

    if (spw_port_open_in_place(&port_config,
                               g_workspace,
                               sizeof(g_workspace),
                               &port) != SPW_OK ||
        port == NULL ||
        spw_port_get_capabilities(port, &capabilities) != SPW_OK ||
        capabilities.max_packet_size > sizeof(g_rx)) {
        fail(0x202u);
    }
    g_spwkit_das_raw_evidence.max_packet_size =
        capabilities.max_packet_size;

    if (spw_port_start(port) != SPW_OK) {
        fail(0x203u);
    }

    g_spwkit_das_raw_evidence.phase = 3u;
    if (!wait_for_run(port)) {
        fail(0x301u);
    }
    if (das_eth_link_state(eth, &physical_link) != DAS_OK ||
        !physical_link.up) {
        fail(0x302u);
    }
    g_spwkit_das_raw_evidence.link_speed_mbps = physical_link.speed_mbps;
    g_spwkit_das_raw_evidence.link_duplex = (uint32_t)physical_link.duplex;
    (void)das_board_led_set(DAS_BOARD_LED_GREEN, true);

    g_spwkit_das_raw_evidence.phase = 4u;
    for (;;) {
        spw_packet_t incoming = {
            g_rx,
            0u,
            sizeof(g_rx),
            SPW_TERMINATOR_EOP
        };
        spw_das_raw_io_metrics_t rx_before = {0};
        spw_das_raw_io_metrics_t rx_after = {0};
        spw_das_raw_io_metrics_t tx_before = {0};
        spw_das_raw_io_metrics_t tx_after = {0};
        uint32_t rx_api_return_cycle;
        uint32_t tx_start;
        uint32_t tx_end;
        int profile_row;
        spw_result_t result;

        spw_das_raw_io_get_metrics(&g_raw_io, &rx_before);
        result = spw_port_receive(port, &incoming, UINT64_C(50000));
        rx_api_return_cycle = cycle_counter_read(NULL);
        spw_das_raw_io_get_metrics(&g_raw_io, &rx_after);

        if (result == SPW_ERR_TIMEOUT || result == SPW_ERR_LINK_UNAVAILABLE) {
            spw_link_state_t state = SPW_LINK_ERROR_RESET;
            (void)spw_port_get_link_state(port, &state);
            continue;
        }
        if (result != SPW_OK) {
            fail(0x401u);
        }

        if (is_done_packet(&incoming)) {
            spw_statistics_t statistics = {0};
            if (spw_port_get_statistics(port, &statistics) != SPW_OK) {
                fail(0x402u);
            }
            g_spwkit_das_raw_evidence.echoed_packets = echoed;
            g_spwkit_das_raw_evidence.tx_packets =
                (uint32_t)statistics.tx_packets;
            g_spwkit_das_raw_evidence.rx_packets =
                (uint32_t)statistics.rx_packets;
            g_spwkit_das_raw_evidence.tx_bytes =
                (uint32_t)statistics.tx_bytes;
            g_spwkit_das_raw_evidence.rx_bytes =
                (uint32_t)statistics.rx_bytes;
            g_spwkit_das_raw_evidence.result = 0u;
            g_spwkit_das_raw_evidence.phase = SPWKIT_DAS_PASS_PHASE;
            (void)das_board_led_set(DAS_BOARD_LED_YELLOW, true);
            for (;;) {
                spw_link_state_t state = SPW_LINK_ERROR_RESET;
                (void)spw_port_get_link_state(port, &state);
                (void)das_delay_ms(10u);
            }
        }

        profile_row = measured_profile_row(&incoming);
        {
            spw_packet_t outgoing = {
                incoming.data,
                incoming.length,
                incoming.length,
                incoming.terminator
            };
            spw_das_raw_io_get_metrics(&g_raw_io, &tx_before);
            tx_start = cycle_counter_read(NULL);
            result = spw_port_send(port, &outgoing, UINT64_C(500000));
            tx_end = cycle_counter_read(NULL);
            spw_das_raw_io_get_metrics(&g_raw_io, &tx_after);
            if (result != SPW_OK) {
                fail(0x403u);
            }
        }
        store_profile_sample(
            profile_row,
            cycle_delta(tx_start, tx_end),
            &rx_before,
            &rx_after,
            rx_api_return_cycle,
            &tx_before,
            &tx_after);

        ++echoed;
        (void)das_board_led_toggle(DAS_BOARD_LED_YELLOW);
    }
}
