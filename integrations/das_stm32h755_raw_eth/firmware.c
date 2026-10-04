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
    volatile uint32_t core_hz;
    volatile spw_das_cycle_stats_t app_send_cycles;
    volatile spw_das_cycle_stats_t app_receive_cycles;
    volatile spw_das_cycle_stats_t das_tx_cycles;
    volatile spw_das_cycle_stats_t das_rx_poll_cycles;
    volatile spw_das_cycle_stats_t das_rx_success_cycles;
    volatile uint32_t das_tx_successes;
    volatile uint32_t das_tx_failures;
    volatile uint32_t das_rx_errors;
    volatile uint32_t das_rx_empty_polls;
    volatile uint32_t das_last_tx_size;
    volatile uint32_t das_last_rx_size;
    volatile uint8_t das_last_tx_header[14];
    volatile uint8_t das_last_rx_header[14];
} spw_das_evidence_t;

volatile spw_das_evidence_t g_spwkit_das_raw_evidence = {
    .magic = SPWKIT_DAS_EVIDENCE_MAGIC,
    .phase = 0u,
    .result = UINT32_MAX,
    .core_hz = SPWKIT_DAS_CORE_HZ
};

static spw_das_cycle_stats_t g_app_send_cycles;
static spw_das_cycle_stats_t g_app_receive_cycles;

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

static void snapshot_raw_stats(void) {
    spw_das_raw_io_stats_t raw_stats;
    spw_das_raw_io_get_stats(&g_raw_io, &raw_stats);
    g_spwkit_das_raw_evidence.app_send_cycles = g_app_send_cycles;
    g_spwkit_das_raw_evidence.app_receive_cycles = g_app_receive_cycles;
    g_spwkit_das_raw_evidence.das_tx_cycles = raw_stats.tx_send;
    g_spwkit_das_raw_evidence.das_rx_poll_cycles = raw_stats.rx_poll;
    g_spwkit_das_raw_evidence.das_rx_success_cycles = raw_stats.rx_success;
    g_spwkit_das_raw_evidence.das_tx_successes = raw_stats.tx_successes;
    g_spwkit_das_raw_evidence.das_tx_failures = raw_stats.tx_failures;
    g_spwkit_das_raw_evidence.das_rx_errors = raw_stats.rx_errors;
    g_spwkit_das_raw_evidence.das_rx_empty_polls = raw_stats.rx_empty_polls;
    g_spwkit_das_raw_evidence.das_last_tx_size = raw_stats.last_tx_size;
    g_spwkit_das_raw_evidence.das_last_rx_size = raw_stats.last_rx_size;
    memcpy((void*)g_spwkit_das_raw_evidence.das_last_tx_header,
           raw_stats.last_tx_header,
           sizeof(raw_stats.last_tx_header));
    memcpy((void*)g_spwkit_das_raw_evidence.das_last_rx_header,
           raw_stats.last_rx_header,
           sizeof(raw_stats.last_rx_header));
}

static void fail(uint32_t code) {
    snapshot_raw_stats();
    g_spwkit_das_raw_evidence.result = code;
    g_spwkit_das_raw_evidence.phase = UINT32_C(0xdead0000) | code;
    (void)das_board_led_set(DAS_BOARD_LED_RED, true);
    for (;;) {
        (void)das_delay_ms(100u);
    }
}

