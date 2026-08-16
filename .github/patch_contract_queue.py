from pathlib import Path

path = Path("tests/contract/contract_suite.cpp")
text = path.read_text()
old = '''    require_result(spw_port_send(fixture.endpoint_a(), &tx, SPW_TIMEOUT_IMMEDIATE),
                   SPW_OK, test, "queue did not recover after drain");
'''
new = '''    require_result(spw_port_send(fixture.endpoint_a(), &tx, g_transfer_timeout_us),
                   SPW_OK, test, "queue did not recover after drain");
'''
if text.count(old) != 1:
    raise SystemExit(f"expected exactly one queue-recovery snippet, found {text.count(old)}")
path.write_text(text.replace(old, new))
