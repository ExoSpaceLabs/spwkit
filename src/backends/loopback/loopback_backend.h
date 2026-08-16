// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_LOOPBACK_BACKEND_H
#define SPWKIT_LOOPBACK_BACKEND_H

#include "core/backend_c.h"

#ifdef __cplusplus
extern "C" {
#endif

const spw_backend_factory_t* spw_loopback_backend_factory(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_LOOPBACK_BACKEND_H */
