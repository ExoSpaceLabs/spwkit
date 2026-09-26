// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/vspw_tp.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    uint8_t encoded[SPW_VSPW_TP_HEADER_SIZE];
    uint64_t acknowledged_session_id = 0u;

    (void)spw_vspw_tp_decode_header(data, size, &header);

    if (size == SPW_VSPW_TP_ACK_PAYLOAD_SIZE) {
        (void)spw_vspw_tp_decode_ack_payload(
            data, size, &acknowledged_session_id);
    }

    if (size >= SPW_VSPW_TP_HEADER_SIZE &&
        spw_vspw_tp_decode_header(data, size, &header) ==
            SPW_VSPW_TP_DECODE_OK) {
        (void)spw_vspw_tp_encode_header(
            &header, encoded, sizeof(encoded));
    }

    return 0;
}
