// SPDX-License-Identifier: Apache-2.0
#include "spwkit/spwkit.h"
#include "spwkit/spwkit.hpp"

#include <cstdint>
#include <type_traits>

static_assert(std::is_same_v<decltype(&spw_port_open),
                             spw_result_t (*)(const spw_port_config_t*, spw_port_t**)>);
static_assert(std::is_same_v<decltype(&spw_port_send),
                             spw_result_t (*)(spw_port_t*, const spw_packet_t*, spw_timeout_us_t)>);
static_assert(std::is_same_v<decltype(&spw_port_wait),
                             spw_result_t (*)(spw_port_t*, spw_ready_events_t,
                                             spw_timeout_us_t, spw_ready_events_t*)>);
static_assert(SPW_UDP_CONFIG_VERSION == 3u);
static_assert(SPW_DEVICE_CONFIG_VERSION == 1u);
static_assert(SPW_SIMULATOR_CONFIG_VERSION == 1u);

static_assert(sizeof(spw_result_t) == sizeof(std::int32_t));
static_assert(sizeof(spw_timeout_us_t) == sizeof(std::uint64_t));
static_assert(sizeof(spw_terminator_t) == sizeof(std::uint8_t));
static_assert(sizeof(spw_link_state_t) == sizeof(std::uint8_t));
static_assert(sizeof(spw_capability_bits_t) == sizeof(std::uint64_t));
static_assert(spwkit::version.major == SPWKIT_TEST_PROJECT_VERSION_MAJOR);
static_assert(spwkit::version.minor == SPWKIT_TEST_PROJECT_VERSION_MINOR);
static_assert(spwkit::version.patch == SPWKIT_TEST_PROJECT_VERSION_PATCH);
static_assert(!std::is_copy_constructible_v<spwkit::Port>);
static_assert(std::is_move_constructible_v<spwkit::Port>);
static_assert(std::is_same_v<spwkit::Buffer, spw_buffer_t>);
static_assert(std::is_same_v<spwkit::BufferView, spw_buffer_view_t>);
static_assert(std::is_same_v<spwkit::WorkspaceRequirements,
                             spw_port_workspace_requirements_t>);

int spwkit_api_cpp_compile_probe() {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spwkit::WorkspaceRequirements requirements{};
    spwkit::Port port;
    spwkit::Buffer* buffer = nullptr;
    spwkit::BufferView view{};

    (void)spwkit::Port::workspace_requirements(config, requirements);
    (void)port.acquire_tx_buffer(1u, buffer);
    if (buffer != nullptr) {
        (void)spwkit::Port::buffer_view(*buffer, view);
        (void)spwkit::Port::set_packet(*buffer, 0u);
        (void)port.release_tx_buffer(buffer);
    }

    return SPWKIT_API_VERSION_MAJOR == SPWKIT_TEST_PROJECT_VERSION_MAJOR &&
                   SPWKIT_API_VERSION_MINOR == SPWKIT_TEST_PROJECT_VERSION_MINOR &&
                   SPWKIT_API_VERSION_PATCH == SPWKIT_TEST_PROJECT_VERSION_PATCH
               ? 0
               : 1;
}
