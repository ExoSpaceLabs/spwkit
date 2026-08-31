// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_INTERNAL_DRIVER_BACKEND_H
#define SPWKIT_INTERNAL_DRIVER_BACKEND_H

#include <stdbool.h>

#include "core/backend_c.h"

const spw_backend_factory_t* spw_driver_backend_factory(bool enable_wait);

#endif /* SPWKIT_INTERNAL_DRIVER_BACKEND_H */
