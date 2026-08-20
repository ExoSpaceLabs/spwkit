// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_WIN32_SOCKET_COMPAT_H
#define SPWKIT_WIN32_SOCKET_COMPAT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spw_win32_pollfd {
    int fd;
    short events;
    short revents;
};

int spw_win32_socket(int domain, int type, int protocol);
int spw_win32_bind(int fd, const struct sockaddr* address, int address_size);
int spw_win32_setsockopt(int fd,
                         int level,
                         int option_name,
                         const void* option_value,
                         int option_size);
intptr_t spw_win32_sendto(int fd,
                          const void* bytes,
                          size_t size,
                          int flags,
                          const struct sockaddr* destination,
                          int destination_size);
intptr_t spw_win32_recvfrom(int fd,
                            void* bytes,
                            size_t capacity,
                            int flags,
                            struct sockaddr* source,
                            int* source_size);
int spw_win32_close(int fd);
int spw_win32_inet_pton(int family, const char* text, void* address);
int spw_win32_poll(struct spw_win32_pollfd* descriptors,
                   unsigned long descriptor_count,
                   int timeout_ms);
int spw_win32_clock_gettime(int clock_id, struct timespec* out_time);
int spw_win32_nanosleep(const struct timespec* request,
                        struct timespec* remaining);
int spw_win32_usleep(unsigned long microseconds);

#ifdef __cplusplus
}
#endif

#endif
