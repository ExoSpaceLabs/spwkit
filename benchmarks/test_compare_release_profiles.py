#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import json
import tempfile
from pathlib import Path


def load_module():
    path = Path(__file__).with_name("compare_release_profiles.py")
    spec = importlib.util.spec_from_file_location("compare_release_profiles", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def write_campaign(root: Path, sha: str, median: float, overhead: float):
    (root / "backends").mkdir(parents=True)
    (root / "comparison").mkdir()
    campaign = {
        "git_sha": sha,
        "result_type": "test",
        "host_environment": {
            "architecture": "x86_64",
            "cpu_model": "synthetic",
            "compiler": {"family": "gcc"},
            "counter": {"kind": "tsc"},
            "measurement_affinity": "cpu:0",
        },
        "host_control": {"enabled": True},
    }
    (root / "campaign.json").write_text(json.dumps(campaign) + "\n")
    (root / "backends" / "loopback_tx.jsonl").write_text(
        json.dumps({"payload_bytes": 64, "statistics": {"median": median}}) + "\n"
    )
    (root / "comparison" / "tx_api_native.jsonl").write_text(
        json.dumps(
            {
                "payload_bytes": 64,
                "native_statistics": {"median": 20},
                "spwkit_statistics": {"median": 20 + overhead},
                "delta": {"median_ticks": overhead},
            }
        )
        + "\n"
    )


def main():
    compare = load_module()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        baseline = root / "baseline"
        candidate = root / "candidate"
        write_campaign(baseline, "baseline", 100.0, 30.0)
        write_campaign(candidate, "candidate", 130.0, 32.0)

        mismatches = compare.comparable_environment(
            compare.load_json(baseline / "campaign.json"),
            compare.load_json(candidate / "campaign.json"),
        )
        assert mismatches == []

        loopback = compare.compare_rows(
            "loopback.tx",
            compare.load_jsonl(baseline / "backends" / "loopback_tx.jsonl"),
            compare.load_jsonl(candidate / "backends" / "loopback_tx.jsonl"),
            ("statistics", "median"),
            5.0,
            20.0,
            "steady-state",
        )
        assert len(loopback) == 1
        assert loopback[0]["attention"] is True
        assert loopback[0]["delta_ticks"] == 30.0

        overhead = compare.compare_rows(
            "driver.copied.tx.overhead",
            compare.load_jsonl(baseline / "comparison" / "tx_api_native.jsonl"),
            compare.load_jsonl(candidate / "comparison" / "tx_api_native.jsonl"),
            ("delta", "median_ticks"),
            5.0,
            20.0,
            "steady-state",
        )
        assert len(overhead) == 1
        assert overhead[0]["attention"] is False
        assert overhead[0]["delta_ticks"] == 2.0

        candidate_campaign = compare.load_json(candidate / "campaign.json")
        candidate_campaign["host_environment"]["cpu_model"] = "different"
        assert compare.comparable_environment(
            compare.load_json(baseline / "campaign.json"), candidate_campaign
        )

    print("release profile comparator self-test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
