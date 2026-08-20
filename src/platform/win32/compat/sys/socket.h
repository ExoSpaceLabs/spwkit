// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_WIN32_COMPAT_SYS_SOCKET_H
#define SPWKIT_WIN32_COMPAT_SYS_SOCKET_H

#include "spwkit_win32_socket_compat.h"

#include <stdint.h>

typedef int socklen_t;
typedef intptr_t ssize_t;

#define socket spw_win32_socket
#define bind spw_win32_bind
#define setsockopt spw_win32_setsockopt
#define sendto spw_win32_sendto
#define recvfrom spw_win32_recvfrom

#endif