static int wait_for_physical_link(das_eth_t eth,
                                  das_eth_link_state_t* out_link,
                                  uint32_t timeout_ms) {
    const das_time_ms_t start = das_time_now_ms();

    if (out_link == NULL) {
        return 0;
    }

    for (;;) {
        das_eth_link_state_t link = {0};
        if (das_eth_link_state(eth, &link) != DAS_OK) {
            return 0;
        }
        if (link.up) {
            *out_link = link;
            return 1;
        }
        if (das_time_elapsed_ms(start) >= timeout_ms) {
            return 0;
        }
        (void)das_delay_ms(10u);
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

static void record_cycles(spw_das_cycle_stats_t* stats, uint32_t cycles) {
    if (stats->count == 0u || cycles < stats->min_cycles) {
        stats->min_cycles = cycles;
    }
    if (cycles > stats->max_cycles) {
        stats->max_cycles = cycles;
    }
    ++stats->count;
    stats->total_cycles += (uint64_t)cycles;
}

static int is_done_packet(const spw_packet_t* packet) {
    return packet->length == sizeof(DONE_PACKET) &&
           memcmp(packet->data, DONE_PACKET, sizeof(DONE_PACKET)) == 0;
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
        das_clock_set_frequency(UINT32_C(400000000)) != DAS_OK ||
        das_time_init() != DAS_OK) {
        fail(0x101u);
    }

    memcpy(eth_config.mac, BOARD_MAC, sizeof(BOARD_MAC));
    if (das_board_eth_init(DAS_BOARD_ETH_RJ45, &eth_config, &eth) != DAS_OK ||
        !das_eth_is_valid(eth)) {
        fail(0x102u);
    }

    g_spwkit_das_raw_evidence.phase = 2u;
    spw_das_raw_io_init(&g_raw_io, eth);
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
    raw.keepalive_interval_ms = 100u;
    raw.peer_timeout_ms = 1000u;

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

    g_spwkit_das_raw_evidence.phase = 3u;
    if (!wait_for_physical_link(eth, &physical_link, 10000u)) {
        fail(0x302u);
    }
    g_spwkit_das_raw_evidence.link_speed_mbps = physical_link.speed_mbps;
    g_spwkit_das_raw_evidence.link_duplex = (uint32_t)physical_link.duplex;

    /*
     * The VSPW engine sends its first keepalive from spw_port_start().
     * The DAS raw-Ethernet binding rejects sends while the PHY reports link
     * down, so starting VSPW before link negotiation completes creates a
     * startup race and returns SPW_ERR_LINK_UNAVAILABLE.
     */
    if (spw_port_start(port) != SPW_OK) {
        fail(0x203u);
    }

    if (!wait_for_run(port)) {
        fail(0x301u);
    }
    (void)das_board_led_set(DAS_BOARD_LED_GREEN, true);

    g_spwkit_das_raw_evidence.phase = 4u;
    for (;;) {
        spw_packet_t incoming = {
            g_rx,
            0u,
            sizeof(g_rx),
            SPW_TERMINATOR_EOP
        };
        const uint32_t receive_start = spw_das_cycle_counter_read();
        spw_result_t result =
            spw_port_receive(port, &incoming, UINT64_C(50000));
        const uint32_t receive_cycles =
            spw_das_cycle_counter_read() - receive_start;

        if (result == SPW_ERR_TIMEOUT || result == SPW_ERR_LINK_UNAVAILABLE) {
            spw_link_state_t state = SPW_LINK_ERROR_RESET;
            (void)spw_port_get_link_state(port, &state);
            continue;
        }
        if (result != SPW_OK) {
            fail(0x401u);
        }
        record_cycles(&g_app_receive_cycles, receive_cycles);

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
            snapshot_raw_stats();
            g_spwkit_das_raw_evidence.result = 0u;
            g_spwkit_das_raw_evidence.phase = SPWKIT_DAS_PASS_PHASE;
            (void)das_board_led_set(DAS_BOARD_LED_YELLOW, true);
            for (;;) {
                spw_link_state_t state = SPW_LINK_ERROR_RESET;
                (void)spw_port_get_link_state(port, &state);
                (void)das_delay_ms(10u);
            }
        }

        {
            spw_packet_t outgoing = {
                incoming.data,
                incoming.length,
                incoming.length,
                incoming.terminator
            };
            const uint32_t send_start = spw_das_cycle_counter_read();
            const spw_result_t send_result =
                spw_port_send(port, &outgoing, UINT64_C(500000));
            const uint32_t send_cycles =
                spw_das_cycle_counter_read() - send_start;
            if (send_result != SPW_OK) {
                fail(0x403u);
            }
            record_cycles(&g_app_send_cycles, send_cycles);
        }
        ++echoed;
        (void)das_board_led_toggle(DAS_BOARD_LED_YELLOW);
    }
}
