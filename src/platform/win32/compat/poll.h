// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_WIN32_COMPAT_POLL_H
#define SPWKIT_WIN32_COMPAT_POLL_H

#include "spwkit_win32_socket_compat.h"

#ifndef POLLIN
#define POLLIN POLLRDNORM
#endif
#ifndef POLLOUT
#define POLLOUT POLLWRNORM
#endif

#define pollfd spw_win32_pollfd
#define poll spw_win32_poll

#endif
