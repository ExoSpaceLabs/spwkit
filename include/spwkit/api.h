// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_API_H
#define SPWKIT_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPWKIT_API_VERSION_MAJOR 0u
#define SPWKIT_API_VERSION_MINOR 2u
#define SPWKIT_API_VERSION_PATCH 0u

/** Result value returned by the public C API. */
typedef int32_t spw_result_t;

/** Timeout value expressed in microseconds. */
typedef uint64_t spw_timeout_us_t;

/* Public handles are opaque. Their representation belongs to libspwkit. */
typedef struct spw_port spw_port_t;
typedef struct spw_buffer spw_buffer_t;

/* Public configuration/value structures are defined by the public type layer. */
typedef struct spw_port_config spw_port_config_t;
typedef struct spw_packet spw_packet_t;
typedef struct spw_buffer_view spw_buffer_view_t;
typedef struct spw_capabilities spw_capabilities_t;
typedef struct spw_statistics spw_statistics_t;
typedef struct spw_fault_statistics spw_fault_statistics_t;
typedef struct spw_time_code spw_time_code_t;
typedef struct spw_port_workspace_requirements spw_port_workspace_requirements_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_API_H */
