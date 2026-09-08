#!/usr/bin/env python3
from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


PROBES = [
    (9, "SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY"),
    (10, "SPW_PROFILE_ID_TX_ZC_ACQUIRE_PROVIDER_ENTRY"),
    (11, "SPW_PROFILE_ID_TX_ZC_ACQUIRE_PROVIDER_RETURN"),
    (12, "SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_RETURN"),
    (13, "SPW_PROFILE_ID_TX_ZC_SUBMIT_API_ENTRY"),
    (14, "SPW_PROFILE_ID_TX_ZC_SUBMIT_BACKEND_ENTRY"),
    (15, "SPW_PROFILE_ID_TX_ZC_SUBMIT_SYNC_ENTRY"),
    (16, "SPW_PROFILE_ID_TX_ZC_SUBMIT_SYNC_RETURN"),
    (17, "SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_ENTRY"),
    (18, "SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_BOUNDARY"),
    (19, "SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_RETURN"),
    (20, "SPW_PROFILE_ID_TX_ZC_SUBMIT_BACKEND_RETURN"),
    (21, "SPW_PROFILE_ID_TX_ZC_SUBMIT_API_RETURN"),
    (22, "SPW_PROFILE_ID_TX_ZC_RECLAIM_API_ENTRY"),
    (23, "SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_ENTRY"),
    (24, "SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_BOUNDARY"),
    (25, "SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_RETURN"),
    (26, "SPW_PROFILE_ID_TX_ZC_RECLAIM_BACKEND_RETURN"),
    (27, "SPW_PROFILE_ID_TX_ZC_RECLAIM_API_RETURN"),
    (28, "SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY"),
    (29, "SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_ENTRY"),
    (30, "SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_ENTRY"),
    (31, "SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_RETURN"),
    (32, "SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_RETURN"),
    (33, "SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN"),
]


def macro_name(id_name):
    return id_name.replace("SPW_PROFILE_ID_", "SPW_PROFILE_", 1)


# ---------------------------------------------------------------------------
# Profiling vocabulary and compile-out macros.
# ---------------------------------------------------------------------------
path = "src/profiling/profile.h"
text = read(path)
id_anchor = "#define SPW_PROFILE_ID_RX_API_RETURN 8\n"
id_block = id_anchor + "".join(f"#define {name} {number}\n" for number, name in PROBES)
text = replace_once(text, id_anchor, id_block, "profile probe ids")

active_blocks = []
for _, id_name in PROBES:
    macro = macro_name(id_name)
    active_blocks.append(
        f"#if SPWKIT_PROFILE_START == {id_name}\n"
        f"#define {macro}() SPW_PROFILE_CAPTURE_START()\n"
        f"#elif SPWKIT_PROFILE_END == {id_name}\n"
        f"#define {macro}() SPW_PROFILE_CAPTURE_END()\n"
        f"#else\n"
        f"#define {macro}() ((void)0)\n"
        f"#endif\n"
    )
active_insert = "\n".join(active_blocks) + "\n"
active_anchor = "\n#else\n\n#define SPW_PROFILE_TX_API_ENTRY() ((void)0)"
text = replace_once(
    text,
    active_anchor,
    "\n" + active_insert + "#else\n\n#define SPW_PROFILE_TX_API_ENTRY() ((void)0)",
    "active zero-copy probe macros",
)

noop_anchor = "#define SPW_PROFILE_RX_API_RETURN() ((void)0)\n\n#endif\n\n#endif"
noop_block = "#define SPW_PROFILE_RX_API_RETURN() ((void)0)\n" + "".join(
    f"#define {macro_name(name)}() ((void)0)\n" for _, name in PROBES
) + "\n#endif\n\n#endif"
text = replace_once(text, noop_anchor, noop_block, "disabled zero-copy probe macros")
write(path, text)


