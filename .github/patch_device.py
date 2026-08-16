from pathlib import Path

p = Path("src/core/port.c")
s = p.read_text()

old = "#include <spwkit/buffer.h>\n"
new = old + "#include <spwkit/device.h>\n"
if "#include <spwkit/device.h>" not in s:
    if old not in s:
        raise SystemExit("buffer include marker missing")
    s = s.replace(old, new, 1)

old = '#include "backends/loopback/loopback_backend.h"\n'
new = old + '#ifdef SPWKIT_HAS_DEVICE\n#include "backends/device/device_backend.h"\n#endif\n'
if 'backends/device/device_backend.h' not in s:
    if old not in s:
        raise SystemExit("loopback include marker missing")
    s = s.replace(old, new, 1)

marker = "static spw_result_t validate_simulator_config(const spw_port_config_t* config) {"
validator = '''static spw_result_t validate_device_config(const spw_port_config_t* config) {
    const spw_device_config_t* device;
    size_t endpoint_length;
    if (config->backend_config == NULL ||
        config->backend_config_size < sizeof(spw_device_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    device = (const spw_device_config_t*)config->backend_config;
    if (device->struct_size < sizeof(spw_device_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (device->version != SPW_DEVICE_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (device->reserved != 0u || device->endpoint[0] == '\\0') {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint_length = strnlen(device->endpoint, SPW_DEVICE_ENDPOINT_CAPACITY);
    if (endpoint_length == 0u || endpoint_length >= SPW_DEVICE_ENDPOINT_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return SPW_OK;
}

'''
if "validate_device_config" not in s:
    if marker not in s:
        raise SystemExit("validator marker missing")
    s = s.replace(marker, validator + marker, 1)

udp_case = '''    case SPW_BACKEND_UDP:
        result = validate_udp_config(config);
        if (result != SPW_OK) {
            return result;
        }
#ifdef SPWKIT_HAS_UDP
        *out_factory = spw_udp_backend_factory();
        return SPW_OK;
#else
        return SPW_ERR_UNSUPPORTED;
#endif

'''
device_case = '''    case SPW_BACKEND_DEVICE:
        result = validate_device_config(config);
        if (result != SPW_OK) {
            return result;
        }
#ifdef SPWKIT_HAS_DEVICE
        *out_factory = spw_device_backend_factory();
        return SPW_OK;
#else
        return SPW_ERR_UNSUPPORTED;
#endif

'''
if "case SPW_BACKEND_DEVICE:" not in s:
    if udp_case not in s:
        raise SystemExit("UDP factory case marker missing")
    s = s.replace(udp_case, udp_case + device_case, 1)

p.write_text(s)

p = Path("cmake/SpWKitConfig.cmake.in")
s = p.read_text()
old = 'set(SpWKit_UDP_RUNTIME_SUPPORTED @SPWKIT_UDP_RUNTIME_SUPPORTED@)\nset(SpWKit_UDP_RUNTIME_SCOPE "POSIX")\n'
new = old + 'set(SpWKit_DEVICE_RUNTIME_SUPPORTED @SPWKIT_DEVICE_RUNTIME_SUPPORTED@)\nset(SpWKit_DEVICE_RUNTIME_SCOPE "Linux")\n'
if "SpWKit_DEVICE_RUNTIME_SUPPORTED" not in s:
    if old not in s:
        raise SystemExit("package metadata marker missing")
    s = s.replace(old, new, 1)
p.write_text(s)

Path(".github/workflows/patch-device-backend.yml").unlink(missing_ok=True)
Path(".github/patch_device.py").unlink(missing_ok=True)
