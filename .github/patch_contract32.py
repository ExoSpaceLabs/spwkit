from pathlib import Path

path = Path("tests/contract/contract_suite.cpp")
text = path.read_text()

replacements = [
    (
        "namespace {\n\n[[noreturn]] void fail",
        "namespace {\n\nspw_timeout_us_t g_transfer_timeout_us = SPW_TIMEOUT_IMMEDIATE;\n\n[[noreturn]] void fail",
    ),
    (
        "require_result(spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE), SPW_OK,",
        "require_result(spw_port_send(port, &packet, g_transfer_timeout_us), SPW_OK,",
    ),
    (
        "require_result(spw_port_receive(port, &packet, SPW_TIMEOUT_IMMEDIATE), SPW_OK,",
        "require_result(spw_port_receive(port, &packet, g_transfer_timeout_us), SPW_OK,",
    ),
    (
        "require_result(spw_port_send(fixture.endpoint_a(), &tx, SPW_TIMEOUT_IMMEDIATE),\n                   SPW_OK, test, \"zero-length send failed\");",
        "require_result(spw_port_send(fixture.endpoint_a(), &tx, g_transfer_timeout_us),\n                   SPW_OK, test, \"zero-length send failed\");",
    ),
    (
        "require_result(spw_port_receive(fixture.endpoint_b(), &rx, SPW_TIMEOUT_IMMEDIATE),\n                   SPW_OK, test, \"zero-length receive failed\");",
        "require_result(spw_port_receive(fixture.endpoint_b(), &rx, g_transfer_timeout_us),\n                   SPW_OK, test, \"zero-length receive failed\");",
    ),
    (
        "require_result(spw_port_receive(fixture.endpoint_b(), &short_rx,\n                                    SPW_TIMEOUT_IMMEDIATE),",
        "require_result(spw_port_receive(fixture.endpoint_b(), &short_rx,\n                                    g_transfer_timeout_us),",
    ),
    (
        "require_result(spw_port_send_time_code(fixture.endpoint_a(), &tx,\n                                           SPW_TIMEOUT_IMMEDIATE),",
        "require_result(spw_port_send_time_code(fixture.endpoint_a(), &tx,\n                                           g_transfer_timeout_us),",
    ),
    (
        "require_result(spw_port_receive_time_code(fixture.endpoint_b(), &rx,\n                                              SPW_TIMEOUT_IMMEDIATE),",
        "require_result(spw_port_receive_time_code(fixture.endpoint_b(), &rx,\n                                              g_transfer_timeout_us),",
    ),
    (
        "int run_backend_contract(BackendContractFixture& fixture) {\n    const spw_capabilities_t caps_a",
        "int run_backend_contract(BackendContractFixture& fixture) {\n    g_transfer_timeout_us = fixture.transfer_timeout_us();\n    const spw_capabilities_t caps_a",
    ),
    (
        "              << \" rxDepthB=\" << caps_b.rx_queue_depth\n              << '\\n';",
        "              << \" rxDepthB=\" << caps_b.rx_queue_depth\n              << \" transferTimeoutUs=\" << fixture.transfer_timeout_us()\n              << '\\n';",
    ),
]

for old, new in replacements:
    if old not in text:
        raise SystemExit(f"missing expected contract snippet: {old[:80]!r}")
    text = text.replace(old, new, 1)

path.write_text(text)