# ---------------------------------------------------------------------------
# CMake probe-domain validation. Zero-copy operations are separate linear
# domains; allowing a pair to cross ownership operations would manufacture a
# meaningless measurement.
# ---------------------------------------------------------------------------
path = "CMakeLists.txt"
text = read(path)
start = text.index("set(_spwkit_tx_profile_probes")
end = text.index("if(SPWKIT_ENABLE_PROFILING)", start)
new_lists = '''set(_spwkit_tx_profile_probes
    SPW_PROFILE_ID_TX_API_ENTRY
    SPW_PROFILE_ID_TX_BACKEND_ENTRY
    SPW_PROFILE_ID_TX_PROVIDER_ENTRY
    SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
)
set(_spwkit_rx_profile_probes
    SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
    SPW_PROFILE_ID_RX_PROVIDER_RETURN
    SPW_PROFILE_ID_RX_BACKEND_RETURN
    SPW_PROFILE_ID_RX_API_RETURN
)
set(_spwkit_tx_zc_acquire_profile_probes
    SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY
    SPW_PROFILE_ID_TX_ZC_ACQUIRE_PROVIDER_ENTRY
    SPW_PROFILE_ID_TX_ZC_ACQUIRE_PROVIDER_RETURN
    SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_RETURN
)
set(_spwkit_tx_zc_submit_profile_probes
    SPW_PROFILE_ID_TX_ZC_SUBMIT_API_ENTRY
    SPW_PROFILE_ID_TX_ZC_SUBMIT_BACKEND_ENTRY
    SPW_PROFILE_ID_TX_ZC_SUBMIT_SYNC_ENTRY
    SPW_PROFILE_ID_TX_ZC_SUBMIT_SYNC_RETURN
    SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_ENTRY
    SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_BOUNDARY
    SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_RETURN
    SPW_PROFILE_ID_TX_ZC_SUBMIT_BACKEND_RETURN
    SPW_PROFILE_ID_TX_ZC_SUBMIT_API_RETURN
)
set(_spwkit_tx_zc_reclaim_profile_probes
    SPW_PROFILE_ID_TX_ZC_RECLAIM_API_ENTRY
    SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_ENTRY
    SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_BOUNDARY
    SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_RETURN
    SPW_PROFILE_ID_TX_ZC_RECLAIM_BACKEND_RETURN
    SPW_PROFILE_ID_TX_ZC_RECLAIM_API_RETURN
)
set(_spwkit_tx_zc_release_profile_probes
    SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY
    SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_ENTRY
    SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_ENTRY
    SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_RETURN
    SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_RETURN
    SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN
)
set(_spwkit_profile_domain_vars
    _spwkit_tx_profile_probes
    _spwkit_rx_profile_probes
    _spwkit_tx_zc_acquire_profile_probes
    _spwkit_tx_zc_submit_profile_probes
    _spwkit_tx_zc_reclaim_profile_probes
    _spwkit_tx_zc_release_profile_probes
)
set(_spwkit_all_profile_probes)
foreach(_spwkit_profile_domain_var IN LISTS _spwkit_profile_domain_vars)
    list(APPEND _spwkit_all_profile_probes ${${_spwkit_profile_domain_var}})
endforeach()
set_property(CACHE SPWKIT_PROFILE_START PROPERTY STRINGS ${_spwkit_all_profile_probes})
set_property(CACHE SPWKIT_PROFILE_END PROPERTY STRINGS ${_spwkit_all_profile_probes})

'''
text = text[:start] + new_lists + text[end:]
old_validation = '''    list(FIND _spwkit_tx_profile_probes "${SPWKIT_PROFILE_START}" _spwkit_tx_start_index)
    list(FIND _spwkit_tx_profile_probes "${SPWKIT_PROFILE_END}" _spwkit_tx_end_index)
    list(FIND _spwkit_rx_profile_probes "${SPWKIT_PROFILE_START}" _spwkit_rx_start_index)
    list(FIND _spwkit_rx_profile_probes "${SPWKIT_PROFILE_END}" _spwkit_rx_end_index)

    set(_spwkit_profile_same_domain OFF)
    if(NOT _spwkit_tx_start_index EQUAL -1 AND NOT _spwkit_tx_end_index EQUAL -1)
        set(_spwkit_profile_same_domain ON)
        if(_spwkit_tx_end_index LESS_EQUAL _spwkit_tx_start_index)
            message(FATAL_ERROR "TX profiling end probe must follow the start probe")
        endif()
    elseif(NOT _spwkit_rx_start_index EQUAL -1 AND NOT _spwkit_rx_end_index EQUAL -1)
        set(_spwkit_profile_same_domain ON)
        if(_spwkit_rx_end_index LESS_EQUAL _spwkit_rx_start_index)
            message(FATAL_ERROR "RX profiling end probe must follow the start probe")
        endif()
    endif()
    if(NOT _spwkit_profile_same_domain)
        message(FATAL_ERROR "Profiling start/end probes must belong to the same TX or RX domain")
    endif()
'''
new_validation = '''    set(_spwkit_profile_same_domain OFF)
    foreach(_spwkit_profile_domain_var IN LISTS _spwkit_profile_domain_vars)
        list(FIND ${_spwkit_profile_domain_var}
            "${SPWKIT_PROFILE_START}" _spwkit_profile_start_domain_index)
        list(FIND ${_spwkit_profile_domain_var}
            "${SPWKIT_PROFILE_END}" _spwkit_profile_end_domain_index)
        if(NOT _spwkit_profile_start_domain_index EQUAL -1 AND
           NOT _spwkit_profile_end_domain_index EQUAL -1)
            set(_spwkit_profile_same_domain ON)
            if(_spwkit_profile_end_domain_index LESS_EQUAL
               _spwkit_profile_start_domain_index)
                message(FATAL_ERROR
                    "Profiling end probe must follow the start probe within its operation domain")
            endif()
        endif()
    endforeach()
    if(NOT _spwkit_profile_same_domain)
        message(FATAL_ERROR
            "Profiling start/end probes must belong to the same operation domain")
    endif()
'''
text = replace_once(text, old_validation, new_validation, "CMake profiling validation")
write(path, text)


