// SPDX-License-Identifier: Apache-2.0

#include "platform/win32/compat/spwkit_win32_socket_compat.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum { SPW_WIN32_SOCKET_TABLE_SIZE = 64 };

typedef struct spw_win32_socket_slot {
    SOCKET socket;
    bool used;
} spw_win32_socket_slot_t;

static INIT_ONCE g_winsock_once = INIT_ONCE_STATIC_INIT;
static SRWLOCK g_socket_lock = SRWLOCK_INIT;
static spw_win32_socket_slot_t g_socket_slots[SPW_WIN32_SOCKET_TABLE_SIZE];
static bool g_winsock_started = false;

static void set_errno_from_wsa(int error) {
    switch (error) {
    case WSAEINTR:
        errno = EINTR;
        break;
    case WSAEWOULDBLOCK:
        errno = EAGAIN;
        break;
    case WSAETIMEDOUT:
        errno = ETIMEDOUT;
        break;
    case WSAEINVAL:
        errno = EINVAL;
        break;
    case WSAEMSGSIZE:
        errno = EMSGSIZE;
        break;
    case WSAEADDRINUSE:
        errno = EADDRINUSE;
        break;
    case WSAEBADF:
    case WSAENOTSOCK:
        errno = EBADF;
        break;
    default:
        errno = EIO;
        break;
    }
}

static void cleanup_winsock(void) {
    AcquireSRWLockExclusive(&g_socket_lock);
    for (size_t i = 0u; i < SPW_WIN32_SOCKET_TABLE_SIZE; ++i) {
        if (g_socket_slots[i].used) {
            (void)closesocket(g_socket_slots[i].socket);
            g_socket_slots[i].used = false;
            g_socket_slots[i].socket = INVALID_SOCKET;
        }
    }
    ReleaseSRWLockExclusive(&g_socket_lock);
    if (g_winsock_started) {
        (void)WSACleanup();
        g_winsock_started = false;
    }
}

static BOOL CALLBACK initialize_winsock(PINIT_ONCE once,
                                        PVOID parameter,
                                        PVOID* context) {
    WSADATA data;
    (void)once;
    (void)parameter;
    (void)context;
    memset(&data, 0, sizeof(data));
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return FALSE;
    }
    g_winsock_started = true;
    if (atexit(cleanup_winsock) != 0) {
        (void)WSACleanup();
        g_winsock_started = false;
        return FALSE;
    }
    return TRUE;
}

static bool ensure_winsock(void) {
    PVOID context = NULL;
    if (!InitOnceExecuteOnce(&g_winsock_once, initialize_winsock, NULL, &context)) {
        errno = EIO;
        return false;
    }
    return true;
}

