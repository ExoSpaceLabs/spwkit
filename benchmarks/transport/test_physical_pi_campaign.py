#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Unit tests for campaign orchestration without physical NICs or sudo."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

MODULE = Path(__file__).with_name("physical_pi_campaign.py")
SPEC = importlib.util.spec_from_file_location("physical_pi_campaign", MODULE)
assert SPEC and SPEC.loader
campaign = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(campaign)


def synthetic(side, role, link, transport, payload, total):
    schema = campaign.WIRE[transport]
    result = {
        "schema": schema, "role": role, "link_id": link, "payload_bytes": payload,
        "total_bytes": total, "packets": (total + payload - 1) // payload,
        "payload_mbps": 25.0, "elapsed_ns": 3000000,
        "dropped_packets": 0, "link_errors": 0,
    }
    if transport == "raw":
        result["ether_type"] = 0x88B5 if link == 364 else 0x88B6
    return [sys.executable, "-c", "import json;print(json.dumps(" + repr(result) + "))"]


class PhysicalPiContract(unittest.TestCase):
    def setUp(self):
        self.args = argparse.Namespace(
            pi_host="pi@10.77.77.2", pi_address="10.77.77.2/24",
            pc_address="10.77.77.1/24",
        )

    def test_direction_identifiers(self):
        self.assertEqual(campaign.direction_spec("udp", "pc-to-pi")[0], 264)
        self.assertEqual(campaign.direction_spec("udp", "pi-to-pc")[0], 265)
        self.assertEqual(campaign.direction_spec("raw", "pc-to-pi")[0], 364)
        self.assertEqual(campaign.direction_spec("raw", "pi-to-pc")[0], 365)

    def test_command_uses_distinct_duplex_carriers(self):
        pc = campaign.pc_command(
            self.args, "raw", "source", 364, 4096, 8192,
            "02:42:ac:1e:00:11", "eth0")
        self.assertEqual(pc[pc.index("--ether-type") + 1], "0x88b5")
        pc = campaign.pc_command(
            self.args, "raw", "sink", 365, 4096, 8192,
            "02:42:ac:1e:00:11", "eth0")
        self.assertEqual(pc[pc.index("--ether-type") + 1], "0x88b6")

    def simulate(self, transport, direction):
        def ssh_side(_args, _control, parameters):
            values = dict(zip(parameters[::2], parameters[1::2]))
            return synthetic("pi", values["--role"], int(values["--link-id"]),
                             values["--transport"], int(values["--payload"]),
                             int(values["--bytes"]))

        def pc_side(_args, tr, role, link, payload, total, _mac, _iface):
            return synthetic("pc", role, link, tr, payload, total)

        with tempfile.TemporaryDirectory() as tmp:
            with mock.patch.object(campaign, "ssh_cmd", side_effect=ssh_side), \
                 mock.patch.object(campaign, "pc_command", side_effect=pc_side), \
                 mock.patch.object(campaign, "mac_address", return_value="02:42:ac:1e:00:10"), \
                 mock.patch.object(campaign.time, "sleep", return_value=None):
                case = campaign.test_case(
                    self.args, "", "eth0", "02:42:ac:1e:00:11",
                    transport, direction, 4096, 8192, 1, Path(tmp))
            return case

    def test_udp_both_directions_and_duplex(self):
        for mode in ("pc-to-pi", "pi-to-pc", "duplex"):
            case = self.simulate("udp", mode)
            self.assertEqual(len(case["streams"]), 2 if mode == "duplex" else 1)

    def test_raw_both_directions_and_duplex(self):
        for mode in ("pc-to-pi", "pi-to-pc", "duplex"):
            case = self.simulate("raw", mode)
            self.assertEqual(len(case["streams"]), 2 if mode == "duplex" else 1)

    def test_report_contains_data(self):
        case = self.simulate("raw", "duplex")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            campaign.report(root, [case], {"pc": {"sha": "a" * 40}})
            self.assertIn("duplex", (root / "README.md").read_text())
            self.assertTrue(json.loads((root / "samples.json").read_text()))
            self.assertEqual(len((root / "results.csv").read_text().splitlines()), 2)

    def test_json_rejects_missing_result(self):
        with self.assertRaises(RuntimeError):
            campaign.read_json('{"schema":"wrong"}', campaign.WIRE["udp"])


if __name__ == "__main__":
    unittest.main()