# ---------------------------------------------------------------------------
# Public zero-copy API boundaries.
# ---------------------------------------------------------------------------
path = "src/core/port.c"
text = read(path)
text = replace_once(text, '''spw_result_t spw_port_acquire_tx_buffer(spw_port_t* port,
                                        size_t min_capacity,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    if (validate_port(port) != SPW_OK || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    if (port->ops->acquire_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    return port->ops->acquire_tx_buffer(
        port->backend_context, min_capacity, timeout_us, out_buffer);
}
''', '''spw_result_t spw_port_acquire_tx_buffer(spw_port_t* port,
                                        size_t min_capacity,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    spw_result_t result;
    if (validate_port(port) != SPW_OK || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    if (port->ops->acquire_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    SPW_PROFILE_TX_ZC_ACQUIRE_API_ENTRY();
    result = port->ops->acquire_tx_buffer(
        port->backend_context, min_capacity, timeout_us, out_buffer);
    SPW_PROFILE_TX_ZC_ACQUIRE_API_RETURN();
    return result;
}
''', "TX acquire API probes")
text = replace_once(text, '''    result = port->ops->submit_tx_buffer(
        port->backend_context, *inout_buffer, timeout_us);
    if (result == SPW_OK) {
''', '''    SPW_PROFILE_TX_ZC_SUBMIT_API_ENTRY();
    result = port->ops->submit_tx_buffer(
        port->backend_context, *inout_buffer, timeout_us);
    SPW_PROFILE_TX_ZC_SUBMIT_API_RETURN();
    if (result == SPW_OK) {
''', "TX submit API probes")
text = replace_once(text, '''spw_result_t spw_port_reclaim_tx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    if (validate_port(port) != SPW_OK || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    if (port->ops->reclaim_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    return port->ops->reclaim_tx_buffer(
        port->backend_context, timeout_us, out_buffer);
}
''', '''spw_result_t spw_port_reclaim_tx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    spw_result_t result;
    if (validate_port(port) != SPW_OK || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    if (port->ops->reclaim_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    SPW_PROFILE_TX_ZC_RECLAIM_API_ENTRY();
    result = port->ops->reclaim_tx_buffer(
        port->backend_context, timeout_us, out_buffer);
    SPW_PROFILE_TX_ZC_RECLAIM_API_RETURN();
    return result;
}
''', "TX reclaim API probes")
text = replace_once(text, '''    result = port->ops->release_tx_buffer(port->backend_context, *inout_buffer);
    if (result == SPW_OK) {
''', '''    SPW_PROFILE_TX_ZC_RELEASE_API_ENTRY();
    result = port->ops->release_tx_buffer(port->backend_context, *inout_buffer);
    SPW_PROFILE_TX_ZC_RELEASE_API_RETURN();
    if (result == SPW_OK) {
''', "TX release API probes")
write(path, text)


