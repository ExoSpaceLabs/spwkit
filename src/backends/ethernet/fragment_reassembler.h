// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_FRAGMENT_REASSEMBLER_H
#define SPWKIT_FRAGMENT_REASSEMBLER_H

#include "backends/ethernet/vspw_tp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t spw_reassembly_result_t;
enum {
    SPW_REASSEMBLY_ACCEPTED = 0u,
    SPW_REASSEMBLY_DUPLICATE,
    SPW_REASSEMBLY_COMPLETE,
    SPW_REASSEMBLY_CONFLICT,
    SPW_REASSEMBLY_INVALID
};

typedef struct spw_fragment_reassembler {
    uint8_t* data;
    uint64_t* coverage;
    size_t capacity;
    size_t coverage_words;
    size_t covered_bytes;
    uint32_t message_id;
    uint32_t total_size;
    uint8_t terminator_flags;
    bool ack_required;
    bool seen_start;
    bool seen_end;
    bool active;
} spw_fragment_reassembler_t;

#define SPW_FRAGMENT_COVERAGE_WORDS(capacity_) (((capacity_) + 63u) / 64u)

void spw_fragment_reassembler_init(spw_fragment_reassembler_t* reassembler,
                                   uint8_t* data,
                                   size_t capacity,
                                   uint64_t* coverage,
                                   size_t coverage_words);
void spw_fragment_reassembler_reset(spw_fragment_reassembler_t* reassembler);
spw_reassembly_result_t spw_fragment_reassembler_push(
    spw_fragment_reassembler_t* reassembler,
    const spw_vspw_tp_header_t* header,
    const uint8_t* payload);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_FRAGMENT_REASSEMBLER_H */
