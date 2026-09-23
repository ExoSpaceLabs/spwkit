// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_HOST_VSPW_RUNTIME_H
#define SPWKIT_HOST_VSPW_RUNTIME_H

#include "backends/ethernet/vspw_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spw_host_vspw_runtime_context {
    unsigned reserved;
} spw_host_vspw_runtime_context_t;

void spw_host_vspw_runtime_init(spw_host_vspw_runtime_context_t* context,
                                spw_vspw_runtime_t* out_runtime);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_HOST_VSPW_RUNTIME_H */
