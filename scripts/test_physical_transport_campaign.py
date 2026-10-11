#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""No-hardware contract checks of the one-command physical HIL orchestrator."""
import argparse
import importlib.util
import json
import subprocess
from pathlib import Path
import tempfile
import unittest
from unittest import mock

PATH = Path(__file__).with_name("physical_transport_campaign.py")
SPEC = importlib.util.spec_from_file_location("physical_transport_campaign", PATH)
assert SPEC and SPEC.loader
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class FakePi:
    def __init__(self):
        self.commands = []
    def command(self, cmd, **kwargs):
        self.commands.append((cmd, kwargs))
        if cmd.startswith('printf'):
            return "/home/operator"
        if "spwkit-hil" in cmd and "choose_interface" in cmd:
            return "eth0"
        return ""


class OrchestrationContracts(unittest.TestCase):
    def test_pi_checkout_is_owned_and_revision_pinned(self):
        pi = FakePi()
        sha = "a" * 40
        path = module.prepare_checkout(pi, sha)
        self.assertEqual(path, "/home/operator/.cache/spwkit-hil/" + sha)
        self.assertIn("checkout --detach " + sha, pi.commands[1][0])
        self.assertIn("git clone --branch", pi.commands[1][0])

    def test_auto_pi_nic(self):
        pi = FakePi()
        self.assertEqual(
            module.find_pi_ethernet(pi, "/home/operator/.cache/spwkit-hil/test"),
            "eth0"
        )

    def test_existing_complete_pi_results_reused(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            folder = repo / "build/physical-pi/results-20261011T145919Z"
            folder.mkdir(parents=True)
            modes = [{"transport": tr, "mode": mode}
                     for tr in ("udp", "raw")
                     for mode in ("pc-to-pi", "pi-to-pc", "duplex")]
            (folder / "samples.json").write_text(json.dumps(modes * 23))
            for name in ("README.md", "environment.json", "results.csv"):
                (folder / name).write_text("{}")
            with mock.patch.object(module, "ROOT", repo):
                self.assertEqual(module.completed_pi_baseline(), folder)

    def test_incomplete_pi_not_reused(self):
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            folder = repo / "build/physical-pi/results-20261011T145919Z"
            folder.mkdir(parents=True)
            (folder / "samples.json").write_text(json.dumps([{"transport":"udp","mode":"pc-to-pi"}]))
            for name in ("README.md", "environment.json", "results.csv"):
                (folder / name).write_text("{}")
            with mock.patch.object(module, "ROOT", repo):
                self.assertIsNone(module.completed_pi_baseline())

    def test_summary_reports_incomplete_phases(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "pc-pi").mkdir()
            (folder / "pc-pi/README.md").write_text("# baseline\n")
            module.summary(folder, "b" * 40)
            result = (folder / "README.md").read_text()
            self.assertIn("pc-pi/README.md", result)
            self.assertIn("pi-stm32: INCOMPLETE", result)

    def test_cross_topology_comparison_all_four_frequencies(self):
        import csv
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, scale in (("pc-stm32", 1.0), ("pi-stm32", 0.8)):
                folder = root / name
                folder.mkdir()
                with (folder / "results.csv").open("w", newline="") as stream:
                    writer = csv.DictWriter(
                        stream, fieldnames=("kind", "clock_hz", "payload_bytes",
                                            "one_way_payload_mbps", "median_ns"))
                    writer.writeheader()
                    for mhz in (64, 200, 300, 400):
                        writer.writerow({"kind": "bulk", "clock_hz": mhz * 1000000,
                                         "payload_bytes": 4096,
                                         "one_way_payload_mbps": mhz * scale / 10,
                                         "median_ns": ""})
                        writer.writerow({"kind": "rtt", "clock_hz": mhz * 1000000,
                                         "payload_bytes": 4096,
                                         "one_way_payload_mbps": "",
                                         "median_ns": int(1000000 / scale)})
            text = "\n".join(module.comparison_data(root))
            self.assertIn("64 MHz", text)
            self.assertIn("400 MHz", text)
            self.assertIn("0.800×", text)

    def test_remote_rejects_injected_options(self):
        with self.assertRaises(ValueError):
            module.RemotePi("-oProxyCommand=evil", "/tmp/mux")

    def test_benchmark_and_stress_plans_without_hardware(self):
        runner = Path(__file__).with_name("stm32h755_das_raw_eth_campaign.sh")
        for profile, clocks, size in (
            ("benchmark", "64000000 200000000 300000000 400000000", "16777216"),
            ("stress", "400000000", "1073741824"),
        ):
            process = subprocess.run(
                ["bash", str(runner), "--profile", profile, "--show-plan"],
                capture_output=True, text=True, check=True
            )
            self.assertIn("profile=" + profile, process.stdout)
            self.assertIn("clocks=" + clocks, process.stdout)
            self.assertIn("bulk_bytes=" + size, process.stdout)
            self.assertIn("no package installation", process.stdout)

    def test_stress_comparison_only_requires_400_mhz(self):
        import csv
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("pc-stm32", "pi-stm32"):
                folder = root / name
                folder.mkdir()
                with (folder / "results.csv").open("w", newline="") as stream:
                    w = csv.DictWriter(stream, fieldnames=(
                        "clock_hz", "payload_bytes", "kind",
                        "median_ns", "one_way_payload_mbps"))
                    w.writeheader()
                    w.writerow(dict(clock_hz=400000000, payload_bytes=4096,
                                    kind="bulk", one_way_payload_mbps=10.0))
                    w.writerow(dict(clock_hz=400000000, payload_bytes=4096,
                                    kind="rtt", median_ns=1000000))
            result = "\n".join(module.comparison_data(root, "stress"))
            self.assertIn("400 MHz", result)
            self.assertNotIn("64 MHz", result)

    def test_full_clock_campaign_not_customized(self):
        file = Path(__file__).with_name("stm32h755_das_raw_eth_campaign.sh")
        content = file.read_text()
        self.assertIn("64000000 200000000 300000000 400000000", content)


if __name__ == "__main__":
    unittest.main()
