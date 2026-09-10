#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class MetricSpec:
    name: str
    relative_path: str
    value_path: tuple[str, ...]
    kind: str = "steady-state"


# Use complete-operation medians for LOOPBACK/SIMULATOR. For paths with an
# in-process native comparator, compare the measured SpWKit-minus-native delta
# so host/transport cost is not silently attributed to the library release.
METRICS = [
    MetricSpec("loopback.tx", "backends/loopback_tx.jsonl", ("statistics", "median")),
    MetricSpec("loopback.rx", "backends/loopback_rx.jsonl", ("statistics", "median")),
    MetricSpec("simulator.tx", "backends/simulator_tx.jsonl", ("statistics", "median")),
    MetricSpec("simulator.rx", "backends/simulator_rx.jsonl", ("statistics", "median")),
    MetricSpec("driver.copied.tx.overhead", "comparison/tx_api_native.jsonl", ("delta", "median_ticks")),
    MetricSpec("driver.copied.rx.overhead", "comparison/rx_native_api.jsonl", ("delta", "median_ticks")),
    MetricSpec("udp.tx.overhead", "comparison/udp_tx.jsonl", ("delta", "median_ticks")),
    MetricSpec("udp.rx.overhead", "comparison/udp_rx.jsonl", ("delta", "median_ticks")),
    MetricSpec("device.tx.overhead", "comparison/device_tx.jsonl", ("delta", "median_ticks")),
    MetricSpec("device.rx.overhead", "comparison/device_rx.jsonl", ("delta", "median_ticks")),
    MetricSpec("device.readiness.overhead", "comparison/device_readiness.jsonl", ("delta", "median_ticks")),
    MetricSpec("driver.zero-copy.tx", "comparison/driver_tx_copy_zero_copy.jsonl", ("zero_copy_statistics", "median")),
    MetricSpec("driver.zero-copy.tx.acquire", "comparison/driver_tx_copy_zero_copy.jsonl", ("ownership_statistics", "acquire", "median")),
    MetricSpec("driver.zero-copy.tx.submit", "comparison/driver_tx_copy_zero_copy.jsonl", ("ownership_statistics", "submit", "median")),
    MetricSpec("driver.zero-copy.rx", "comparison/driver_rx_copy_zero_copy.jsonl", ("zero_copy_visibility_statistics", "median")),
    MetricSpec("driver.zero-copy.rx.acquire", "comparison/driver_rx_copy_zero_copy.jsonl", ("ownership_statistics", "acquire", "median")),
    MetricSpec("driver.zero-copy.rx.release", "comparison/driver_rx_copy_zero_copy.jsonl", ("ownership_statistics", "release", "median")),
]


def load_json(path: Path):
    return json.loads(path.read_text())


def load_jsonl(path: Path):
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def nested(row, path):
    value = row
    for key in path:
        value = value[key]
    return float(value)


def row_key(row):
    preferred = (
        "payload_bytes",
        "direction",
        "operation",
        "allocation",
        "case",
        "case_name",
        "stage",
    )
    parts = []
    for key in preferred:
        if key in row and not isinstance(row[key], (dict, list)):
            parts.append((key, row[key]))
    if not parts:
        raise ValueError(f"cannot derive stable row identity from keys: {sorted(row)}")
    return tuple(parts)


def comparable_environment(baseline, candidate):
    b = baseline.get("host_environment", {})
    c = candidate.get("host_environment", {})
    fields = [
        ("architecture", b.get("architecture"), c.get("architecture")),
        ("cpu_model", b.get("cpu_model"), c.get("cpu_model")),
        (
            "compiler.family",
            b.get("compiler", {}).get("family"),
            c.get("compiler", {}).get("family"),
        ),
        (
            "counter.kind",
            b.get("counter", {}).get("kind"),
            c.get("counter", {}).get("kind"),
        ),
        (
            "measurement_affinity",
            b.get("measurement_affinity"),
            c.get("measurement_affinity"),
        ),
    ]
    mismatches = [
        {"field": field, "baseline": left, "candidate": right}
        for field, left, right in fields
        if left not in (None, "unknown")
        and right not in (None, "unknown")
        and left != right
    ]

    b_control = baseline.get("host_control", {})
    c_control = candidate.get("host_control", {})
    if bool(b_control.get("enabled")) != bool(c_control.get("enabled")):
        mismatches.append(
            {
                "field": "host_control.enabled",
                "baseline": bool(b_control.get("enabled")),
                "candidate": bool(c_control.get("enabled")),
            }
        )
    return mismatches


def compare_rows(name, b_rows, c_rows, value_path, rel_threshold, abs_threshold, kind):
    b_map = {row_key(row): row for row in b_rows}
    c_map = {row_key(row): row for row in c_rows}
    results = []
    for key in sorted(set(b_map) & set(c_map), key=str):
        b_value = nested(b_map[key], value_path)
        c_value = nested(c_map[key], value_path)
        delta = c_value - b_value
        percent = (
            None
            if math.isclose(b_value, 0.0, abs_tol=1e-12)
            else (delta / abs(b_value)) * 100.0
        )
        attention = delta > abs_threshold and (
            percent is None or percent > rel_threshold
        )
        results.append(
            {
                "metric": name,
                "kind": kind,
                "identity": dict(key),
                "baseline": b_value,
                "candidate": c_value,
                "delta_ticks": delta,
                "delta_percent": percent,
                "attention": attention,
            }
        )
    return results


