// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_STORAGE_SIZE 8192u
#define WORKSPACE_STORAGE_SIZE 65536u
#define WORKSPACE_ALIGNMENT_SLACK 256u

int main(void) {
    spw_port_workspace_requirements_t requirements = {123u, 456u};
    spw_port_t* port = NULL;

    assert(spw_port_workspace_requirements(NULL, &requirements) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_workspace_requirements(NULL, NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_open(NULL, &port) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_open(NULL, NULL) == SPW_ERR_INVALID_ARGUMENT);

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_config_t invalid = config;

    invalid.struct_size = sizeof(invalid) - 1u;
    assert(spw_port_workspace_requirements(&invalid, &requirements) == SPW_ERR_INVALID_ARGUMENT);
    invalid = config;
    invalid.version = SPW_PORT_CONFIG_VERSION + 1u;
    assert(spw_port_workspace_requirements(&invalid, &requirements) == SPW_ERR_UNSUPPORTED);
    invalid = config;
    invalid.flags = 1u;
    assert(spw_port_workspace_requirements(&invalid, &requirements) == SPW_ERR_UNSUPPORTED);
    invalid = config;
    invalid.backend = 0xffffffffu;
    assert(spw_port_workspace_requirements(&invalid, &requirements) == SPW_ERR_UNSUPPORTED);
    invalid = config;
    invalid.backend_config = &invalid;
    invalid.backend_config_size = sizeof(invalid);
    assert(spw_port_workspace_requirements(&invalid, &requirements) == SPW_ERR_INVALID_ARGUMENT);

    assert(spw_port_workspace_requirements(&config, NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_workspace_requirements(&config, &requirements) == SPW_OK);
    assert(requirements.size > 0u);
    assert(requirements.alignment > 0u);
    assert((requirements.alignment & (requirements.alignment - 1u)) == 0u);
    assert(requirements.alignment <= WORKSPACE_ALIGNMENT_SLACK);

    /*
     * Align from the requirement reported by SpWKit instead of assuming the C
     * implementation exposes max_align_t. This also verifies that the returned
     * alignment is directly usable by a plain C caller on every CI toolchain.
     */
    uint8_t workspace_storage[WORKSPACE_STORAGE_SIZE + WORKSPACE_ALIGNMENT_SLACK];
    const uintptr_t raw_address = (uintptr_t)workspace_storage;
    const uintptr_t aligned_address =
        (raw_address + requirements.alignment - 1u) &
        ~(uintptr_t)(requirements.alignment - 1u);
    uint8_t* workspace = (uint8_t*)aligned_address;
    const size_t workspace_size =
        sizeof(workspace_storage) - (size_t)(aligned_address - raw_address);
    assert(requirements.size <= workspace_size);

    assert(spw_port_open_in_place(&config, NULL, workspace_size, &port) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_open_in_place(&config, workspace, workspace_size, NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_open_in_place(&config, workspace, requirements.size - 1u, &port) == SPW_ERR_BUFFER_TOO_SMALL);
    if (requirements.alignment > 1u) {
        assert(spw_port_open_in_place(&config, workspace + 1u,
                                      workspace_size - 1u, &port) == SPW_ERR_INVALID_ARGUMENT);
    }

    assert(spw_port_open_in_place(&config, workspace, workspace_size, &port) == SPW_OK);
    assert(port != NULL);

    spw_link_state_t state = 0xffu;
    spw_capabilities_t caps = {0};
    spw_statistics_t stats = {0};
    uint8_t byte = 0x42u;
    spw_packet_t packet = {&byte, 1u, 1u, SPW_TERMINATOR_EOP};

    assert(spw_port_start(NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_stop(NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_reset(NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_get_link_state(NULL, &state) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_get_link_state(port, NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_get_capabilities(NULL, &caps) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_get_capabilities(port, NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_send(NULL, &packet, 0u) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_send(port, NULL, 0u) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_receive(NULL, &packet, 0u) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_receive(port, NULL, 0u) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_get_statistics(NULL, &stats) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_get_statistics(port, NULL) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_clear_statistics(NULL) == SPW_ERR_INVALID_ARGUMENT);

    assert(spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_STATE);
    assert(spw_port_receive(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_STATE);

    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_RUN);
    assert(spw_port_get_capabilities(port, &caps) == SPW_OK);
    assert(caps.max_packet_size > 0u && caps.max_packet_size <= TEST_STORAGE_SIZE);

    spw_packet_t invalid_packet = {&byte, 1u, 1u, (spw_terminator_t)9u};
    assert(spw_port_send(port, &invalid_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_PACKET);
    invalid_packet = (spw_packet_t){NULL, 1u, 1u, SPW_TERMINATOR_EOP};
    assert(spw_port_send(port, &invalid_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_ARGUMENT);
    invalid_packet = (spw_packet_t){&byte, 2u, 1u, SPW_TERMINATOR_EOP};
    assert(spw_port_send(port, &invalid_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_PACKET);
    invalid_packet = (spw_packet_t){&byte, caps.max_packet_size + 1u,
                                    caps.max_packet_size + 1u, SPW_TERMINATOR_EOP};
    assert(spw_port_send(port, &invalid_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_PACKET);

    spw_packet_t zero_tx = {NULL, 0u, 0u, SPW_TERMINATOR_EEP};
    spw_packet_t zero_rx = {NULL, 0u, 0u, SPW_TERMINATOR_EOP};
    assert(spw_port_send(port, &zero_tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive(port, &zero_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(zero_rx.length == 0u && zero_rx.terminator == SPW_TERMINATOR_EEP);

    static uint8_t max_tx[TEST_STORAGE_SIZE];
    static uint8_t max_rx[TEST_STORAGE_SIZE];
    for (size_t i = 0u; i < caps.max_packet_size; ++i) {
        max_tx[i] = (uint8_t)((i * 29u + 7u) & 0xffu);
    }
    spw_packet_t max_packet = {max_tx, caps.max_packet_size,
                               caps.max_packet_size, SPW_TERMINATOR_EOP};
    assert(spw_port_send(port, &max_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    spw_packet_t max_received = {max_rx, 0u, caps.max_packet_size, SPW_TERMINATOR_EEP};
    assert(spw_port_receive(port, &max_received, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(max_received.length == caps.max_packet_size);
    assert(max_received.terminator == SPW_TERMINATOR_EOP);
    assert(memcmp(max_tx, max_rx, caps.max_packet_size) == 0);

    uint8_t three[] = {1u, 2u, 3u};
    spw_packet_t three_tx = {three, 3u, 3u, SPW_TERMINATOR_EEP};
    assert(spw_port_send(port, &three_tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    uint8_t guard[2] = {0xaau, 0xbbu};
    spw_packet_t short_rx = {guard, 0u, 2u, SPW_TERMINATOR_EOP};
    assert(spw_port_receive(port, &short_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_BUFFER_TOO_SMALL);
    assert(short_rx.length == 3u);
    assert(short_rx.terminator == SPW_TERMINATOR_EEP);
    assert(guard[0] == 0xaau && guard[1] == 0xbbu);
    uint8_t exact[3] = {0u, 0u, 0u};
    spw_packet_t exact_rx = {exact, 0u, sizeof(exact), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(port, &exact_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(memcmp(exact, three, sizeof(three)) == 0);

    assert(spw_port_send(port, &three_tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    spw_packet_t null_rx = {NULL, 0u, 3u, SPW_TERMINATOR_EOP};
    assert(spw_port_receive(port, &null_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_ARGUMENT);
    exact_rx.length = 0u;
    assert(spw_port_receive(port, &exact_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);

    assert(caps.rx_queue_depth > 0u && caps.rx_queue_depth <= 1024u);
    for (size_t i = 0u; i < caps.rx_queue_depth; ++i) {
        assert(spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }
    assert(spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_RESOURCE_EXHAUSTED);
    for (size_t i = 0u; i < caps.rx_queue_depth; ++i) {
        uint8_t value = 0u;
        spw_packet_t rx = {&value, 0u, 1u, SPW_TERMINATOR_EEP};
        assert(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }
    assert(spw_port_receive(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);

    spw_time_code_t tc = {63u, 0u};
    assert(spw_port_send_time_code(port, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    spw_time_code_t tc_rx = {0u, 0u};
    assert(spw_port_receive_time_code(port, &tc_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(tc_rx.time_count == 63u && tc_rx.control_flags == 0u);
    tc.time_count = 64u;
    assert(spw_port_send_time_code(port, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_ARGUMENT);
    tc.time_count = 0u;
    tc.control_flags = 1u;
    assert(spw_port_send_time_code(port, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_send_time_code(NULL, &tc, 0u) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_send_time_code(port, NULL, 0u) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_receive_time_code(NULL, &tc_rx, 0u) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_receive_time_code(port, NULL, 0u) == SPW_ERR_INVALID_ARGUMENT);

    assert(spw_port_clear_statistics(port) == SPW_OK);
    assert(spw_port_get_statistics(port, &stats) == SPW_OK);
    assert(stats.tx_packets == 0u && stats.rx_packets == 0u &&
           stats.tx_bytes == 0u && stats.rx_bytes == 0u);

    assert(spw_port_stop(port) == SPW_OK);
    assert(spw_port_stop(port) == SPW_OK);
    assert(spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_STATE);
    assert(spw_port_reset(port) == SPW_OK);
    assert(spw_port_reset(port) == SPW_OK);
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_ERROR_RESET);

    assert(spw_port_close(port) == SPW_OK);
    return 0;
}
