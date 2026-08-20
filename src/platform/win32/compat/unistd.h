// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_WIN32_COMPAT_UNISTD_H
#define SPWKIT_WIN32_COMPAT_UNISTD_H

#include "spwkit_win32_socket_compat.h"

#include <process.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define close spw_win32_close
#define getpid _getpid
#define clock_gettime spw_win32_clock_gettime
#define nanosleep spw_win32_nanosleep
#define usleep spw_win32_usleep

#endif
