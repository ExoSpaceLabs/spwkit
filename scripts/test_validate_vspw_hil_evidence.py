#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name("validate_vspw_hil_evidence.py")
spec = importlib.util.spec_from_file_location("vspw_validate", SCRIPT)
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class EvidenceTests(unittest.TestCase):
    def evidence(self, path: Path, *, missing_bulk: bool = False):
        host = path / "host.jsonl"
        board = path / "board.log"
        full = {
            "schema": "spwkit.embedded.vspw-diagnostics.v1",
            "tx_data_frames": 5, "rx_data_frames": 5, "ack_matched": 1,
            "retry_attempts": 1, "ack_timeouts": 1,
        }
        bulk = {
            "schema": "spwkit.embedded.raw-ethernet-bulk.v1",
            "transferred_bytes": 4096, "packets": 1,
            "vspw_bulk_tx_data_frames": 3,
            "vspw_bulk_rx_data_frames": 3,
            "vspw_bulk_tx_ack_frames": 1,
            "vspw_bulk_rx_ack_frames": 1,
            "vspw_bulk_retry_attempts": 1,
            "vspw_bulk_retransmitted_frames": 1,
            "vspw_bulk_ack_timeouts": 1,
            "vspw_bulk_ack_matched": 1,
            "vspw_bulk_tx_slot_wait_calls": 1,
            "vspw_bulk_tx_slot_wait_us": 10,
            "vspw_bulk_provider_receive_us": 10,
        }
        host.write_text(json.dumps(full) + "\n" +
                        ("" if missing_bulk else json.dumps(bulk) + "\n") +
                        "HOST_RESULT: PASS\n", encoding="utf-8")
        board.write_text(
            "vspw_tx_data_frames=4\n"
            "vspw_rx_data_frames=4\n"
            "vspw_ack_matched=1\n"
            "vspw_retry_attempts=0\n"
            "vspw_ack_timeouts=0\n"
            "RESULT: PASS\n", encoding="utf-8")
        return host, board

    def test_valid_complete_bulk(self):
        with tempfile.TemporaryDirectory() as temp:
            host, board = self.evidence(Path(temp))
            self.assertIn("bulk records=1", mod.validate(host, board, 4096))

    def test_missing_bulk_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            host, board = self.evidence(Path(temp), missing_bulk=True)
            with self.assertRaisesRegex(ValueError, "one measured bulk"):
                mod.validate(host, board, 4096)

    def test_legacy_snapshot_must_not_silently_pass(self):
        with tempfile.TemporaryDirectory() as temp:
            host, board = self.evidence(Path(temp))
            host.write_text("HOST_RESULT: PASS\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "host VSPW snapshot"):
                mod.validate(host, board, 0)


if __name__ == "__main__":
    unittest.main()
