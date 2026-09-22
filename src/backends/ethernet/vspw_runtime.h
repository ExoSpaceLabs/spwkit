// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPW_RUNTIME_H
#define SPWKIT_VSPW_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include <spwkit/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spw_vspw_runtime_ops {
    uint64_t (*now_us)(void* context);
    spw_result_t (*delay_us)(void* context,
                             uint64_t delay_us,
                             spw_timeout_us_t timeout_us);
} spw_vspw_runtime_ops_t;

typedef struct spw_vspw_runtime {
    const spw_vspw_runtime_ops_t* ops;
    void* context;
} spw_vspw_runtime_t;

#define SPW_VSPW_RUNTIME_INITIALIZER {NULL, NULL}

bool spw_vspw_runtime_valid(const spw_vspw_runtime_t* runtime);
uint64_t spw_vspw_runtime_now_us(const spw_vspw_runtime_t* runtime);
spw_result_t spw_vspw_runtime_delay_us(const spw_vspw_runtime_t* runtime,
                                       uint64_t delay_us,
                                       spw_timeout_us_t timeout_us);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPW_RUNTIME_H */
