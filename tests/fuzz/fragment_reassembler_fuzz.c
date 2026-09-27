// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/fragment_reassembler.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FUZZ_CAPACITY 4096u
#define FUZZ_MAX_FRAGMENTS 32u

static uint32_t read_u16_le(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    uint8_t storage[FUZZ_CAPACITY];
    uint64_t coverage[SPW_FRAGMENT_COVERAGE_WORDS(FUZZ_CAPACITY)];
    spw_fragment_reassembler_t reassembler;
    size_t cursor = 0u;
    unsigned fragment_count = 0u;
    uint32_t total_size;
    uint8_t message_flags;

    memset(storage, 0, sizeof(storage));
    spw_fragment_reassembler_init(
        &reassembler, storage, sizeof(storage), coverage,
        SPW_FRAGMENT_COVERAGE_WORDS(FUZZ_CAPACITY));

    /*
     * Keep message metadata stable across the generated fragment stream so
     * multi-fragment inputs exercise real ordering/overlap/completion behavior
     * instead of immediately collapsing into metadata conflicts.
     *
     * Input layout:
     *   total_size:u16, message_flags:u8,
     *   repeated { offset:u16, length:u16, boundary_flags:u8, payload:length }
     */
    if (size < 3u) {
        spw_fragment_reassembler_reset(&reassembler);
        return 0;
    }

    total_size = 1u + (read_u16_le(data) % FUZZ_CAPACITY);
    message_flags =
        (data[2] & 0x01u) != 0u ? SPW_VSPW_TP_FLAG_EEP : SPW_VSPW_TP_FLAG_EOP;
    if ((data[2] & 0x02u) != 0u) {
        message_flags |= SPW_VSPW_TP_FLAG_ACK_REQUIRED;
    }
    cursor = 3u;

    while (cursor + 5u <= size && fragment_count < FUZZ_MAX_FRAGMENTS) {
        spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
        const uint32_t offset = read_u16_le(data + cursor);
        const uint32_t requested = read_u16_le(data + cursor + 2u);
        const uint8_t boundary_flags = data[cursor + 4u] &
            (SPW_VSPW_TP_FLAG_FRAGMENT_START |
             SPW_VSPW_TP_FLAG_FRAGMENT_END);
        const size_t available = size - (cursor + 5u);
        const uint32_t available_u16 =
            available > UINT16_MAX ? UINT16_MAX : (uint32_t)available;
        const uint32_t length =
            requested < available_u16 ? requested : available_u16;

        cursor += 5u;

        header.type = SPW_VSPW_TP_DATA;
        header.flags = message_flags | boundary_flags;
        header.payload_size = (uint16_t)length;
        header.session_id = 1u;
        header.message_id = 1u;
        header.fragment_offset = offset;
        header.total_size = total_size;

        (void)spw_fragment_reassembler_push(
            &reassembler, &header, data + cursor);

        /* Exact replay must remain idempotent and memory-safe. */
        (void)spw_fragment_reassembler_push(
            &reassembler, &header, data + cursor);

        cursor += length;
        ++fragment_count;
    }

    spw_fragment_reassembler_reset(&reassembler);
    return 0;
}
