// SPDX-License-Identifier: Apache-2.0

#include "platform/host_sync.h"

#ifdef _WIN32

#include <limits.h>

bool spw_host_mutex_init(spw_host_mutex_t* mutex) {
    if (mutex == NULL) {
        return false;
    }
    InitializeSRWLock(mutex);
    return true;
}

void spw_host_mutex_destroy(spw_host_mutex_t* mutex) {
    (void)mutex;
}

void spw_host_mutex_lock(spw_host_mutex_t* mutex) {
    AcquireSRWLockExclusive(mutex);
}

void spw_host_mutex_unlock(spw_host_mutex_t* mutex) {
    ReleaseSRWLockExclusive(mutex);
}

bool spw_host_condition_init(spw_host_condition_t* condition) {
    if (condition == NULL) {
        return false;
    }
    InitializeConditionVariable(condition);
    return true;
}

void spw_host_condition_destroy(spw_host_condition_t* condition) {
    (void)condition;
}

void spw_host_condition_broadcast(spw_host_condition_t* condition) {
    WakeAllConditionVariable(condition);
}

bool spw_host_condition_wait(spw_host_condition_t* condition,
                             spw_host_mutex_t* mutex,
                             uint64_t timeout_us,
                             bool infinite) {
    DWORD timeout_ms;
    if (infinite) {
        timeout_ms = INFINITE;
    } else {
        uint64_t rounded_ms = (timeout_us + 999u) / 1000u;
        if (rounded_ms > (uint64_t)(INFINITE - 1u)) {
            rounded_ms = (uint64_t)(INFINITE - 1u);
        }
        timeout_ms = (DWORD)rounded_ms;
    }

    if (SleepConditionVariableSRW(condition, mutex, timeout_ms, 0)) {
        return true;
    }
    return GetLastError() != ERROR_TIMEOUT ? false : false;
}

uint64_t spw_host_now_us(void) {
    return (uint64_t)GetTickCount64() * 1000u;
}

#else

#include <errno.h>
#include <time.h>

bool spw_host_mutex_init(spw_host_mutex_t* mutex) {
    return mutex != NULL && pthread_mutex_init(mutex, NULL) == 0;
}

void spw_host_mutex_destroy(spw_host_mutex_t* mutex) {
    if (mutex != NULL) {
        (void)pthread_mutex_destroy(mutex);
    }
}

void spw_host_mutex_lock(spw_host_mutex_t* mutex) {
    (void)pthread_mutex_lock(mutex);
}

void spw_host_mutex_unlock(spw_host_mutex_t* mutex) {
    (void)pthread_mutex_unlock(mutex);
}

bool spw_host_condition_init(spw_host_condition_t* condition) {
    return condition != NULL && pthread_cond_init(condition, NULL) == 0;
}

void spw_host_condition_destroy(spw_host_condition_t* condition) {
    if (condition != NULL) {
        (void)pthread_cond_destroy(condition);
    }
}

void spw_host_condition_broadcast(spw_host_condition_t* condition) {
    (void)pthread_cond_broadcast(condition);
}

bool spw_host_condition_wait(spw_host_condition_t* condition,
                             spw_host_mutex_t* mutex,
                             uint64_t timeout_us,
                             bool infinite) {
    int result;
    if (infinite) {
        do {
            result = pthread_cond_wait(condition, mutex);
        } while (result == EINTR);
        return result == 0;
    }

    struct timespec absolute;
    if (clock_gettime(CLOCK_REALTIME, &absolute) != 0) {
        return false;
    }
    absolute.tv_sec += (time_t)(timeout_us / 1000000u);
    absolute.tv_nsec += (long)((timeout_us % 1000000u) * 1000u);
    if (absolute.tv_nsec >= 1000000000L) {
        ++absolute.tv_sec;
        absolute.tv_nsec -= 1000000000L;
    }

    do {
        result = pthread_cond_timedwait(condition, mutex, &absolute);
    } while (result == EINTR);
    return result == 0;
}

uint64_t spw_host_now_us(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

#endif
