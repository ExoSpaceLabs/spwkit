// SPDX-License-Identifier: Apache-2.0

/*
 * Temporary private symbol compatibility for port.c while #48 still uses the
 * C++-named UDP factory beside it. Remove this alias when the final backend
 * selection cleanup lands with the UDP C conversion.
 */
#include "backends/virtual/simulator_backend.h"

const spw_backend_factory_t* spw_cpp_simulator_backend_factory(void) {
    return spw_simulator_backend_factory();
}