def compare_lifecycle(b_root, c_root, rel_threshold, abs_threshold):
    b_rows = load_jsonl(b_root / "lifecycle.jsonl")
    c_rows = load_jsonl(c_root / "lifecycle.jsonl")
    if not b_rows or not c_rows:
        return []
    return compare_rows(
        "lifecycle",
        b_rows,
        c_rows,
        ("statistics", "median"),
        rel_threshold,
        abs_threshold,
        "lifecycle",
    )


def fmt(value):
    if value is None:
        return "n/a"
    return f"{value:.1f}"


def render_markdown(report):
    lines = [
        "# SpWKit release performance comparison",
        "",
        f"- Baseline: `{report['baseline']['git_sha']}`",
        f"- Candidate: `{report['candidate']['git_sha']}`",
        f"- Environment comparable: **{'yes' if report['environment_comparable'] else 'no'}**",
        f"- Attention rows: **{report['attention_count']}**",
        "",
    ]
    if report["environment_mismatches"]:
        lines += ["## Environment mismatches", ""]
        for item in report["environment_mismatches"]:
            lines.append(
                f"- `{item['field']}`: baseline `{item['baseline']}`, "
                f"candidate `{item['candidate']}`"
            )
        lines.append("")

    lines += [
        "## Matched metrics",
        "",
        "| Metric | Identity | Baseline | Candidate | Delta ticks | Delta % | Status |",
        "|---|---|---:|---:|---:|---:|---|",
    ]
    for row in report["rows"]:
        identity = ", ".join(f"{key}={value}" for key, value in row["identity"].items())
        status = "ATTENTION" if row["attention"] else "ok"
        lines.append(
            f"| `{row['metric']}` | {identity} | {fmt(row['baseline'])} | "
            f"{fmt(row['candidate'])} | {fmt(row['delta_ticks'])} | "
            f"{fmt(row['delta_percent'])} | {status} |"
        )

    lines += [
        "",
        "An `ATTENTION` row is a triage signal, not automatic proof of a regression. "
        "Repeat suspect rows under the controlled-host policy before accepting or "
        "optimizing a release change.",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Compare SpWKit profiling campaigns across releases"
    )
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--baseline-lifecycle", type=Path)
    parser.add_argument("--candidate-lifecycle", type=Path)
    parser.add_argument(
        "--relative-threshold",
        type=float,
        default=5.0,
        help="positive percentage change required for attention (default: 5)",
    )
    parser.add_argument(
        "--absolute-threshold",
        type=float,
        default=20.0,
        help="positive tick change required for attention (default: 20)",
    )
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-markdown", type=Path)
    parser.add_argument("--require-comparable-environment", action="store_true")
    parser.add_argument("--fail-on-attention", action="store_true")
    args = parser.parse_args()

    b_campaign = load_json(args.baseline / "campaign.json")
    c_campaign = load_json(args.candidate / "campaign.json")
    mismatches = comparable_environment(b_campaign, c_campaign)

    rows = []
    missing = []
    for spec in METRICS:
        b_path = args.baseline / spec.relative_path
        c_path = args.candidate / spec.relative_path
        if not b_path.exists() or not c_path.exists():
            missing.append(
                {
                    "metric": spec.name,
                    "baseline_present": b_path.exists(),
                    "candidate_present": c_path.exists(),
                }
            )
            continue
        rows.extend(
            compare_rows(
                spec.name,
                load_jsonl(b_path),
                load_jsonl(c_path),
                spec.value_path,
                args.relative_threshold,
                args.absolute_threshold,
                spec.kind,
            )
        )

    if args.baseline_lifecycle or args.candidate_lifecycle:
        if not (args.baseline_lifecycle and args.candidate_lifecycle):
            parser.error(
                "baseline and candidate lifecycle directories must be supplied together"
            )
        rows.extend(
            compare_lifecycle(
                args.baseline_lifecycle,
                args.candidate_lifecycle,
                args.relative_threshold,
                args.absolute_threshold,
            )
        )

    report = {
        "schema": "spwkit.profile.release-comparison.v1",
        "baseline": {
            "git_sha": b_campaign.get("git_sha", "unknown"),
            "result_type": b_campaign.get("result_type", "unknown"),
        },
        "candidate": {
            "git_sha": c_campaign.get("git_sha", "unknown"),
            "result_type": c_campaign.get("result_type", "unknown"),
        },
        "environment_comparable": not mismatches,
        "environment_mismatches": mismatches,
        "relative_threshold_percent": args.relative_threshold,
        "absolute_threshold_ticks": args.absolute_threshold,
        "matched_row_count": len(rows),
        "attention_count": sum(1 for row in rows if row["attention"]),
        "missing_metrics": missing,
        "rows": rows,
    }

    text = render_markdown(report)
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(report, indent=2) + "\n")
    if args.output_markdown:
        args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
        args.output_markdown.write_text(text)
    print(text)

    if args.require_comparable_environment and mismatches:
        return 2
    if args.fail_on_attention and report["attention_count"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
