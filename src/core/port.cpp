// SPDX-License-Identifier: Apache-2.0

#include <spwkit/port.h>
#include <spwkit/simulator.h>

#include "backends/loopback/loopback_backend.hpp"
#include "core/backend.hpp"

#ifdef SPWKIT_HAS_SIMULATOR
#include "backends/virtual/simulator_backend.hpp"
#endif

#include <new>

struct spw_port {
    spwkit::detail::Backend* backend;
    void (*destroy_backend)(spwkit::detail::Backend*) noexcept;
};

namespace {

void destroy_loopback(spwkit::detail::Backend* backend) noexcept {
    delete static_cast<spwkit::detail::LoopbackBackend*>(backend);
}

#ifdef SPWKIT_HAS_SIMULATOR
void destroy_simulator(spwkit::detail::Backend* backend) noexcept {
    delete static_cast<spwkit::detail::SimulatorBackend*>(backend);
}
#endif

spw_result_t validate_common_config(const spw_port_config_t* config) noexcept {
    if (config == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (config->struct_size < sizeof(spw_port_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (config->version != SPW_PORT_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (config->flags != 0u) {
        return SPW_ERR_UNSUPPORTED;
    }
    return SPW_OK;
}

spw_result_t validate_simulator_config(const spw_port_config_t* config) noexcept {
    if (config->backend_config == nullptr ||
        config->backend_config_size < sizeof(spw_simulator_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    const auto* simulator = static_cast<const spw_simulator_config_t*>(config->backend_config);
    if (simulator->struct_size < sizeof(spw_simulator_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (simulator->version != SPW_SIMULATOR_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (simulator->endpoint != SPW_SIMULATOR_ENDPOINT_A &&
        simulator->endpoint != SPW_SIMULATOR_ENDPOINT_B) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return SPW_OK;
}

spw_result_t validate_port(const spw_port_t* port) noexcept {
    return (port != nullptr && port->backend != nullptr) ? SPW_OK : SPW_ERR_INVALID_ARGUMENT;
}

} // namespace

extern "C" {

spw_result_t spw_port_open(const spw_port_config_t* config, spw_port_t** out_port) {
    if (out_port == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_port = nullptr;

    const spw_result_t common = validate_common_config(config);
    if (common != SPW_OK) {
        return common;
    }

    spwkit::detail::Backend* backend = nullptr;
    void (*destroy_backend)(spwkit::detail::Backend*) noexcept = nullptr;

    switch (config->backend) {
    case SPW_BACKEND_LOOPBACK:
        if (config->backend_config != nullptr || config->backend_config_size != 0u) {
            return SPW_ERR_INVALID_ARGUMENT;
        }
        backend = new (std::nothrow) spwkit::detail::LoopbackBackend();
        destroy_backend = &destroy_loopback;
        break;

    case SPW_BACKEND_SIMULATOR: {
        const spw_result_t result = validate_simulator_config(config);
        if (result != SPW_OK) {
            return result;
        }
#ifdef SPWKIT_HAS_SIMULATOR
        const auto* simulator = static_cast<const spw_simulator_config_t*>(config->backend_config);
        auto* simulator_backend = new (std::nothrow) spwkit::detail::SimulatorBackend(*simulator);
        if (simulator_backend == nullptr) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        const spw_result_t attach_result = simulator_backend->attach();
        if (attach_result != SPW_OK) {
            delete simulator_backend;
            return attach_result;
        }
        backend = simulator_backend;
        destroy_backend = &destroy_simulator;
        break;
#else
        return SPW_ERR_UNSUPPORTED;
#endif
    }

    default:
        return SPW_ERR_UNSUPPORTED;
    }

    if (backend == nullptr) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    spw_port_t* port = new (std::nothrow) spw_port_t{backend, destroy_backend};
    if (port == nullptr) {
        destroy_backend(backend);
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    *out_port = port;
    return SPW_OK;
}

spw_result_t spw_port_close(spw_port_t* port) {
    if (validate_port(port) != SPW_OK) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    port->destroy_backend(port->backend);
    port->backend = nullptr;
    delete port;
    return SPW_OK;
}

spw_result_t spw_port_start(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->start() : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_stop(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->stop() : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_reset(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->reset() : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_get_link_state(const spw_port_t* port, spw_link_state_t* out_state) {
    if (validate_port(port) != SPW_OK || out_state == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->get_link_state(*out_state);
}

spw_result_t spw_port_get_capabilities(const spw_port_t* port,
                                       spw_capabilities_t* out_capabilities) {
    if (validate_port(port) != SPW_OK || out_capabilities == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->get_capabilities(*out_capabilities);
}

spw_result_t spw_port_send(spw_port_t* port,
                           const spw_packet_t* packet,
                           spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || packet == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->send(*packet, timeout_us);
}

spw_result_t spw_port_receive(spw_port_t* port,
                              spw_packet_t* packet,
                              spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || packet == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->receive(*packet, timeout_us);
}

spw_result_t spw_port_send_time_code(spw_port_t* port,
                                     const spw_time_code_t* time_code,
                                     spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || time_code == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->send_time_code(*time_code, timeout_us);
}

spw_result_t spw_port_receive_time_code(spw_port_t* port,
                                        spw_time_code_t* time_code,
                                        spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || time_code == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->receive_time_code(*time_code, timeout_us);
}

spw_result_t spw_port_get_statistics(const spw_port_t* port,
                                     spw_statistics_t* out_statistics) {
    if (validate_port(port) != SPW_OK || out_statistics == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->get_statistics(*out_statistics);
}

spw_result_t spw_port_clear_statistics(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->clear_statistics()
                                         : SPW_ERR_INVALID_ARGUMENT;
}

} // extern "C"
