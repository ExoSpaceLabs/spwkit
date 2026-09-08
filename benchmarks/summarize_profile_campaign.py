#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import platform
from pathlib import Path


def load_json(path: Path):
    return json.loads(path.read_text())


def load_jsonl(path: Path):
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def fmt(value, digits=3):
    if value is None:
        return "n/a"
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"


def coverage_entries(root: Path):
    is_linux = platform.system() == "Linux"

    def measured(path: str):
        return (root / path).exists() and (root / path).stat().st_size > 0

    entries = [
        {
            "backend": "driver",
            "path": "copied",
            "direction": "tx",
            "status": "measured" if measured("comparison/tx_api_native.jsonl") else "not-implemented-benchmark",
            "reason": "same-process direct/provider comparison" if measured("comparison/tx_api_native.jsonl") else "comparison result missing",
        },
        {"backend": "driver", "path": "copied", "direction": "rx", "status": "not-implemented-benchmark", "reason": "tracked by #166"},
        {"backend": "driver", "path": "zero-copy", "direction": "tx", "status": "not-implemented-benchmark", "reason": "tracked by #138/#159/#160"},
        {"backend": "driver", "path": "zero-copy", "direction": "rx", "status": "not-implemented-benchmark", "reason": "tracked by #138"},
        {"backend": "loopback", "path": "standard", "direction": "tx", "status": "not-implemented-benchmark", "reason": "tracked by #163"},
        {"backend": "loopback", "path": "standard", "direction": "rx", "status": "not-implemented-benchmark", "reason": "tracked by #163"},
        {"backend": "simulator", "path": "standard", "direction": "tx", "status": "not-implemented-benchmark", "reason": "tracked by #163"},
        {"backend": "simulator", "path": "standard", "direction": "rx", "status": "not-implemented-benchmark", "reason": "tracked by #163"},
        {"backend": "udp", "path": "vspw-tp", "direction": "tx", "status": "not-implemented-benchmark", "reason": "tracked by #164"},
        {"backend": "udp", "path": "vspw-tp", "direction": "rx", "status": "not-implemented-benchmark", "reason": "tracked by #164"},
        {
            "backend": "device",
            "path": "vspd",
            "direction": "tx",
            "status": "not-implemented-benchmark" if is_linux else "unsupported-platform",
            "reason": "tracked by #165" if is_linux else "DEVICE/VSPD runtime is Linux-only",
        },
        {
            "backend": "device",
            "path": "vspd",
            "direction": "rx",
            "status": "not-implemented-benchmark" if is_linux else "unsupported-platform",
            "reason": "tracked by #165" if is_linux else "DEVICE/VSPD runtime is Linux-only",
        },
    ]
    counts = {}
    for entry in entries:
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1
    return {
        "schema": "spwkit.profile.coverage.v1",
        "entries": entries,
        "counts": counts,
    }


def render_summary(root: Path, campaign, coverage):
    lines = []
    lines.append("SpWKit Host Profiling Summary")
    lines.append("============================")
    lines.append(f"Result set : {campaign['result_directory_name']}")
    lines.append(f"Type       : {campaign['result_type']}")
    lines.append(f"UTC        : {campaign['timestamp_utc']}")
    lines.append(f"Commit     : {campaign['git_short_sha']} ({campaign['git_sha']})")
    lines.append(f"Build      : {campaign['build_type']} / clean serial cases")
    lines.append("")

    comparison_path = root / "comparison" / "tx_api_native.jsonl"
    comparisons = load_jsonl(comparison_path)
    if comparisons:
        lines.append("DRIVER copied TX: direct/provider vs SpWKit")
        lines.append("--------------------------------------------------------------")
        lines.append("Payload   Native med   SpWKit med   Delta   Delta %   p95 delta")
        for row in comparisons:
            native = row["native_statistics"]
            spwkit = row["spwkit_statistics"]
            delta = row["delta"]
            p95_delta = spwkit["p95"] - native["p95"]
            lines.append(
                f"{row['payload_bytes']:>6} B   "
                f"{fmt(native['median']):>10}   "
                f"{fmt(spwkit['median']):>10}   "
                f"{fmt(delta['median_ticks']):>5}   "
                f"{fmt(delta['median_percent']):>7}   "
                f"{fmt(p95_delta):>9}"
            )
        lines.append("")

    case_files = sorted((root / "cases").glob("*.jsonl"))
    if case_files:
        lines.append("DRIVER copied TX: paired layer ranges")
        lines.append("------------------------------------------------")
        lines.append("Case                     Payload   Median   p95   p99")
        for case_file in case_files:
            for row in load_jsonl(case_file):
                stats = row["statistics"]
                lines.append(
                    f"{case_file.stem:<24} "
                    f"{row['payload_bytes']:>6} B   "
                    f"{fmt(stats['median']):>6}   "
                    f"{fmt(stats['p95']):>4}   "
                    f"{fmt(stats['p99']):>4}"
                )
        lines.append("")

    lines.append("Hosted-backend coverage")
    lines.append("-----------------------")
    measured = coverage["counts"].get("measured", 0)
    total = len(coverage["entries"])
    lines.append(f"Measured: {measured}/{total}")
    for entry in coverage["entries"]:
        label = f"{entry['backend']}/{entry['path']}/{entry['direction']}"
        lines.append(f"  {label:<30} {entry['status']:<26} {entry['reason']}")
    lines.append("")

    if campaign["result_type"] == "github-hosted":
        lines.append("NOTE: GitHub-hosted timings are informational/regression data only.")
    else:
        lines.append("NOTE: Local timing is suitable for controlled-host characterization only when host conditions are recorded and kept stable.")
    lines.append("Standalone counter-floor calibration is diagnostic and is not subtracted from measured intervals.")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description="Summarize a SpWKit profiling result set")
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()

    root = args.result_dir
    campaign = load_json(root / "campaign.json")
    coverage = coverage_entries(root)
    (root / "coverage.json").write_text(json.dumps(coverage, indent=2) + "\n")
    summary = render_summary(root, campaign, coverage)
    (root / "summary.txt").write_text(summary)
    print(summary, end="")


if __name__ == "__main__":
    main()
