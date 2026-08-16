from pathlib import Path
p = Path("tests/vspw_device_protocol.c")
text = p.read_text()
old = '''        0x56u, 0x53u, 0x50u, 0x44u,\n        0x01u, 0x00u, 0x09u, 0x0eu,\n'''
new = '''        0x56u, 0x53u, 0x50u, 0x44u,\n        0x01u, 0x01u, 0x09u, 0x0eu,\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one VSPD golden version row, got {text.count(old)}")
p.write_text(text.replace(old, new, 1))
