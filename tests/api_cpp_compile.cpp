// SPDX-License-Identifier: Apache-2.0
#include "spwkit/spwkit.h"
#include "spwkit/spwkit.hpp"

#include <type_traits>

static_assert(std::is_same_v<decltype(&spw_port_open),
                             spw_result_t (*)(const spw_port_config_t*, spw_port_t**)>);
static_assert(std::is_same_v<decltype(&spw_port_send),
                             spw_result_t (*)(spw_port_t*, const spw_packet_t*, spw_timeout_us_t)>);

int spwkit_api_cpp_compile_probe() {
    return SPWKIT_API_VERSION_MAJOR == 0u && SPWKIT_API_VERSION_MINOR == 1u ? 0 : 1;
}
