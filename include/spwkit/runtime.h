// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_RUNTIME_H
#define SPWKIT_RUNTIME_H

#include <stdint.h>

#include "spwkit/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_RUNTIME_OPS_VERSION 1u

/**
 * Platform runtime hooks used by transport-independent protocol engines.
 *
 * These hooks are intentionally separate from any network/driver contract.
 * An RTOS integration may implement them with its own monotonic clock and
 * scheduler delay primitives; SpWKit takes no dependency on that RTOS.
 */
typedef struct spw_runtime_ops {
    uint32_t struct_size;
    uint32_t version;

    uint64_t (*now_us)(const void* runtime_context);
    spw_result_t (*delay_us)(void* runtime_context,
                             uint64_t delay_us,
                             spw_timeout_us_t timeout_us);
} spw_runtime_ops_t;

#define SPW_RUNTIME_OPS_INITIALIZER \
    { sizeof(spw_runtime_ops_t), SPW_RUNTIME_OPS_VERSION, NULL, NULL }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_RUNTIME_H */
