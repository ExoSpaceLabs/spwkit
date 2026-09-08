#!/usr/bin/env python3
from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def macro_block(name, probe_id):
    return f'''#if SPWKIT_PROFILE_START == {probe_id}\n#define {name}() SPW_PROFILE_CAPTURE_START()\n#elif SPWKIT_PROFILE_END == {probe_id}\n#define {name}() SPW_PROFILE_CAPTURE_END()\n#else\n#define {name}() ((void)0)\n#endif\n\n'''


# ---------------------------------------------------------------------------
# Top-level profiling domains
# ---------------------------------------------------------------------------
path = "CMakeLists.txt"
text = read(path)
text = replace_once(
    text,
    '''set(_spwkit_tx_zc_release_profile_probes\n    SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY\n    SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_ENTRY\n    SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_ENTRY\n    SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_RETURN\n    SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_RETURN\n    SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN\n)\nset(_spwkit_profile_domain_vars\n''',
    '''set(_spwkit_tx_zc_release_profile_probes\n    SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY\n    SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_ENTRY\n    SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_ENTRY\n    SPW_PROFILE_ID_TX_ZC_RELEASE_PROVIDER_RETURN\n    SPW_PROFILE_ID_TX_ZC_RELEASE_BACKEND_RETURN\n    SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN\n)\nset(_spwkit_rx_zc_acquire_profile_probes\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_ENTRY\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_BACKEND_ENTRY\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_ENTRY\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_RETURN\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_SYNC_ENTRY\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_SYNC_RETURN\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_BACKEND_RETURN\n    SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_RETURN\n)\nset(_spwkit_rx_zc_release_profile_probes\n    SPW_PROFILE_ID_RX_ZC_RELEASE_API_ENTRY\n    SPW_PROFILE_ID_RX_ZC_RELEASE_BACKEND_ENTRY\n    SPW_PROFILE_ID_RX_ZC_RELEASE_PROVIDER_ENTRY\n    SPW_PROFILE_ID_RX_ZC_RELEASE_PROVIDER_RETURN\n    SPW_PROFILE_ID_RX_ZC_RELEASE_BACKEND_RETURN\n    SPW_PROFILE_ID_RX_ZC_RELEASE_API_RETURN\n)\nset(_spwkit_profile_domain_vars\n''',
    "add RX zero-copy profiling domains",
)
text = replace_once(
    text,
    '''    _spwkit_tx_zc_reclaim_profile_probes\n    _spwkit_tx_zc_release_profile_probes\n)\n''',
    '''    _spwkit_tx_zc_reclaim_profile_probes\n    _spwkit_tx_zc_release_profile_probes\n    _spwkit_rx_zc_acquire_profile_probes\n    _spwkit_rx_zc_release_profile_probes\n)\n''',
    "register RX zero-copy profiling domains",
)
write(path, text)


# ---------------------------------------------------------------------------
# Profiling probe IDs and macros
# ---------------------------------------------------------------------------
path = "src/profiling/profile.h"
text = read(path)
text = replace_once(
    text,
    '#define SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN 33\n',
    '''#define SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN 33\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_ENTRY 34\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_BACKEND_ENTRY 35\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_ENTRY 36\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY 37\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_RETURN 38\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_SYNC_ENTRY 39\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_SYNC_RETURN 40\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_BACKEND_RETURN 41\n#define SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_RETURN 42\n#define SPW_PROFILE_ID_RX_ZC_RELEASE_API_ENTRY 43\n#define SPW_PROFILE_ID_RX_ZC_RELEASE_BACKEND_ENTRY 44\n#define SPW_PROFILE_ID_RX_ZC_RELEASE_PROVIDER_ENTRY 45\n#define SPW_PROFILE_ID_RX_ZC_RELEASE_PROVIDER_RETURN 46\n#define SPW_PROFILE_ID_RX_ZC_RELEASE_BACKEND_RETURN 47\n#define SPW_PROFILE_ID_RX_ZC_RELEASE_API_RETURN 48\n''',
    "add RX zero-copy probe IDs",
)

