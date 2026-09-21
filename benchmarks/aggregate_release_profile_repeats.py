#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path


def load_reports(root: Path):
    paths = sorted(root.rglob("comparison.json"))
    reports = []
    for path in paths:
        report = json.loads(path.read_text())
        if report.get("schema") != "spwkit.profile.release-comparison.v1":
            continue
        reports.append((path, report))
    return reports


def row_key(row):
    return (
        row["metric"],
        tuple(sorted(row.get("identity", {}).items())),
    )


def render_identity(identity):
    return ", ".join(f"{key}={value}" for key, value in identity)


def aggregate(reports, expected_runs, min_attention_runs):
    if len(reports) != expected_runs:
        raise ValueError(
            f"expected {expected_runs} release-comparison reports, found {len(reports)}"
        )

    grouped = defaultdict(list)
    comparable_runs = 0
    baseline_shas = set()
    candidate_shas = set()

    for path, report in reports:
        if report.get("environment_comparable"):
            comparable_runs += 1
        baseline_shas.add(report.get("baseline", {}).get("git_sha", "unknown"))
        candidate_shas.add(report.get("candidate", {}).get("git_sha", "unknown"))
        for row in report.get("rows", []):
            grouped[row_key(row)].append((path, row))

    rows = []
    for (metric, identity), samples in sorted(grouped.items(), key=str):
        deltas = [float(row["delta_ticks"]) for _, row in samples]
        attention_runs = sum(1 for _, row in samples if row.get("attention"))
        positive_runs = sum(1 for value in deltas if value > 0)
        negative_runs = sum(1 for value in deltas if value < 0)
        zero_runs = len(deltas) - positive_runs - negative_runs
        recurring = (
            len(samples) == expected_runs
            and attention_runs >= min_attention_runs
            and positive_runs >= min_attention_runs
        )
        if recurring:
            status = "RECURRING_ATTENTION"
        elif attention_runs:
            status = "single/noisy-attention"
        else:
            status = "ok"

        rows.append(
            {
                "metric": metric,
                "identity": dict(identity),
                "sample_count": len(samples),
                "attention_runs": attention_runs,
                "positive_runs": positive_runs,
                "negative_runs": negative_runs,
                "zero_runs": zero_runs,
                "median_delta_ticks": statistics.median(deltas),
                "min_delta_ticks": min(deltas),
                "max_delta_ticks": max(deltas),
                "status": status,
            }
        )

    recurring_rows = [row for row in rows if row["status"] == "RECURRING_ATTENTION"]
    incomplete_rows = [row for row in rows if row["sample_count"] != expected_runs]

    return {
        "schema": "spwkit.profile.release-repeat-summary.v1",
        "run_count": len(reports),
        "expected_runs": expected_runs,
        "minimum_attention_runs": min_attention_runs,
        "comparable_run_count": comparable_runs,
        "baseline_git_shas": sorted(baseline_shas),
        "candidate_git_shas": sorted(candidate_shas),
        "recurring_attention_count": len(recurring_rows),
        "incomplete_row_count": len(incomplete_rows),
        "rows": rows,
    }


def render_markdown(report):
    lines = [
        "# SpWKit repeated hosted release screen",
        "",
        f"- Paired runs: **{report['run_count']}/{report['expected_runs']}**",
        f"- Within-run comparable environments: **{report['comparable_run_count']}/{report['run_count']}**",
        f"- Recurring attention rows: **{report['recurring_attention_count']}**",
        "",
        "A recurring hosted signal means the same baseline-to-candidate row crossed the "
        f"triage threshold in at least {report['minimum_attention_runs']} runs. It is still "
        "screening evidence, not release acceptance.",
        "",
        "| Metric | Identity | Attention runs | Positive/negative/zero | Median delta | Range | Status |",
        "|---|---|---:|---:|---:|---:|---|",
    ]
    for row in report["rows"]:
        identity = render_identity(sorted(row["identity"].items()))
        lines.append(
            f"| `{row['metric']}` | {identity} | {row['attention_runs']}/{row['sample_count']} | "
            f"{row['positive_runs']}/{row['negative_runs']}/{row['zero_runs']} | "
            f"{row['median_delta_ticks']:.1f} | "
            f"{row['min_delta_ticks']:.1f}..{row['max_delta_ticks']:.1f} | {row['status']} |"
        )
    lines += [
        "",
        "Only recurring rows should be promoted to targeted controlled-host investigation. "
        "Single-run attention remains noise until reproduced.",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Aggregate repeated SpWKit release-comparison reports"
    )
    parser.add_argument("root", type=Path)
    parser.add_argument("--expected-runs", type=int, default=3)
    parser.add_argument("--min-attention-runs", type=int, default=2)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-markdown", type=Path)
    parser.add_argument("--fail-on-recurring-attention", action="store_true")
    args = parser.parse_args()

    if args.expected_runs < 1:
        parser.error("expected-runs must be at least 1")
    if not 1 <= args.min_attention_runs <= args.expected_runs:
        parser.error("min-attention-runs must be in 1..expected-runs")

    try:
        report = aggregate(
            load_reports(args.root),
            args.expected_runs,
            args.min_attention_runs,
        )
    except ValueError as exc:
        raise SystemExit(str(exc))

    text = render_markdown(report)
    if args.output_json:
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(report, indent=2) + "\n")
    if args.output_markdown:
        args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
        args.output_markdown.write_text(text)
    print(text)

    if args.fail_on_recurring_attention and report["recurring_attention_count"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
