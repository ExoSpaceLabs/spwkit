#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import json
import tempfile
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("aggregate_release_profile_repeats.py")
spec = importlib.util.spec_from_file_location("repeat_summary", MODULE_PATH)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)


def write_report(root: Path, run: int, rows):
    path = root / f"run-{run}" / "comparison.json"
    path.parent.mkdir(parents=True)
    path.write_text(
        json.dumps(
            {
                "schema": "spwkit.profile.release-comparison.v1",
                "baseline": {"git_sha": "baseline"},
                "candidate": {"git_sha": "candidate"},
                "environment_comparable": True,
                "rows": rows,
            }
        )
        + "\n"
    )


def row(metric, payload, delta, attention):
    return {
        "metric": metric,
        "identity": {"payload_bytes": payload, "direction": "rx"},
        "delta_ticks": delta,
        "attention": attention,
    }


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    write_report(root, 1, [row("stable", 64, 24.0, True), row("noise", 64, 24.0, True)])
    write_report(root, 2, [row("stable", 64, 25.0, True), row("noise", 64, -1.0, False)])
    write_report(root, 3, [row("stable", 64, 1.0, False), row("noise", 64, 0.0, False)])

    report = module.aggregate(module.load_reports(root), 3, 2)
    by_metric = {item["metric"]: item for item in report["rows"]}

    assert report["recurring_attention_count"] == 1
    assert by_metric["stable"]["status"] == "RECURRING_ATTENTION"
    assert by_metric["stable"]["attention_runs"] == 2
    assert by_metric["stable"]["median_delta_ticks"] == 24.0
    assert by_metric["noise"]["status"] == "single/noisy-attention"

print("repeat aggregation tests passed")
