from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing marker in {path}: {old!r}")
    p.write_text(text.replace(old, new, 1))

replace_once(
    "src/vspwd/main.c",
    '            "  --udp-link-id ID           default 42\\n",',
    '            "  --udp-link-id ID           default 42\\n"\n            "  --udp-ack-timeout-ms MS    default 100\\n"\n            "  --udp-keepalive-ms MS      default 1000\\n"\n            "  --udp-peer-timeout-ms MS   default 3000\\n",')
replace_once(
    "src/vspwd/main.c",
    '''        } else if (strcmp(argv[i], "--udp-link-id") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.link_id) ||
                config.udp_bridge.udp.link_id == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0 ||''',
    '''        } else if (strcmp(argv[i], "--udp-link-id") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.link_id) ||
                config.udp_bridge.udp.link_id == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-ack-timeout-ms") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.ack_timeout_ms) ||
                config.udp_bridge.udp.ack_timeout_ms == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-keepalive-ms") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.keepalive_interval_ms) ||
                config.udp_bridge.udp.keepalive_interval_ms == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--udp-peer-timeout-ms") == 0) {
            if (++i >= argc || !parse_u32(argv[i], &config.udp_bridge.udp.peer_timeout_ms) ||
                config.udp_bridge.udp.peer_timeout_ms == 0u) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--help") == 0 ||''')

replace_once(
    "tests/device/run_udp_bridge.sh",
    '''  --udp-remote-port "$remote_udp_port" \\
  --udp-link-id 4242 >"$tmpdir/vspwd.log" 2>&1 &''',
    '''  --udp-remote-port "$remote_udp_port" \\
  --udp-link-id 4242 \\
  --udp-ack-timeout-ms 50 \\
  --udp-keepalive-ms 100 \\
  --udp-peer-timeout-ms 500 >"$tmpdir/vspwd.log" 2>&1 &''')

p = Path("tests/device/run_spwctl.sh")
text = p.read_text()
text = text.replace("^0 no no no ERROR_RESET", "^0 no no no no ERROR_RESET")
text = text.replace("^1 no no no ERROR_RESET", "^1 no no no no ERROR_RESET")
text = text.replace("^0 yes yes no RUN", "^0 no yes yes no RUN")
text = text.replace("^1 yes yes no RUN", "^1 no yes yes no RUN")
p.write_text(text)

p = Path("docs/vspwd.md")
text = p.read_text()
text = text.replace(
    "  --udp-remote-address 127.0.0.1 \\\n  --udp-link-id 42",
    "  --udp-remote-address 127.0.0.1 \\\n  --udp-link-id 42 \\\n  --udp-keepalive-ms 1000 \\\n  --udp-peer-timeout-ms 3000")
p.write_text(text)

print("bridge liveness/test fix applied")
