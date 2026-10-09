#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Negative and positive contracts for the CI transport evidence summarizer."""
from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("summarize_ci_campaign.py")
SPEC = importlib.util.spec_from_file_location("spwkit_ci_summary", MODULE_PATH)
assert SPEC and SPEC.loader
summary = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(summary)


def make_row(role: str, link: int, payload: int = 4096,
             transport: str = "udp") -> dict[str, object]:
    out: dict[str, object] = {
        "schema": summary.SCHEMAS[transport],
        "role": role, "link_id": link, "payload_bytes": payload,
        "total_bytes": 8192, "packets": 2, "elapsed_ns": 10_000_000,
        "payload_mbps": 6.5536, "link_errors": 0, "dropped_packets": 0,
    }
    if transport == "raw":
        out.update(ether_type=0x88B5, carrier_tx_frames=8,
                   carrier_tx_bytes=8768)
    return out


class CampaignEvidenceTests(unittest.TestCase):
    def sample(self, transport: str, profile: str,
               rows: list[dict[str, object]]) -> dict[str, object]:
        prefix = "raw-" if transport == "raw" else ""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / f"{prefix}{profile}-4096-r01.log"
            path.write_text("\n".join(
                "endpoint-1 | " + json.dumps(row, separators=(",", ":"))
                for row in rows
            ), encoding="utf-8")
            return summary.sample_from_log(path, transport)

    def test_uni_udp_paired(self) -> None:
        result = self.sample("udp", "uni", [
            make_row("source", 264), make_row("sink", 264)
        ])
        self.assertEqual(result["source_streams"], 1)
        self.assertEqual(result["logical_bytes"], 8192)

    def test_raw_duplex_distinct_links(self) -> None:
        rows = [
            make_row("source", 364, transport="raw"),
            make_row("sink", 364, transport="raw"),
            make_row("source", 365, transport="raw"),
            make_row("sink", 365, transport="raw"),
        ]
        for row in rows:
            if row["link_id"] == 365:
                row["ether_type"] = 0x88B6
        result = self.sample("raw", "duplex", rows)
        self.assertEqual(result["source_streams"], 2)
        self.assertEqual(result["logical_bytes"], 16384)

    def test_missing_sink_rejected(self) -> None:
        with self.assertRaises(ValueError):
            self.sample("udp", "uni", [make_row("source", 264)])

    def test_mismatched_bytes_rejected(self) -> None:
        sink = make_row("sink", 264)
        sink["total_bytes"] = 4096
        with self.assertRaisesRegex(ValueError, "total_bytes mismatch"):
            self.sample("udp", "uni", [make_row("source", 264), sink])

    def test_mismatched_link_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "link IDs do not match"):
            self.sample("udp", "uni", [
                make_row("source", 264), make_row("sink", 265)
            ])

    def test_dropped_packet_rejected(self) -> None:
        sink = make_row("sink", 264)
        sink["dropped_packets"] = 1
        with self.assertRaisesRegex(ValueError, "dropped_packets"):
            self.sample("udp", "uni", [make_row("source", 264), sink])

    def test_raw_ether_type_mismatch_rejected(self) -> None:
        sink = make_row("sink", 364, transport="raw")
        sink["ether_type"] = 0x88B6
        with self.assertRaisesRegex(ValueError, "EtherType mismatch"):
            self.sample("raw", "uni", [
                make_row("source", 364, transport="raw"), sink
            ])


if __name__ == "__main__":
    unittest.main()
