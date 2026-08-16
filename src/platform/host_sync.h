// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_HOST_SYNC_H
#define SPWKIT_HOST_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef SRWLOCK spw_host_mutex_t;
typedef CONDITION_VARIABLE spw_host_condition_t;
#define SPW_HOST_MUTEX_STATIC_INITIALIZER SRWLOCK_INIT
#else
#include <pthread.h>

typedef pthread_mutex_t spw_host_mutex_t;
typedef pthread_cond_t spw_host_condition_t;
#define SPW_HOST_MUTEX_STATIC_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool spw_host_mutex_init(spw_host_mutex_t* mutex);
void spw_host_mutex_destroy(spw_host_mutex_t* mutex);
void spw_host_mutex_lock(spw_host_mutex_t* mutex);
void spw_host_mutex_unlock(spw_host_mutex_t* mutex);

bool spw_host_condition_init(spw_host_condition_t* condition);
void spw_host_condition_destroy(spw_host_condition_t* condition);
void spw_host_condition_broadcast(spw_host_condition_t* condition);

/*
 * Wait while atomically releasing/reacquiring mutex.
 * Returns true when signalled, false on timeout/error.
 */
bool spw_host_condition_wait(spw_host_condition_t* condition,
                             spw_host_mutex_t* mutex,
                             uint64_t timeout_us,
                             bool infinite);

uint64_t spw_host_now_us(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_HOST_SYNC_H */
