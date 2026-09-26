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

    memset(storage, 0, sizeof(storage));
    spw_fragment_reassembler_init(
        &reassembler, storage, sizeof(storage), coverage,
        SPW_FRAGMENT_COVERAGE_WORDS(FUZZ_CAPACITY));

    /*
     * Input is a sequence of compact fragment descriptors:
     *   offset:u16, length:u16, flags:u8, payload:length
     * total_size and message_id are shared so the fuzzer concentrates on
     * ordering, duplication, overlap and boundary semantics.
     */
    while (cursor + 5u <= size && fragment_count < FUZZ_MAX_FRAGMENTS) {
        spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
        const uint32_t offset = read_u16_le(data + cursor);
        const uint32_t requested = read_u16_le(data + cursor + 2u);
        const uint8_t raw_flags = data[cursor + 4u];
        const size_t available = size - (cursor + 5u);
        const uint32_t length =
            requested < available ? requested : (uint32_t)available;
        uint32_t total_size;

        cursor += 5u;
        total_size = offset + length;
        if (total_size == 0u || total_size > FUZZ_CAPACITY) {
            if (length > available) {
                break;
            }
            cursor += length;
            ++fragment_count;
            continue;
        }

        /* Keep the message fragmented; unfragmented packets bypass this unit. */
        if (offset == 0u && length == total_size) {
            total_size = total_size < FUZZ_CAPACITY ? total_size + 1u : total_size;
        }

        header.type = SPW_VSPW_TP_DATA;
        header.flags = raw_flags &
            (SPW_VSPW_TP_FLAG_EOP | SPW_VSPW_TP_FLAG_EEP |
             SPW_VSPW_TP_FLAG_FRAGMENT_START |
             SPW_VSPW_TP_FLAG_FRAGMENT_END |
             SPW_VSPW_TP_FLAG_ACK_REQUIRED);
        header.payload_size = (uint16_t)length;
        header.session_id = 1u;
        header.message_id = 1u;
        header.fragment_offset = offset;
        header.total_size = total_size;

        (void)spw_fragment_reassembler_push(
            &reassembler, &header, data + cursor);

        /* Exact replay must never corrupt state, regardless of first result. */
        (void)spw_fragment_reassembler_push(
            &reassembler, &header, data + cursor);

        cursor += length;
        ++fragment_count;
    }

    spw_fragment_reassembler_reset(&reassembler);
    return 0;
}