static int store_socket(SOCKET socket_value) {
    int fd = -1;
    AcquireSRWLockExclusive(&g_socket_lock);
    for (size_t i = 0u; i < SPW_WIN32_SOCKET_TABLE_SIZE; ++i) {
        if (!g_socket_slots[i].used) {
            g_socket_slots[i].socket = socket_value;
            g_socket_slots[i].used = true;
            fd = (int)i + 3;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_socket_lock);
    if (fd < 0) {
        errno = EMFILE;
    }
    return fd;
}

static SOCKET lookup_socket(int fd) {
    SOCKET socket_value = INVALID_SOCKET;
    const int index = fd - 3;
    if (index < 0 || index >= SPW_WIN32_SOCKET_TABLE_SIZE) {
        errno = EBADF;
        return INVALID_SOCKET;
    }
    AcquireSRWLockShared(&g_socket_lock);
    if (g_socket_slots[index].used) {
        socket_value = g_socket_slots[index].socket;
    }
    ReleaseSRWLockShared(&g_socket_lock);
    if (socket_value == INVALID_SOCKET) {
        errno = EBADF;
    }
    return socket_value;
}

int spw_win32_socket(int domain, int type, int protocol) {
    SOCKET native_socket;
    int fd;
    if (!ensure_winsock()) {
        return -1;
    }
    native_socket = socket(domain, type, protocol);
    if (native_socket == INVALID_SOCKET) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    fd = store_socket(native_socket);
    if (fd < 0) {
        (void)closesocket(native_socket);
    }
    return fd;
}

int spw_win32_bind(int fd, const struct sockaddr* address, int address_size) {
    const SOCKET native_socket = lookup_socket(fd);
    int result;
    if (native_socket == INVALID_SOCKET) {
        return -1;
    }
    result = bind(native_socket, address, address_size);
    if (result == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    return 0;
}

int spw_win32_setsockopt(int fd,
                         int level,
                         int option_name,
                         const void* option_value,
                         int option_size) {
    const SOCKET native_socket = lookup_socket(fd);
    int result;
    if (native_socket == INVALID_SOCKET) {
        return -1;
    }
    result = setsockopt(native_socket, level, option_name,
                        (const char*)option_value, option_size);
    if (result == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    return 0;
}

intptr_t spw_win32_sendto(int fd,
                          const void* bytes,
                          size_t size,
                          int flags,
                          const struct sockaddr* destination,
                          int destination_size) {
    const SOCKET native_socket = lookup_socket(fd);
    int result;
    if (native_socket == INVALID_SOCKET) {
        return -1;
    }
    if (size > (size_t)INT_MAX) {
        errno = EMSGSIZE;
        return -1;
    }
    result = sendto(native_socket, (const char*)bytes, (int)size, flags,
                    destination, destination_size);
    if (result == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    return (intptr_t)result;
}

intptr_t spw_win32_recvfrom(int fd,
                            void* bytes,
                            size_t capacity,
                            int flags,
                            struct sockaddr* source,
                            int* source_size) {
    const SOCKET native_socket = lookup_socket(fd);
    int result;
    int native_source_size = source_size != NULL ? *source_size : 0;
    if (native_socket == INVALID_SOCKET) {
        return -1;
    }
    if (capacity > (size_t)INT_MAX) {
        errno = EMSGSIZE;
        return -1;
    }
    result = recvfrom(native_socket, (char*)bytes, (int)capacity, flags,
                      source, source_size != NULL ? &native_source_size : NULL);
    if (result == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    if (source_size != NULL) {
        *source_size = native_source_size;
    }
    return (intptr_t)result;
}

int spw_win32_close(int fd) {
    const int index = fd - 3;
    SOCKET native_socket = INVALID_SOCKET;
    int result;
    if (index < 0 || index >= SPW_WIN32_SOCKET_TABLE_SIZE) {
        errno = EBADF;
        return -1;
    }
    AcquireSRWLockExclusive(&g_socket_lock);
    if (g_socket_slots[index].used) {
        native_socket = g_socket_slots[index].socket;
        g_socket_slots[index].used = false;
        g_socket_slots[index].socket = INVALID_SOCKET;
    }
    ReleaseSRWLockExclusive(&g_socket_lock);
    if (native_socket == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    result = closesocket(native_socket);
    if (result == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    return 0;
}

int spw_win32_inet_pton(int family, const char* text, void* address) {
    int result;
    if (!ensure_winsock()) {
        return -1;
    }
    result = InetPtonA(family, text, address);
    if (result < 0) {
        set_errno_from_wsa(WSAGetLastError());
    }
    return result;
}

int spw_win32_poll(struct spw_win32_pollfd* descriptors,
                   unsigned long descriptor_count,
                   int timeout_ms) {
    WSAPOLLFD native_descriptors[SPW_WIN32_SOCKET_TABLE_SIZE];
    int result;
    if (descriptors == NULL && descriptor_count != 0u) {
        errno = EINVAL;
        return -1;
    }
    if (descriptor_count > SPW_WIN32_SOCKET_TABLE_SIZE) {
        errno = EINVAL;
        return -1;
    }
    if (!ensure_winsock()) {
        return -1;
    }
    memset(native_descriptors, 0, sizeof(native_descriptors));
    for (unsigned long i = 0u; i < descriptor_count; ++i) {
        native_descriptors[i].fd = lookup_socket(descriptors[i].fd);
        if (native_descriptors[i].fd == INVALID_SOCKET) {
            return -1;
        }
        native_descriptors[i].events = descriptors[i].events;
        descriptors[i].revents = 0;
    }
    result = WSAPoll(native_descriptors, descriptor_count, timeout_ms);
    if (result == SOCKET_ERROR) {
        set_errno_from_wsa(WSAGetLastError());
        return -1;
    }
    for (unsigned long i = 0u; i < descriptor_count; ++i) {
        descriptors[i].revents = native_descriptors[i].revents;
    }
    return result;
}

int spw_win32_clock_gettime(int clock_id, struct timespec* out_time) {
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    uint64_t seconds;
    uint64_t remainder;
    if (clock_id != 1 || out_time == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!QueryPerformanceFrequency(&frequency) ||
        !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0) {
        errno = EIO;
        return -1;
    }
    seconds = (uint64_t)(counter.QuadPart / frequency.QuadPart);
    remainder = (uint64_t)(counter.QuadPart % frequency.QuadPart);
    out_time->tv_sec = (time_t)seconds;
    out_time->tv_nsec = (long)((remainder * UINT64_C(1000000000)) /
                               (uint64_t)frequency.QuadPart);
    return 0;
}

int spw_win32_nanosleep(const struct timespec* request,
                        struct timespec* remaining) {
    HANDLE timer;
    LARGE_INTEGER due_time;
    uint64_t nanoseconds;
    uint64_t ticks_100ns;
    DWORD wait_result;
    if (request == NULL || request->tv_sec < 0 || request->tv_nsec < 0 ||
        request->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    if (remaining != NULL) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }
    nanoseconds = (uint64_t)request->tv_sec * UINT64_C(1000000000) +
                  (uint64_t)request->tv_nsec;
    if (nanoseconds == 0u) {
        return 0;
    }
    ticks_100ns = (nanoseconds + 99u) / 100u;
    if (ticks_100ns > (uint64_t)INT64_MAX) {
        errno = EINVAL;
        return -1;
    }
    due_time.QuadPart = -(LONGLONG)ticks_100ns;
    timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    if (timer == NULL) {
        errno = EIO;
        return -1;
    }
    if (!SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE)) {
        (void)CloseHandle(timer);
        errno = EIO;
        return -1;
    }
    wait_result = WaitForSingleObject(timer, INFINITE);
    (void)CloseHandle(timer);
    if (wait_result != WAIT_OBJECT_0) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int spw_win32_usleep(unsigned long microseconds) {
    struct timespec request;
    request.tv_sec = (time_t)(microseconds / 1000000ul);
    request.tv_nsec = (long)((microseconds % 1000000ul) * 1000ul);
    return spw_win32_nanosleep(&request, NULL);
}
