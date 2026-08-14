// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_API_H
#define SPWKIT_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPWKIT_API_VERSION_MAJOR 0u
#define SPWKIT_API_VERSION_MINOR 1u
#define SPWKIT_API_VERSION_PATCH 0u

/**
 * @brief Result value returned by the public C API.
 *
 * Concrete result codes are defined as part of the core public types. The
 * underlying type is fixed here so the function ABI does not depend on a C
 * compiler's enum representation.
 */
typedef int32_t spw_result_t;

/**
 * @brief Timeout value expressed in microseconds.
 *
 * Special timeout constants are defined with the core public types. Keeping
 * the unit fixed avoids exposing POSIX-specific time structures.
 */
typedef uint64_t spw_timeout_us_t;

/* Public handles are opaque. Their representation belongs to the backend. */
typedef struct spw_port spw_port_t;

/* Public value/configuration types are completed by the core-types layer. */
typedef struct spw_port_config spw_port_config_t;
typedef struct spw_packet spw_packet_t;
typedef struct spw_link_state spw_link_state_t;
typedef struct spw_capabilities spw_capabilities_t;
typedef struct spw_statistics spw_statistics_t;
typedef struct spw_time_code spw_time_code_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_API_H */