# ---------------------------------------------------------------------------
# DRIVER ownership/backend/provider boundaries. Provider-native boundaries
# remain provider-owned and are emitted by real/reference providers.
# ---------------------------------------------------------------------------
path = "src/backends/driver/driver_backend.c"
text = read(path)
text = replace_once(text, '''    memset(&descriptor, 0, sizeof(descriptor));
    result = b->ops->acquire_tx_buffer(
        b->driver_context, min_capacity, timeout_us, &descriptor);
    if (result != SPW_OK) {
''', '''    memset(&descriptor, 0, sizeof(descriptor));
    SPW_PROFILE_TX_ZC_ACQUIRE_PROVIDER_ENTRY();
    result = b->ops->acquire_tx_buffer(
        b->driver_context, min_capacity, timeout_us, &descriptor);
    SPW_PROFILE_TX_ZC_ACQUIRE_PROVIDER_RETURN();
    if (result != SPW_OK) {
''', "TX acquire provider probes")
text = replace_once(text, '''static spw_result_t driver_submit_tx_buffer(void* raw,
                                            spw_buffer_t* buffer,
                                            spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(
''', '''static spw_result_t driver_submit_tx_buffer(void* raw,
                                            spw_buffer_t* buffer,
                                            spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    SPW_PROFILE_TX_ZC_SUBMIT_BACKEND_ENTRY();
    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(
''', "TX submit backend entry")
text = replace_once(text, '''    if (b->ops->sync_buffer != NULL) {
        result = b->ops->sync_buffer(b->driver_context,
                                     &slot->driver_buffer,
                                     SPW_DRIVER_SYNC_TO_DEVICE);
        if (result != SPW_OK) {
            return result;
        }
    }
    result = b->ops->submit_tx_buffer(
        b->driver_context, &slot->driver_buffer, timeout_us);
    if (result == SPW_OK) {
        slot->buffer.state = SPW_BUFFER_STATE_BACKEND;
    }
    return result;
''', '''    if (b->ops->sync_buffer != NULL) {
        SPW_PROFILE_TX_ZC_SUBMIT_SYNC_ENTRY();
        result = b->ops->sync_buffer(b->driver_context,
                                     &slot->driver_buffer,
                                     SPW_DRIVER_SYNC_TO_DEVICE);
        SPW_PROFILE_TX_ZC_SUBMIT_SYNC_RETURN();
        if (result != SPW_OK) {
            return result;
        }
    }
    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_ENTRY();
    result = b->ops->submit_tx_buffer(
        b->driver_context, &slot->driver_buffer, timeout_us);
    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_RETURN();
    if (result == SPW_OK) {
        slot->buffer.state = SPW_BUFFER_STATE_BACKEND;
    }
    SPW_PROFILE_TX_ZC_SUBMIT_BACKEND_RETURN();
    return result;
''', "TX submit sync/provider/return probes")
text = replace_once(text, '''    memset(&descriptor, 0, sizeof(descriptor));
    result = b->ops->reclaim_tx_buffer(
        b->driver_context, timeout_us, &descriptor);
    if (result != SPW_OK) {
''', '''    memset(&descriptor, 0, sizeof(descriptor));
    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_ENTRY();
    result = b->ops->reclaim_tx_buffer(
        b->driver_context, timeout_us, &descriptor);
    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_RETURN();
    if (result != SPW_OK) {
''', "TX reclaim provider probes")
text = replace_once(text, '''    expose_slot(b, slot, SPW_BUFFER_DIRECTION_TX,
                SPW_BUFFER_STATE_APPLICATION);
    *out_buffer = (spw_buffer_t*)&slot->buffer;
    return SPW_OK;
}

static spw_result_t driver_release_tx_buffer''', '''    expose_slot(b, slot, SPW_BUFFER_DIRECTION_TX,
                SPW_BUFFER_STATE_APPLICATION);
    *out_buffer = (spw_buffer_t*)&slot->buffer;
    SPW_PROFILE_TX_ZC_RECLAIM_BACKEND_RETURN();
    return SPW_OK;
}

static spw_result_t driver_release_tx_buffer''', "TX reclaim backend return")
text = replace_once(text, '''static spw_result_t driver_release_tx_buffer(void* raw,
                                             spw_buffer_t* buffer) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(
''', '''static spw_result_t driver_release_tx_buffer(void* raw,
                                             spw_buffer_t* buffer) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    SPW_PROFILE_TX_ZC_RELEASE_BACKEND_ENTRY();
    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(
''', "TX release backend entry")
text = replace_once(text, '''    result = b->ops->release_tx_buffer(
        b->driver_context, &slot->driver_buffer);
    if (result == SPW_OK) {
        clear_slot(slot);
    }
    return result;
''', '''    SPW_PROFILE_TX_ZC_RELEASE_PROVIDER_ENTRY();
    result = b->ops->release_tx_buffer(
        b->driver_context, &slot->driver_buffer);
    SPW_PROFILE_TX_ZC_RELEASE_PROVIDER_RETURN();
    if (result == SPW_OK) {
        clear_slot(slot);
    }
    SPW_PROFILE_TX_ZC_RELEASE_BACKEND_RETURN();
    return result;
''', "TX release provider/return probes")
write(path, text)


