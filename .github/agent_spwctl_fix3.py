from pathlib import Path
p = Path("tests/vspw_device_protocol.c")
text = p.read_text()
old = '''    frame[5] = 1u;\n    assert(vspd_validate_frame(frame, sizeof(frame), NULL) ==\n           VSPD_CODEC_UNSUPPORTED_VERSION);\n'''
new = '''    frame[5] = (uint8_t)(VSPD_VERSION_MINOR + 1u);\n    assert(vspd_validate_frame(frame, sizeof(frame), NULL) ==\n           VSPD_CODEC_UNSUPPORTED_VERSION);\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one stale VSPD minor mutation, got {text.count(old)}")
p.write_text(text.replace(old, new, 1))
