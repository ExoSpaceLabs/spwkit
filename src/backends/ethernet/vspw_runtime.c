// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/vspw_runtime.h"

#include <stddef.h>

bool spw_vspw_runtime_valid(const spw_vspw_runtime_t* runtime) {
    return runtime != NULL && runtime->context != NULL && runtime->ops != NULL &&
           runtime->ops->now_us != NULL && runtime->ops->delay_us != NULL;
}

uint64_t spw_vspw_runtime_now_us(const spw_vspw_runtime_t* runtime) {
    return spw_vspw_runtime_valid(runtime) ? runtime->ops->now_us(runtime->context)
                                           : 0u;
}

spw_result_t spw_vspw_runtime_delay_us(const spw_vspw_runtime_t* runtime,
                                       uint64_t delay_us,
                                       spw_timeout_us_t timeout_us) {
    if (!spw_vspw_runtime_valid(runtime)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    return runtime->ops->delay_us(runtime->context, delay_us, timeout_us);
}