# ---------------------------------------------------------------------------
# Real-path smoke fixture: same public ownership API and DRIVER callbacks used
# in production, including optional sync and provider-owned native markers.
# ---------------------------------------------------------------------------
path = "tests/profile_probe_smoke.c"
text = read(path)
text = replace_once(text, '''    spw_terminator_t terminator;
    int ready;
} profile_driver_t;
''', '''    spw_terminator_t terminator;
    int ready;
    int tx_acquired;
    int tx_submitted;
} profile_driver_t;
''', "profile fixture DMA state")
text = replace_once(text, '''    driver->state = SPW_LINK_ERROR_RESET;
    driver->ready = 0;
    return SPW_OK;
''', '''    driver->state = SPW_LINK_ERROR_RESET;
    driver->ready = 0;
    driver->tx_acquired = 0;
    driver->tx_submitted = 0;
    return SPW_OK;
''', "profile fixture reset")
insert_anchor = '''static const spw_driver_ops_t PROFILE_OPS = {
'''
dma_callbacks = r'''static void profile_fill_tx_descriptor(profile_driver_t* driver,
                                       spw_driver_buffer_t* out_buffer) {
    out_buffer->data = driver->payload;
    out_buffer->length = driver->length;
    out_buffer->capacity = sizeof(driver->payload);
    out_buffer->terminator = driver->terminator;
    out_buffer->token = 1u;
}

static spw_result_t profile_acquire_tx_buffer(
    void* raw,
    size_t min_capacity,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;
    if (driver->state != SPW_LINK_RUN ||
        min_capacity > sizeof(driver->payload) ||
        driver->tx_acquired || driver->tx_submitted) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    driver->length = 0u;
    driver->terminator = SPW_TERMINATOR_EOP;
    driver->tx_acquired = 1;
    profile_fill_tx_descriptor(driver, out_buffer);
    return SPW_OK;
}

static spw_result_t profile_submit_tx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer,
    spw_timeout_us_t timeout_us) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;

    /* Provider-owned equivalent of the DMA/native submission boundary. */
    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_BOUNDARY();

    if (!driver->tx_acquired || driver->tx_submitted || buffer->token != 1u ||
        buffer->data != driver->payload || buffer->length > sizeof(driver->payload)) {
        return SPW_ERR_INVALID_STATE;
    }
    driver->length = buffer->length;
    driver->terminator = buffer->terminator;
    driver->tx_acquired = 0;
    driver->tx_submitted = 1;
    return SPW_OK;
}

static spw_result_t profile_reclaim_tx_buffer(
    void* raw,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;

    /* Provider-owned completion boundary before ownership is returned. */
    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_BOUNDARY();

    if (!driver->tx_submitted || driver->tx_acquired) {
        return SPW_ERR_TIMEOUT;
    }
    driver->tx_submitted = 0;
    driver->tx_acquired = 1;
    profile_fill_tx_descriptor(driver, out_buffer);
    return SPW_OK;
}

static spw_result_t profile_release_tx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    if (!driver->tx_acquired || driver->tx_submitted || buffer->token != 1u) {
        return SPW_ERR_INVALID_STATE;
    }
    driver->tx_acquired = 0;
    return SPW_OK;
}

static spw_result_t profile_acquire_rx_buffer(
    void* raw,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    (void)raw;
    (void)timeout_us;
    (void)out_buffer;
    return SPW_ERR_TIMEOUT;
}

static spw_result_t profile_release_rx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer) {
    (void)raw;
    (void)buffer;
    return SPW_OK;
}

static spw_result_t profile_sync_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer,
    spw_driver_sync_direction_t direction) {
    (void)raw;
    (void)buffer;
    return direction == SPW_DRIVER_SYNC_TO_DEVICE ? SPW_OK : SPW_ERR_UNSUPPORTED;
}

'''
text = replace_once(text, insert_anchor, dma_callbacks + insert_anchor, "profile DMA callbacks")
text = replace_once(text, '''    .send = profile_send,
    .receive = profile_receive,
};
''', '''    .send = profile_send,
    .receive = profile_receive,
    .acquire_tx_buffer = profile_acquire_tx_buffer,
    .submit_tx_buffer = profile_submit_tx_buffer,
    .reclaim_tx_buffer = profile_reclaim_tx_buffer,
    .release_tx_buffer = profile_release_tx_buffer,
    .acquire_rx_buffer = profile_acquire_rx_buffer,
    .release_rx_buffer = profile_release_rx_buffer,
    .sync_buffer = profile_sync_buffer,
};
''', "profile DMA ops")
macro_calls = "".join(f"    {macro_name(name)}();\n" for _, name in PROBES)
text = replace_once(text, '''    SPW_PROFILE_RX_API_RETURN();

    require_one_sample();
''', '''    SPW_PROFILE_RX_API_RETURN();
''' + macro_calls + '''
    require_one_sample();
''', "probe mechanism zero-copy calls")
zero_copy_test = r'''
static void exercise_zero_copy_boundaries(void) {
    profile_driver_t driver;
    spw_driver_config_t driver_config =
        SPW_DRIVER_CONFIG_INITIALIZER(&PROFILE_OPS, &driver);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_t* port = NULL;
    spw_buffer_t* buffer = NULL;
    spw_buffer_view_t view;

    memset(&driver, 0, sizeof(driver));
    driver.state = SPW_LINK_READY;
    driver_config.tx_buffer_slots = 1u;
    driver_config.rx_buffer_slots = 1u;
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_RETURN
    spw_profile_reset();
    assert(spw_port_acquire_tx_buffer(
               port, 4u, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_acquire_tx_buffer(
               port, 4u, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
#endif

    assert(spw_buffer_get_view(buffer, &view) == SPW_OK);
    assert(view.capacity >= 4u);
    view.data[0] = 0x11u;
    view.data[1] = 0x22u;
    view.data[2] = 0x33u;
    view.data[3] = 0x44u;
    assert(spw_buffer_set_packet(buffer, 4u, SPW_TERMINATOR_EOP) == SPW_OK);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_SUBMIT_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_SUBMIT_API_RETURN
    spw_profile_reset();
    assert(spw_port_submit_tx_buffer(
               port, &buffer, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_submit_tx_buffer(
               port, &buffer, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
#endif
    assert(buffer == NULL);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_RECLAIM_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_RECLAIM_API_RETURN
    spw_profile_reset();
    assert(spw_port_reclaim_tx_buffer(
               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_reclaim_tx_buffer(
               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
#endif
    assert(buffer != NULL);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN
    spw_profile_reset();
    assert(spw_port_release_tx_buffer(port, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_release_tx_buffer(port, &buffer) == SPW_OK);
#endif
    assert(buffer == NULL);

    assert(spw_port_stop(port) == SPW_OK);
    assert(spw_port_close(port) == SPW_OK);
}
'''
text = replace_once(text, '''int main(void) {
''', zero_copy_test + '''
int main(void) {
''', "zero-copy smoke function")
text = replace_once(text, '''    exercise_probe_mechanism();
    exercise_driver_boundary();
    return 0;
''', '''    exercise_probe_mechanism();
    exercise_driver_boundary();
    exercise_zero_copy_boundaries();
    return 0;
''', "zero-copy smoke invocation")
write(path, text)


