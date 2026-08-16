// SPDX-License-Identifier: Apache-2.0

/*
 * Temporary private selector alias retained until #49 cleans the internal
 * backend-factory names in port.c. It is a C symbol and introduces no C++ ABI.
 */
#include "backends/ethernet/udp_backend.h"

const spw_backend_factory_t* spw_cpp_udp_backend_factory(void) {
    return spw_udp_backend_factory();
}
