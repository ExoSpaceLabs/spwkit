// SPDX-License-Identifier: Apache-2.0
#include "backends/device/vspw_device_protocol.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    vspd_header_t header;
    uint8_t encoded[VSPD_HEADER_SIZE];

    (void)vspd_decode_header(data, size, &header);
    (void)vspd_validate_frame(data, size, &header);

    if (vspd_decode_header(data, size, &header) == VSPD_CODEC_OK) {
        (void)vspd_encode_header(&header, encoded);
    }

    return 0;
}