rx_macros = [
    ("SPW_PROFILE_RX_ZC_ACQUIRE_API_ENTRY", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_ENTRY"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_BACKEND_ENTRY", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_BACKEND_ENTRY"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_ENTRY", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_ENTRY"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_RETURN", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_RETURN"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_SYNC_ENTRY", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_SYNC_ENTRY"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_SYNC_RETURN", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_SYNC_RETURN"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_BACKEND_RETURN", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_BACKEND_RETURN"),
    ("SPW_PROFILE_RX_ZC_ACQUIRE_API_RETURN", "SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_RETURN"),
    ("SPW_PROFILE_RX_ZC_RELEASE_API_ENTRY", "SPW_PROFILE_ID_RX_ZC_RELEASE_API_ENTRY"),
    ("SPW_PROFILE_RX_ZC_RELEASE_BACKEND_ENTRY", "SPW_PROFILE_ID_RX_ZC_RELEASE_BACKEND_ENTRY"),
    ("SPW_PROFILE_RX_ZC_RELEASE_PROVIDER_ENTRY", "SPW_PROFILE_ID_RX_ZC_RELEASE_PROVIDER_ENTRY"),
    ("SPW_PROFILE_RX_ZC_RELEASE_PROVIDER_RETURN", "SPW_PROFILE_ID_RX_ZC_RELEASE_PROVIDER_RETURN"),
    ("SPW_PROFILE_RX_ZC_RELEASE_BACKEND_RETURN", "SPW_PROFILE_ID_RX_ZC_RELEASE_BACKEND_RETURN"),
    ("SPW_PROFILE_RX_ZC_RELEASE_API_RETURN", "SPW_PROFILE_ID_RX_ZC_RELEASE_API_RETURN"),
]
blocks = "".join(macro_block(name, probe_id) for name, probe_id in rx_macros)
text = replace_once(
    text,
    '''#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN\n#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() SPW_PROFILE_CAPTURE_START()\n#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN\n#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() SPW_PROFILE_CAPTURE_END()\n#else\n#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() ((void)0)\n#endif\n\n#else\n''',
    '''#if SPWKIT_PROFILE_START == SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN\n#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() SPW_PROFILE_CAPTURE_START()\n#elif SPWKIT_PROFILE_END == SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN\n#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() SPW_PROFILE_CAPTURE_END()\n#else\n#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() ((void)0)\n#endif\n\n''' + blocks + '''#else\n''',
    "add RX zero-copy profiling macro blocks",
)
disabled = "".join(f"#define {name}() ((void)0)\n" for name, _ in rx_macros)
text = replace_once(
    text,
    '#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() ((void)0)\n\n#endif\n',
    '#define SPW_PROFILE_TX_ZC_RELEASE_API_RETURN() ((void)0)\n' + disabled + '\n#endif\n',
    "add disabled RX zero-copy macros",
)
write(path, text)


# ---------------------------------------------------------------------------
# Public API boundaries
# ---------------------------------------------------------------------------
path = "src/core/port.c"
text = read(path)
text = replace_once(
    text,
    '''spw_result_t spw_port_acquire_rx_buffer(spw_port_t* port,\n                                        spw_timeout_us_t timeout_us,\n                                        spw_buffer_t** out_buffer) {\n    if (validate_port(port) != SPW_OK || out_buffer == NULL) {\n        return SPW_ERR_INVALID_ARGUMENT;\n    }\n    *out_buffer = NULL;\n    if (port->ops->acquire_rx_buffer == NULL) {\n        return SPW_ERR_UNSUPPORTED;\n    }\n    return port->ops->acquire_rx_buffer(\n        port->backend_context, timeout_us, out_buffer);\n}\n''',
    '''spw_result_t spw_port_acquire_rx_buffer(spw_port_t* port,\n                                        spw_timeout_us_t timeout_us,\n                                        spw_buffer_t** out_buffer) {\n    spw_result_t result;\n    if (validate_port(port) != SPW_OK || out_buffer == NULL) {\n        return SPW_ERR_INVALID_ARGUMENT;\n    }\n    *out_buffer = NULL;\n    if (port->ops->acquire_rx_buffer == NULL) {\n        return SPW_ERR_UNSUPPORTED;\n    }\n    SPW_PROFILE_RX_ZC_ACQUIRE_API_ENTRY();\n    result = port->ops->acquire_rx_buffer(\n        port->backend_context, timeout_us, out_buffer);\n    SPW_PROFILE_RX_ZC_ACQUIRE_API_RETURN();\n    return result;\n}\n''',
    "instrument RX zero-copy acquire API",
)
text = replace_once(
    text,
    '''    if (port->ops->release_rx_buffer == NULL) {\n        return SPW_ERR_UNSUPPORTED;\n    }\n    result = port->ops->release_rx_buffer(port->backend_context, *inout_buffer);\n    if (result == SPW_OK) {\n        *inout_buffer = NULL;\n    }\n    return result;\n}\n''',
    '''    if (port->ops->release_rx_buffer == NULL) {\n        return SPW_ERR_UNSUPPORTED;\n    }\n    SPW_PROFILE_RX_ZC_RELEASE_API_ENTRY();\n    result = port->ops->release_rx_buffer(port->backend_context, *inout_buffer);\n    SPW_PROFILE_RX_ZC_RELEASE_API_RETURN();\n    if (result == SPW_OK) {\n        *inout_buffer = NULL;\n    }\n    return result;\n}\n''',
    "instrument RX zero-copy release API",
)
write(path, text)


# ---------------------------------------------------------------------------
# DRIVER backend boundaries
# ---------------------------------------------------------------------------
path = "src/backends/driver/driver_backend.c"
text = read(path)
text = replace_once(
    text,
    '''static spw_result_t driver_acquire_rx_buffer(void* raw,\n                                             spw_timeout_us_t timeout_us,\n                                             spw_buffer_t** out_buffer) {\n    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;\n    spw_driver_buffer_slot_t* slot = find_free_slot(\n        b, b->tx_slot_count, b->rx_slot_count);\n    spw_driver_buffer_t descriptor;\n    spw_result_t result;\n    if (slot == NULL) {\n        return SPW_ERR_RESOURCE_EXHAUSTED;\n    }\n    memset(&descriptor, 0, sizeof(descriptor));\n    result = b->ops->acquire_rx_buffer(\n        b->driver_context, timeout_us, &descriptor);\n    if (result != SPW_OK) {\n        return result;\n    }\n    if (validate_driver_buffer(&descriptor, true) != SPW_OK ||\n        token_in_use(b, descriptor.token)) {\n        (void)b->ops->release_rx_buffer(b->driver_context, &descriptor);\n        return SPW_ERR_BACKEND;\n    }\n    if (b->ops->sync_buffer != NULL) {\n        result = b->ops->sync_buffer(b->driver_context,\n                                     &descriptor,\n                                     SPW_DRIVER_SYNC_FROM_DEVICE);\n        if (result != SPW_OK) {\n            (void)b->ops->release_rx_buffer(b->driver_context, &descriptor);\n            return result;\n        }\n    }\n    slot->driver_buffer = descriptor;\n    expose_slot(b, slot, SPW_BUFFER_DIRECTION_RX,\n                SPW_BUFFER_STATE_APPLICATION);\n    *out_buffer = (spw_buffer_t*)&slot->buffer;\n    return SPW_OK;\n}\n''',
    '''static spw_result_t driver_acquire_rx_buffer(void* raw,\n                                             spw_timeout_us_t timeout_us,\n                                             spw_buffer_t** out_buffer) {\n    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;\n    spw_driver_buffer_slot_t* slot;\n    spw_driver_buffer_t descriptor;\n    spw_result_t result;\n    SPW_PROFILE_RX_ZC_ACQUIRE_BACKEND_ENTRY();\n    slot = find_free_slot(b, b->tx_slot_count, b->rx_slot_count);\n    if (slot == NULL) {\n        return SPW_ERR_RESOURCE_EXHAUSTED;\n    }\n    memset(&descriptor, 0, sizeof(descriptor));\n    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_ENTRY();\n    result = b->ops->acquire_rx_buffer(\n        b->driver_context, timeout_us, &descriptor);\n    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_RETURN();\n    if (result != SPW_OK) {\n        return result;\n    }\n    if (validate_driver_buffer(&descriptor, true) != SPW_OK ||\n        token_in_use(b, descriptor.token)) {\n        (void)b->ops->release_rx_buffer(b->driver_context, &descriptor);\n        return SPW_ERR_BACKEND;\n    }\n    if (b->ops->sync_buffer != NULL) {\n        SPW_PROFILE_RX_ZC_ACQUIRE_SYNC_ENTRY();\n        result = b->ops->sync_buffer(b->driver_context,\n                                     &descriptor,\n                                     SPW_DRIVER_SYNC_FROM_DEVICE);\n        SPW_PROFILE_RX_ZC_ACQUIRE_SYNC_RETURN();\n        if (result != SPW_OK) {\n            (void)b->ops->release_rx_buffer(b->driver_context, &descriptor);\n            return result;\n        }\n    }\n    slot->driver_buffer = descriptor;\n    expose_slot(b, slot, SPW_BUFFER_DIRECTION_RX,\n                SPW_BUFFER_STATE_APPLICATION);\n    *out_buffer = (spw_buffer_t*)&slot->buffer;\n    SPW_PROFILE_RX_ZC_ACQUIRE_BACKEND_RETURN();\n    return SPW_OK;\n}\n''',
    "instrument DRIVER RX acquire",
)
text = replace_once(
    text,
    '''static spw_result_t driver_release_rx_buffer(void* raw,\n                                             spw_buffer_t* buffer) {\n    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;\n    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(\n        b, buffer, SPW_BUFFER_DIRECTION_RX);\n    spw_result_t result;\n    if (slot == NULL || slot->buffer.state != SPW_BUFFER_STATE_APPLICATION) {\n        return SPW_ERR_INVALID_STATE;\n    }\n    result = b->ops->release_rx_buffer(\n        b->driver_context, &slot->driver_buffer);\n    if (result == SPW_OK) {\n        clear_slot(slot);\n    }\n    return result;\n}\n''',
    '''static spw_result_t driver_release_rx_buffer(void* raw,\n                                             spw_buffer_t* buffer) {\n    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;\n    spw_driver_buffer_slot_t* slot;\n    spw_result_t result;\n    SPW_PROFILE_RX_ZC_RELEASE_BACKEND_ENTRY();\n    slot = find_slot_for_public_buffer(\n        b, buffer, SPW_BUFFER_DIRECTION_RX);\n    if (slot == NULL || slot->buffer.state != SPW_BUFFER_STATE_APPLICATION) {\n        return SPW_ERR_INVALID_STATE;\n    }\n    SPW_PROFILE_RX_ZC_RELEASE_PROVIDER_ENTRY();\n    result = b->ops->release_rx_buffer(\n        b->driver_context, &slot->driver_buffer);\n    SPW_PROFILE_RX_ZC_RELEASE_PROVIDER_RETURN();\n    if (result == SPW_OK) {\n        clear_slot(slot);\n    }\n    SPW_PROFILE_RX_ZC_RELEASE_BACKEND_RETURN();\n    return result;\n}\n''',
    "instrument DRIVER RX release",
)
write(path, text)


# ---------------------------------------------------------------------------
# Profiling smoke fixture exercises the real public API -> DRIVER callbacks.
# ---------------------------------------------------------------------------
path = "tests/profile_probe_smoke.c"
text = read(path)
text = replace_once(
    text,
    '''    int tx_acquired;\n    int tx_submitted;\n} profile_driver_t;\n''',
    '''    int tx_acquired;\n    int tx_submitted;\n    int rx_acquired;\n} profile_driver_t;\n''',
    "add RX ownership state to smoke driver",
)
text = replace_once(
    text,
    '''    driver->tx_acquired = 0;\n    driver->tx_submitted = 0;\n    return SPW_OK;\n''',
    '''    driver->tx_acquired = 0;\n    driver->tx_submitted = 0;\n    driver->rx_acquired = 0;\n    return SPW_OK;\n''',
    "reset RX ownership state",
)
text = replace_once(
    text,
    '''static spw_result_t profile_acquire_rx_buffer(\n    void* raw,\n    spw_timeout_us_t timeout_us,\n    spw_driver_buffer_t* out_buffer) {\n    (void)raw;\n    (void)timeout_us;\n    (void)out_buffer;\n    return SPW_ERR_TIMEOUT;\n}\n\nstatic spw_result_t profile_release_rx_buffer(\n    void* raw,\n    const spw_driver_buffer_t* buffer) {\n    (void)raw;\n    (void)buffer;\n    return SPW_OK;\n}\n''',
    '''static spw_result_t profile_acquire_rx_buffer(\n    void* raw,\n    spw_timeout_us_t timeout_us,\n    spw_driver_buffer_t* out_buffer) {\n    profile_driver_t* driver = (profile_driver_t*)raw;\n    (void)timeout_us;\n    if (driver->state != SPW_LINK_RUN || driver->rx_acquired ||\n        out_buffer == NULL) {\n        return SPW_ERR_TIMEOUT;\n    }\n\n    /* Provider-owned DMA/native data-ready boundary. */\n    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY();\n\n    out_buffer->data = driver->payload;\n    out_buffer->length = driver->length;\n    out_buffer->capacity = sizeof(driver->payload);\n    out_buffer->terminator = driver->terminator;\n    out_buffer->token = 2u;\n    driver->rx_acquired = 1;\n    return SPW_OK;\n}\n\nstatic spw_result_t profile_release_rx_buffer(\n    void* raw,\n    const spw_driver_buffer_t* buffer) {\n    profile_driver_t* driver = (profile_driver_t*)raw;\n    if (!driver->rx_acquired || buffer == NULL || buffer->token != 2u ||\n        buffer->data != driver->payload) {\n        return SPW_ERR_INVALID_STATE;\n    }\n    driver->rx_acquired = 0;\n    return SPW_OK;\n}\n''',
    "make smoke RX zero-copy callbacks functional",
)
text = replace_once(
    text,
    '''    (void)raw;\n    (void)buffer;\n    return direction == SPW_DRIVER_SYNC_TO_DEVICE ? SPW_OK : SPW_ERR_UNSUPPORTED;\n}\n''',
    '''    (void)raw;\n    (void)buffer;\n    return (direction == SPW_DRIVER_SYNC_TO_DEVICE ||\n            direction == SPW_DRIVER_SYNC_FROM_DEVICE)\n               ? SPW_OK\n               : SPW_ERR_UNSUPPORTED;\n}\n''',
    "allow smoke FROM_DEVICE sync",
)
probe_calls = ''.join(f'    {name}();\n' for name, _ in rx_macros)
text = replace_once(
    text,
    '''    SPW_PROFILE_TX_ZC_RELEASE_BACKEND_RETURN();\n    SPW_PROFILE_TX_ZC_RELEASE_API_RETURN();\n\n    require_one_sample();\n''',
    '''    SPW_PROFILE_TX_ZC_RELEASE_BACKEND_RETURN();\n    SPW_PROFILE_TX_ZC_RELEASE_API_RETURN();\n''' + probe_calls + '''\n    require_one_sample();\n''',
    "exercise RX zero-copy probe mechanism",
)
text = replace_once(
    text,
    '''    assert(buffer == NULL);\n\n    assert(spw_port_stop(port) == SPW_OK);\n''',
    '''    assert(buffer == NULL);\n\n#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_ENTRY && \\\n    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_RETURN\n    spw_profile_reset();\n    assert(spw_port_acquire_rx_buffer(\n               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);\n    require_one_sample();\n#else\n    assert(spw_port_acquire_rx_buffer(\n               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);\n#endif\n    assert(buffer != NULL);\n    assert(spw_buffer_get_view(buffer, &view) == SPW_OK);\n    assert(view.length == 4u);\n    assert(view.data == driver.payload);\n\n#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_RX_ZC_RELEASE_API_ENTRY && \\\n    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_RX_ZC_RELEASE_API_RETURN\n    spw_profile_reset();\n    assert(spw_port_release_rx_buffer(port, &buffer) == SPW_OK);\n    require_one_sample();\n#else\n    assert(spw_port_release_rx_buffer(port, &buffer) == SPW_OK);\n#endif\n    assert(buffer == NULL);\n\n    assert(spw_port_stop(port) == SPW_OK);\n''',
    "exercise RX zero-copy public ownership path",
)
write(path, text)


# Add provider-owned boundary to the deterministic RX comparison provider.
path = "benchmarks/profile_zero_copy_receive_comparison.c"
text = read(path)
text = replace_once(
    text,
    '''    /* Same data-ready point as copied_receive(), before ownership plumbing. */\n    driver->data_ready_tick = spw_profile_counter_read();\n    fill_rx_descriptor(driver, out_buffer);\n''',
    '''    /* Same data-ready point as copied_receive(), before ownership plumbing. */\n    driver->data_ready_tick = spw_profile_counter_read();\n    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY();\n    fill_rx_descriptor(driver, out_buffer);\n''',
    "mark RX provider data-ready boundary in comparison fixture",
)
write(path, text)


# ---------------------------------------------------------------------------
# Benchmark target
# ---------------------------------------------------------------------------
path = "benchmarks/CMakeLists.txt"
text = read(path)
text = replace_once(
    text,
    '''spwkit_configure_profile_executable(\n    spwkit_profile_zero_copy_comparison\n    profile_zero_copy_comparison.c)\nspwkit_configure_profile_executable(\n    spwkit_profile_host_backend\n''',
    '''spwkit_configure_profile_executable(\n    spwkit_profile_zero_copy_comparison\n    profile_zero_copy_comparison.c)\nspwkit_configure_profile_executable(\n    spwkit_profile_zero_copy_receive_comparison\n    profile_zero_copy_receive_comparison.c)\nspwkit_configure_profile_executable(\n    spwkit_profile_host_backend\n''',
    "add RX zero-copy benchmark target",
)
write(path, text)


# ---------------------------------------------------------------------------
# Campaign integration
# ---------------------------------------------------------------------------
path = "benchmarks/run_profile_campaign.sh"
text = read(path)
text = replace_once(
    text,
    "printf '  copy-elimination comparison: DRIVER copied vs zero-copy TX\\n' >&2\n",
    "printf '  copy-elimination comparison: DRIVER copied vs zero-copy TX/RX\\n' >&2\n",
    "update campaign banner",
)
text = replace_once(
    text,
    '''bash "$ROOT_DIR/benchmarks/run_native_receive_comparison.sh" \\\n  --warmup "$warmup" \\\n  --iterations "$iterations" \\\n  --payloads "$payloads" \\\n  --counter-hz "$counter_hz" \\\n  --settle-seconds "$settle_seconds" \\\n  --build-dir "$campaign_build_root/native-rx-comparison" \\\n  --output "$rx_comparison_output" \\\n  --calibration-output "$rx_comparison_calibration" \\\n  > /dev/null\n\nhost_backend_cases=(\n''',
    '''bash "$ROOT_DIR/benchmarks/run_native_receive_comparison.sh" \\\n  --warmup "$warmup" \\\n  --iterations "$iterations" \\\n  --payloads "$payloads" \\\n  --counter-hz "$counter_hz" \\\n  --settle-seconds "$settle_seconds" \\\n  --build-dir "$campaign_build_root/native-rx-comparison" \\\n  --output "$rx_comparison_output" \\\n  --calibration-output "$rx_comparison_calibration" \\\n  > /dev/null\n\nzero_copy_rx_output="$output_dir/comparison/driver_rx_copy_zero_copy.jsonl"\nzero_copy_rx_calibration="$output_dir/comparison/driver_rx_copy_zero_copy_calibration.json"\nprintf '\\n[campaign comparison] DRIVER copied vs zero-copy RX\\n' >&2\nbash "$ROOT_DIR/benchmarks/run_zero_copy_receive_comparison.sh" \\\n  --warmup "$warmup" \\\n  --iterations "$iterations" \\\n  --payloads "$payloads" \\\n  --counter-hz "$counter_hz" \\\n  --settle-seconds "$settle_seconds" \\\n  --build-dir "$campaign_build_root/driver-rx-copy-zero-copy" \\\n  --output "$zero_copy_rx_output" \\\n  --calibration-output "$zero_copy_rx_calibration" \\\n  > /dev/null\n\nhost_backend_cases=(\n''',
    "integrate RX zero-copy comparison in campaign",
)
text = replace_once(
    text,
    "    'zero_copy_comparison_cases': ['driver_tx_copy_zero_copy'],\n",
    "    'zero_copy_comparison_cases': ['driver_tx_copy_zero_copy', 'driver_rx_copy_zero_copy'],\n",
    "record RX zero-copy campaign metadata",
)
write(path, text)


# ---------------------------------------------------------------------------
# Summary and coverage
# ---------------------------------------------------------------------------
path = "benchmarks/summarize_profile_campaign.py"
text = read(path)
text = replace_once(
    text,
    '    zero_copy_tx = measured("comparison/driver_tx_copy_zero_copy.jsonl")\n',
    '    zero_copy_tx = measured("comparison/driver_tx_copy_zero_copy.jsonl")\n    zero_copy_rx = measured("comparison/driver_rx_copy_zero_copy.jsonl")\n',
    "measure RX zero-copy coverage",
)
text = replace_once(
    text,
    '        {"backend": "driver", "path": "zero-copy", "direction": "rx", "status": "not-implemented-benchmark", "reason": "tracked by #138"},\n',
    '''        {\n            "backend": "driver",\n            "path": "zero-copy",\n            "direction": "rx",\n            "status": "measured" if zero_copy_rx else "not-implemented-benchmark",\n            "reason": "provider-owned RX DMA-buffer copied-vs-zero-copy comparison" if zero_copy_rx else "tracked by #182",\n        },\n''',
    "mark RX zero-copy coverage from result artifact",
)
rx_summary_fn = '''\n\ndef append_zero_copy_rx(lines, comparison_path: Path):\n    rows = load_jsonl(comparison_path)\n    if not rows:\n        return\n    lines.append("DRIVER RX: copied vs zero-copy DMA-buffer visibility")\n    lines.append("----------------------------------------------------------------")\n    lines.append("Payload   Copied med   ZC med   ZC-Copy   Delta %   Acquire   Release")\n    for row in rows:\n        copied = row["copied_visibility_statistics"]\n        zero_copy = row["zero_copy_visibility_statistics"]\n        ownership = row["ownership_statistics"]\n        delta = row["delta"]\n        lines.append(\n            f"{row['payload_bytes']:>6} B   "\n            f"{fmt(copied['median']):>10}   "\n            f"{fmt(zero_copy['median']):>6}   "\n            f"{fmt(delta['median_ticks']):>7}   "\n            f"{fmt(delta['median_percent']):>7}   "\n            f"{fmt(ownership['acquire']['median']):>7}   "\n            f"{fmt(ownership['release']['median']):>7}"\n        )\n    lines.append("")\n'''
text = replace_once(
    text,
    '\n\ndef append_backend(lines, title, backend_path: Path):\n',
    rx_summary_fn + '\n\ndef append_backend(lines, title, backend_path: Path):\n',
    "add RX zero-copy summary renderer",
)
text = replace_once(
    text,
    '    append_zero_copy(lines, root / "comparison" / "driver_tx_copy_zero_copy.jsonl")\n',
    '    append_zero_copy(lines, root / "comparison" / "driver_tx_copy_zero_copy.jsonl")\n    append_zero_copy_rx(lines, root / "comparison" / "driver_rx_copy_zero_copy.jsonl")\n',
    "render RX zero-copy summary",
)
write(path, text)


# ---------------------------------------------------------------------------
# Machine-readable validation and 12/12 coverage contract
# ---------------------------------------------------------------------------
path = "benchmarks/validate_profile_campaign.py"
text = read(path)
rx_validator = '''\n\ndef validate_zero_copy_rx_comparison(row):\n    assert row['schema'] == 'spwkit.profile.zero-copy-rx-comparison.v1'\n    assert row['measurement_domain'] == 'software'\n    assert row['unit'] == 'counter_ticks'\n    assert row['backend'] == 'driver'\n    assert row['direction'] == 'rx'\n    assert row['fixture'] == 'provider-owned-dma-buffer-reference'\n    assert row['boundary'] == 'provider-data-ready-to-application-visibility'\n    assert row['copied_path'] == 'provider-storage-to-caller-buffer-copy'\n    assert row['zero_copy_path'] == 'acquire-provider-storage-directly'\n    assert row['release_outside_visibility_interval'] is True\n    assert row['provider_storage_shared'] is True\n    assert row['sync_hook_present'] is False\n    assert row['sample_order'] == 'alternating-per-iteration'\n    assert row['counter_floor_subtracted'] is False\n    assert REQUIRED_STATS == set(row['copied_visibility_statistics'])\n    assert REQUIRED_STATS == set(row['zero_copy_visibility_statistics'])\n    assert set(row['ownership_statistics']) == {'copied_api_call', 'acquire', 'release'}\n    for stats in row['ownership_statistics'].values():\n        assert REQUIRED_STATS == set(stats)\n    assert row['delta']['definition'] == 'zero-copy-minus-copied'\n    assert isinstance(row['delta']['zero_copy_faster_by_median'], bool)\n    assert set(row['effective_software_throughput']) == {'copied', 'zero_copy'}\n    assert 0 <= row['payload_bytes'] <= 4096\n    assert 1 <= row['iterations'] <= 4096\n'''
text = replace_once(
    text,
    '\n\ndef validate_udp_comparison(row, direction):\n',
    rx_validator + '\n\ndef validate_udp_comparison(row, direction):\n',
    "add RX zero-copy schema validator",
)
text = replace_once(
    text,
    "    parser.add_argument('--expected-measured', type=int, default=11 if platform.system() == 'Linux' else 9)\n",
    "    parser.add_argument('--expected-measured', type=int, default=12 if platform.system() == 'Linux' else 10)\n",
    "raise default coverage target",
)
text = replace_once(
    text,
    "    assert metadata['zero_copy_comparison_cases'] == ['driver_tx_copy_zero_copy']\n",
    "    assert metadata['zero_copy_comparison_cases'] == ['driver_tx_copy_zero_copy', 'driver_rx_copy_zero_copy']\n",
    "validate RX zero-copy campaign metadata",
)
text = replace_once(
    text,
    "    zero_copy_rows = load_jsonl(root / 'comparison' / 'driver_tx_copy_zero_copy.jsonl')\n",
    "    zero_copy_rows = load_jsonl(root / 'comparison' / 'driver_tx_copy_zero_copy.jsonl')\n    zero_copy_rx_rows = load_jsonl(root / 'comparison' / 'driver_rx_copy_zero_copy.jsonl')\n",
    "load RX zero-copy rows",
)
text = replace_once(
    text,
    "    if not tx_rows or not rx_rows or not zero_copy_rows or not udp_tx_rows or not udp_rx_rows:\n",
    "    if not tx_rows or not rx_rows or not zero_copy_rows or not zero_copy_rx_rows or not udp_tx_rows or not udp_rx_rows:\n",
    "require RX zero-copy rows",
)
text = replace_once(
    text,
    '''    for row in zero_copy_rows:\n        validate_zero_copy_comparison(row)\n    for row in udp_tx_rows:\n''',
    '''    for row in zero_copy_rows:\n        validate_zero_copy_comparison(row)\n    for row in zero_copy_rx_rows:\n        validate_zero_copy_rx_comparison(row)\n    for row in udp_tx_rows:\n''',
    "validate RX zero-copy rows",
)
text = replace_once(
    text,
    "        root / 'comparison' / 'driver_tx_copy_zero_copy_calibration.json',\n",
    "        root / 'comparison' / 'driver_tx_copy_zero_copy_calibration.json',\n        root / 'comparison' / 'driver_rx_copy_zero_copy_calibration.json',\n",
    "validate RX zero-copy calibration",
)
text = replace_once(
    text,
    "        ('driver', 'zero-copy', 'tx'),\n",
    "        ('driver', 'zero-copy', 'tx'),\n        ('driver', 'zero-copy', 'rx'),\n",
    "require RX zero-copy coverage entry",
)
text = replace_once(
    text,
    "        'DRIVER TX: copied vs zero-copy DMA-buffer preparation',\n",
    "        'DRIVER TX: copied vs zero-copy DMA-buffer preparation',\n        'DRIVER RX: copied vs zero-copy DMA-buffer visibility',\n",
    "require RX zero-copy summary heading",
)
text = replace_once(
    text,
    '''        f"{len(zero_copy_rows)} DRIVER copied/zero-copy TX row(s), "\n        f"{len(udp_tx_rows)} UDP TX row(s), {len(udp_rx_rows)} UDP RX row(s), "\n''',
    '''        f"{len(zero_copy_rows)} DRIVER copied/zero-copy TX row(s), "\n        f"{len(zero_copy_rx_rows)} DRIVER copied/zero-copy RX row(s), "\n        f"{len(udp_tx_rows)} UDP TX row(s), {len(udp_rx_rows)} UDP RX row(s), "\n''',
    "report RX zero-copy row count",
)
write(path, text)

print("zero-copy RX profiling and campaign patch applied")
