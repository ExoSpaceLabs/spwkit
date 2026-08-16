from pathlib import Path

for path in (
    "src/backends/loopback/loopback_backend.c",
    "src/backends/virtual/simulator_backend.c",
    "src/backends/ethernet/udp_backend.c",
):
    p = Path(path)
    text = p.read_text()
    marker = "static const spw_backend_ops_t "
    start = text.find(marker)
    if start < 0:
        raise SystemExit(f"{path}: backend ops initializer not found")
    end = text.find("};", start)
    if end < 0:
        raise SystemExit(f"{path}: backend ops initializer end not found")
    prefix = text[:end].rstrip()
    if prefix.endswith("device_wait"):
        raise SystemExit(f"{path}: unexpected readiness implementation")
    text = prefix + ",\n    NULL\n" + text[end:]
    p.write_text(text)
