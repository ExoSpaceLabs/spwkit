// SPDX-License-Identifier: Apache-2.0

#include "backends/driver/driver_backend.h"

#include <spwkit/driver.h>

#include <stdalign.h>
#include <string.h>

typedef struct spw_driver_backend {
    const spw_driver_ops_t* ops;
    void* driver_context;
} spw_driver_backend_t;

static spw_result_t driver_construct(void* raw,
                                     const spw_port_config_t* config) {
    spw_driver_backend_t* backend = (spw_driver_backend_t*)raw;
    const spw_driver_config_t* driver =
        (const spw_driver_config_t*)config->backend_config;
    memset(backend, 0, sizeof(*backend));
    backend->ops = driver->ops;
    backend->driver_context = driver->driver_context;
    return SPW_OK;
}

static void driver_destroy(void* raw) {
    spw_driver_backend_t* backend = (spw_driver_backend_t*)raw;
    backend->ops = NULL;
    backend->driver_context = NULL;
}

static spw_result_t driver_start(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->start(b->driver_context);
}

static spw_result_t driver_stop(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->stop(b->driver_context);
}

static spw_result_t driver_reset(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->reset(b->driver_context);
}

static spw_result_t driver_get_link_state(const void* raw,
                                          spw_link_state_t* out_state) {
    const spw_driver_backend_t* b = (const spw_driver_backend_t*)raw;
    return b->ops->get_link_state(b->driver_context, out_state);
}

static spw_result_t driver_get_capabilities(
    const void* raw,
    spw_capabilities_t* out_capabilities) {
    const spw_driver_backend_t* b = (const spw_driver_backend_t*)raw;
    spw_result_t result =
        b->ops->get_capabilities(b->driver_context, out_capabilities);
    if (result != SPW_OK) {
        return result;
    }
    if ((out_capabilities->bits & SPW_CAP_ZERO_COPY) != 0u) {
        /* DMA/zero-copy mapping is the next driver-contract slice. */
        return SPW_ERR_UNSUPPORTED;
    }
    if ((out_capabilities->bits & SPW_CAP_FAULT_INJECTION) != 0u) {
        return SPW_ERR_UNSUPPORTED;
    }
    if ((out_capabilities->bits & SPW_CAP_TIME_CODE) != 0u &&
        (b->ops->send_time_code == NULL ||
         b->ops->receive_time_code == NULL)) {
        return SPW_ERR_BACKEND;
    }
    if ((out_capabilities->bits & SPW_CAP_STATISTICS) != 0u &&
        (b->ops->get_statistics == NULL ||
         b->ops->clear_statistics == NULL)) {
        return SPW_ERR_BACKEND;
    }
    if ((out_capabilities->bits & SPW_CAP_READINESS) != 0u &&
        b->ops->wait == NULL) {
        return SPW_ERR_BACKEND;
    }
    if (b->ops->wait == NULL) {
        out_capabilities->bits &= ~SPW_CAP_READINESS;
    }
    return SPW_OK;
}

static bool driver_supports_zero_copy(const void* raw) {
    (void)raw;
    return false;
}

static spw_result_t driver_send(void* raw,
                                const spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->send(b->driver_context, packet, timeout_us);
}

static spw_result_t driver_receive(void* raw,
                                   spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->receive(b->driver_context, packet, timeout_us);
}

static spw_result_t driver_send_time_code(void* raw,
                                          const spw_time_code_t* code,
                                          spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->send_time_code != NULL
               ? b->ops->send_time_code(b->driver_context, code, timeout_us)
               : SPW_ERR_UNSUPPORTED;
}

static spw_result_t driver_receive_time_code(void* raw,
                                             spw_time_code_t* code,
                                             spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->receive_time_code != NULL
               ? b->ops->receive_time_code(b->driver_context, code, timeout_us)
               : SPW_ERR_UNSUPPORTED;
}

static spw_result_t driver_get_statistics(
    const void* raw,
    spw_statistics_t* out_statistics) {
    const spw_driver_backend_t* b = (const spw_driver_backend_t*)raw;
    if (b->ops->get_statistics == NULL) {
        memset(out_statistics, 0, sizeof(*out_statistics));
        return SPW_ERR_UNSUPPORTED;
    }
    return b->ops->get_statistics(b->driver_context, out_statistics);
}

static spw_result_t driver_clear_statistics(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->clear_statistics != NULL
               ? b->ops->clear_statistics(b->driver_context)
               : SPW_ERR_UNSUPPORTED;
}

static spw_result_t driver_wait(void* raw,
                                spw_ready_events_t interests,
                                spw_timeout_us_t timeout_us,
                                spw_ready_events_t* out_ready) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->wait != NULL
               ? b->ops->wait(b->driver_context,
                              interests,
                              timeout_us,
                              out_ready)
               : SPW_ERR_UNSUPPORTED;
}

#define DRIVER_COMMON_OPS(wait_) \
    { driver_start, driver_stop, driver_reset, driver_get_link_state, \
      driver_get_capabilities, driver_supports_zero_copy, driver_send, \
      driver_receive, driver_send_time_code, driver_receive_time_code, \
      driver_get_statistics, driver_clear_statistics, NULL, NULL, NULL, \
      NULL, NULL, NULL, NULL, NULL, (wait_) }

static const spw_backend_ops_t DRIVER_OPS = DRIVER_COMMON_OPS(NULL);
static const spw_backend_ops_t DRIVER_WAIT_OPS =
    DRIVER_COMMON_OPS(driver_wait);

static const spw_backend_factory_t DRIVER_FACTORY = {
    sizeof(spw_driver_backend_t),
    alignof(spw_driver_backend_t),
    driver_construct,
    driver_destroy,
    &DRIVER_OPS
};

static const spw_backend_factory_t DRIVER_WAIT_FACTORY = {
    sizeof(spw_driver_backend_t),
    alignof(spw_driver_backend_t),
    driver_construct,
    driver_destroy,
    &DRIVER_WAIT_OPS
};

const spw_backend_factory_t* spw_driver_backend_factory(bool enable_wait) {
    return enable_wait ? &DRIVER_WAIT_FACTORY : &DRIVER_FACTORY;
}