# ---------------------------------------------------------------------------
# CI: validate representative ownership subranges and full-operation ranges in
# clean serial builds. This keeps runner count sane while still compiling each
# selected pair independently.
# ---------------------------------------------------------------------------
path = ".github/workflows/profiling.yml"
text = read(path)
job_anchor = "\n  counter-metadata:\n"
zc_job = r'''
  zero-copy-paired-probes:
    name: Paired probes / DRIVER zero-copy TX
    runs-on: ubuntu-24.04
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Exercise zero-copy ownership ranges
        shell: bash
        run: |
          set -euo pipefail
          pairs=(
            "acquire-provider:SPW_PROFILE_ID_TX_ZC_ACQUIRE_PROVIDER_ENTRY:SPW_PROFILE_ID_TX_ZC_ACQUIRE_PROVIDER_RETURN"
            "acquire-total:SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY:SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_RETURN"
            "submit-sync:SPW_PROFILE_ID_TX_ZC_SUBMIT_SYNC_ENTRY:SPW_PROFILE_ID_TX_ZC_SUBMIT_SYNC_RETURN"
            "submit-provider-native:SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_ENTRY:SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_BOUNDARY"
            "submit-total:SPW_PROFILE_ID_TX_ZC_SUBMIT_API_ENTRY:SPW_PROFILE_ID_TX_ZC_SUBMIT_API_RETURN"
            "reclaim-native-provider:SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_BOUNDARY:SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_RETURN"
            "reclaim-total:SPW_PROFILE_ID_TX_ZC_RECLAIM_API_ENTRY:SPW_PROFILE_ID_TX_ZC_RECLAIM_API_RETURN"
            "release-provider:SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_ENTRY:SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_RETURN"
            "release-total:SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY:SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN"
          )
          for pair in "${pairs[@]}"; do
            IFS=: read -r name start end <<<"$pair"
            build="build-profile-zc-${name}"
            cmake -S . -B "$build" \
              -DSPWKIT_ENABLE_PROFILING=ON \
              -DSPWKIT_PROFILE_START="$start" \
              -DSPWKIT_PROFILE_END="$end" \
              -DSPWKIT_BUILD_CPP_TESTS=OFF \
              -DSPWKIT_BUILD_CPP_EXAMPLES=OFF
            cmake --build "$build" --target spwkit_profile_probe_smoke --parallel 2
            ctest --test-dir "$build" -R '^profiling_pair_smoke$' --output-on-failure
          done

'''
text = replace_once(text, job_anchor, "\n" + zc_job + "  counter-metadata:\n", "zero-copy profiling CI job")
text += r'''

      - name: Reject cross zero-copy operation probes
        shell: bash
        run: |
          set -euo pipefail
          if cmake -S . -B build-invalid-zc-domain \
              -DSPWKIT_ENABLE_PROFILING=ON \
              -DSPWKIT_PROFILE_START=SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY \
              -DSPWKIT_PROFILE_END=SPW_PROFILE_ID_TX_ZC_SUBMIT_API_RETURN; then
            echo 'cross-operation zero-copy profiling probes were incorrectly accepted' >&2
            exit 1
          fi
'''
write(path, text)

print("zero-copy profiling patch applied")
