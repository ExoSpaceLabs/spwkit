#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Hardware-free HIL summary compatibility checks, including bulk-only deltas."""
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("summarize_stm32h755_das_raw_eth.py")


class HilSummaryTests(unittest.TestCase):
    def test_bulk_diagnostics_rendered_without_confusing_cumulative_counts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            host = root / "host.jsonl"
            board = root / "board.log"
            report = root / "report.md"
            host.write_text(
                json.dumps({
                    "schema": "spwkit.embedded.raw-ethernet-rtt.v1",
                    "payload_bytes": 4096, "median_ns": 5000000,
                    "p95_ns": 5500000, "p99_ns": 5600000,
                    "min_ns": 4800000, "max_ns": 6000000,
                }) + "\n" + json.dumps({
                    "schema": "spwkit.embedded.raw-ethernet-bulk.v1",
                    "payload_bytes": 4096, "transferred_bytes": 4194304,
                    "packets": 1024, "elapsed_ns": 900000000,
                    "one_way_payload_mbps": 37.282,
                    "aggregate_echo_mbps": 74.564,
                    "vspw_bulk_tx_data_frames": 5120,
                    "vspw_bulk_rx_data_frames": 5120,
                    "vspw_bulk_tx_ack_frames": 1024,
                    "vspw_bulk_rx_ack_frames": 1024,
                    "vspw_bulk_retry_attempts": 0,
                    "vspw_bulk_ack_timeouts": 0,
                    "vspw_bulk_retransmitted_frames": 0,
                }) + "\n", encoding="utf-8"
            )
            fields = {
                "core_hz": 400000000,
                "echoed_packets": 1200, "das_rx_empty_polls": 400,
                "link_speed_mbps": 100, "vspw_tx_data_frames": 6200,
                "vspw_retry_attempts": 3,
            }
            for prefix in (
                "app_send", "app_receive", "das_tx",
                "das_rx_success", "das_rx_poll"
            ):
                fields.update({
                    f"{prefix}_count": 100,
                    f"{prefix}_min_cycles": 5,
                    f"{prefix}_max_cycles": 20,
                    f"{prefix}_total_cycles": 1000,
                })
            board.write_text(
                "\n".join(f"{key}={value}" for key, value in fields.items())
                + "\n", encoding="utf-8"
            )
            subprocess.run([
                sys.executable, str(SCRIPT), "--host", str(host),
                "--board", str(board), "--output", str(report),
                "--linux-host", "pi",
            ], check=True, capture_output=True, text=True)
            rendered = report.read_text(encoding="utf-8")
            self.assertIn("Physical Pi↔STM32 RTT", rendered)
            self.assertIn("VSPW-TP host bulk-phase diagnostics", rendered)
            self.assertIn("| vspw_bulk_tx_data_frames | 5120 |", rendered)
            self.assertIn("| vspw_bulk_retry_attempts | 0 |", rendered)
            self.assertIn("VSPW-TP diagnostic counters (board)", rendered)
            self.assertIn("| vspw_retry_attempts | 3 |", rendered)

    def test_legacy_host_record_remains_supported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            host = root / "old.jsonl"
            host.write_text(json.dumps({
                "schema": "spwkit.embedded.raw-ethernet-rtt.v1",
                "payload_bytes": 16, "median_ns": 100, "p95_ns": 200,
                "p99_ns": 210, "min_ns": 90, "max_ns": 240,
            }) + "\n", encoding="utf-8")
            # Parse only. Old RTT JSON is intentionally not rejected.
            import importlib.util
            spec = importlib.util.spec_from_file_location("hil_summary", SCRIPT)
            assert spec and spec.loader
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            rtt, bulk = mod.parse_host(host)
            self.assertEqual(len(rtt), 1)
            self.assertEqual(bulk, [])


if __name__ == "__main__":
    unittest.main()
