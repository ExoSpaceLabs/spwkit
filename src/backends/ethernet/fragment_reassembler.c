// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/fragment_reassembler.h"

#include <string.h>

static bool covered(const spw_fragment_reassembler_t* reassembler,
                    size_t position) {
    const size_t word = position / 64u;
    const size_t bit = position % 64u;
    return (reassembler->coverage[word] & (UINT64_C(1) << bit)) != 0u;
}

static void mark_covered(spw_fragment_reassembler_t* reassembler,
                         size_t position) {
    const size_t word = position / 64u;
    const size_t bit = position % 64u;
    reassembler->coverage[word] |= UINT64_C(1) << bit;
}

void spw_fragment_reassembler_init(spw_fragment_reassembler_t* reassembler,
                                   uint8_t* data,
                                   size_t capacity,
                                   uint64_t* coverage,
                                   size_t coverage_words) {
    if (reassembler == NULL) {
        return;
    }
    memset(reassembler, 0, sizeof(*reassembler));
    reassembler->data = data;
    reassembler->coverage = coverage;
    reassembler->capacity = capacity;
    reassembler->coverage_words = coverage_words;
    spw_fragment_reassembler_reset(reassembler);
}

void spw_fragment_reassembler_reset(spw_fragment_reassembler_t* reassembler) {
    if (reassembler == NULL) {
        return;
    }
    if (reassembler->coverage != NULL && reassembler->coverage_words != 0u) {
        memset(reassembler->coverage, 0,
               reassembler->coverage_words * sizeof(reassembler->coverage[0]));
    }
    reassembler->covered_bytes = 0u;
    reassembler->message_id = 0u;
    reassembler->total_size = 0u;
    reassembler->terminator_flags = 0u;
    reassembler->ack_required = false;
    reassembler->seen_start = false;
    reassembler->seen_end = false;
    reassembler->active = false;
}

spw_reassembly_result_t spw_fragment_reassembler_push(
    spw_fragment_reassembler_t* reassembler,
    const spw_vspw_tp_header_t* header,
    const uint8_t* payload) {
    uint8_t terminator_flags;
    bool ack_required;
    bool start;
    bool end;
    size_t added = 0u;
    size_t i;

    if (reassembler == NULL || header == NULL || reassembler->data == NULL ||
        reassembler->coverage == NULL ||
        reassembler->coverage_words < SPW_FRAGMENT_COVERAGE_WORDS(reassembler->capacity) ||
        header->type != SPW_VSPW_TP_DATA ||
        header->total_size == header->payload_size ||
        header->total_size == 0u || header->total_size > reassembler->capacity ||
        header->message_id == 0u ||
        (header->payload_size != 0u && payload == NULL) ||
        (uint64_t)header->fragment_offset + header->payload_size >
            header->total_size) {
        return SPW_REASSEMBLY_INVALID;
    }

    terminator_flags = header->flags &
        (SPW_VSPW_TP_FLAG_EOP | SPW_VSPW_TP_FLAG_EEP);
    ack_required = (header->flags & SPW_VSPW_TP_FLAG_ACK_REQUIRED) != 0u;
    start = (header->flags & SPW_VSPW_TP_FLAG_FRAGMENT_START) != 0u;
    end = (header->flags & SPW_VSPW_TP_FLAG_FRAGMENT_END) != 0u;

    if (!reassembler->active) {
        reassembler->active = true;
        reassembler->message_id = header->message_id;
        reassembler->total_size = header->total_size;
        reassembler->terminator_flags = terminator_flags;
        reassembler->ack_required = ack_required;
    } else if (header->message_id != reassembler->message_id ||
               header->total_size != reassembler->total_size ||
               terminator_flags != reassembler->terminator_flags ||
               ack_required != reassembler->ack_required) {
        return SPW_REASSEMBLY_CONFLICT;
    }

    for (i = 0u; i < header->payload_size; ++i) {
        const size_t position = (size_t)header->fragment_offset + i;
        if (covered(reassembler, position) &&
            reassembler->data[position] != payload[i]) {
            return SPW_REASSEMBLY_CONFLICT;
        }
    }

    for (i = 0u; i < header->payload_size; ++i) {
        const size_t position = (size_t)header->fragment_offset + i;
        if (!covered(reassembler, position)) {
            reassembler->data[position] = payload[i];
            mark_covered(reassembler, position);
            ++reassembler->covered_bytes;
            ++added;
        }
    }

    reassembler->seen_start = reassembler->seen_start || start;
    reassembler->seen_end = reassembler->seen_end || end;

    if (reassembler->covered_bytes == reassembler->total_size &&
        reassembler->seen_start && reassembler->seen_end) {
        return SPW_REASSEMBLY_COMPLETE;
    }
    return added == 0u ? SPW_REASSEMBLY_DUPLICATE : SPW_REASSEMBLY_ACCEPTED;
}
