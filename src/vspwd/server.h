// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPWD_SERVER_H
#define SPWKIT_VSPWD_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#define VSPWD_DEFAULT_SOCKET_PATH "/tmp/spwkit-vspwd.sock"

/* Run until SIGINT/SIGTERM or a fatal server error. Returns 0 on clean stop. */
int vspwd_run(const char* socket_path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPWD_SERVER_H */
