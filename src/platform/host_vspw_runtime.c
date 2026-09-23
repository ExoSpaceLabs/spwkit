// SPDX-License-Identifier: Apache-2.0

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/host_vspw_runtime.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

static uint64_t host_now_us(void* context) {
    struct timespec now;
    (void)context;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static spw_result_t host_delay_us(void* context,
                                  uint64_t delay_us,
                                  spw_timeout_us_t timeout_us) {
    struct timespec request;
    struct timespec remaining;
    (void)context;

    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }

    request.tv_sec = (time_t)(delay_us / UINT64_C(1000000));
    request.tv_nsec = (long)((delay_us % UINT64_C(1000000)) * UINT64_C(1000));
    while (nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR) {
            return SPW_ERR_BACKEND;
        }
        request = remaining;
    }
    return SPW_OK;
}

static const spw_vspw_runtime_ops_t HOST_RUNTIME_OPS = {
    host_now_us,
    host_delay_us
};

void spw_host_vspw_runtime_init(spw_host_vspw_runtime_context_t* context,
                                spw_vspw_runtime_t* out_runtime) {
    if (context == NULL || out_runtime == NULL) {
        return;
    }
    context->reserved = 0u;
    out_runtime->ops = &HOST_RUNTIME_OPS;
    out_runtime->context = context;
}
