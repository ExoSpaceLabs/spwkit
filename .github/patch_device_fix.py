from pathlib import Path

p = Path('src/core/port.c')
s = p.read_text()
old = '''    endpoint_length = strnlen(device->endpoint, SPW_DEVICE_ENDPOINT_CAPACITY);
    if (endpoint_length == 0u || endpoint_length >= SPW_DEVICE_ENDPOINT_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
'''
new = '''    endpoint_length = 0u;
    while (endpoint_length < SPW_DEVICE_ENDPOINT_CAPACITY &&
           device->endpoint[endpoint_length] != '\\0') {
        ++endpoint_length;
    }
    if (endpoint_length == 0u || endpoint_length >= SPW_DEVICE_ENDPOINT_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
'''
if old not in s:
    raise SystemExit('device endpoint validation marker missing')
p.write_text(s.replace(old, new, 1))

Path('.github/workflows/patch-device-fix.yml').unlink(missing_ok=True)
Path('.github/patch_device_fix.py').unlink(missing_ok=True)

# Trigger update; file is removed by the successful patch.
