#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path


def rounded(value):
    value = float(value)
    return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()

    campaign = json.loads((args.result_dir / "campaign.json").read_text())
    files = [args.result_dir / "cases" / f"{name}.json" for name in campaign["cases"]]
    docs = [json.loads(path.read_text()) for path in files]

    lines = [
        "SpWKit STM32H755 Physical Profiling Summary",
        "===========================================",
        f"Result set : {campaign['result_directory_name']}",
        f"Commit     : {campaign['git_sha']}",
        f"Board      : NUCLEO-H755ZI-Q / STM32H755ZI Cortex-M7",
        f"Build      : Release / clean serial configuration builds",
        f"Counter    : DWT CYCCNT / {campaign['core_hz']} Hz",
        f"Payloads   : 0 1 8 64 256 bytes",
        f"Iterations : {campaign['iterations']} measured, {campaign['warmup']} warmup",
        "Display    : cycle values rounded to nearest integer; JSON retains raw samples/full precision",
        "",
    ]

    for doc in docs:
        floor = doc["counter_floor"]["statistics"]
        lines.append(f"{doc['case']}: {doc['probe_start']} -> {doc['probe_end']}")
        lines.append(f"Counter floor median: {rounded(floor['median'])} cycles (diagnostic, not subtracted)")
        lines.append("Payload   Median   Mean   p95   p99")
        for row in doc["rows"]:
            stats = row["statistics"]
            lines.append(
                f"{row['payload_bytes']:>6} B   {rounded(stats['median']):>6}   "
                f"{rounded(stats['mean']):>4}   {stats['p95']:>3}   {stats['p99']:>3}"
            )
        lines.append("")

    cache_doc = next((doc for doc in docs if "cache_maintenance" in doc), None)
    if cache_doc is not None:
        lines.append("Isolated Cortex-M7 D-cache maintenance")
        lines.append("--------------------------------------")
        lines.append("Payload   Clean med   Invalidate med")
        clean = cache_doc["cache_maintenance"]["clean"]
        invalidate = cache_doc["cache_maintenance"]["invalidate"]
        for clean_row, invalidate_row in zip(clean, invalidate):
            lines.append(
                f"{clean_row['payload_bytes']:>6} B   "
                f"{rounded(clean_row['statistics']['median']):>9}   "
                f"{rounded(invalidate_row['statistics']['median']):>14}"
            )
        lines.append("")

    lines.extend([
        "NOTE: These are MCU software/provider/DMA/cache measurements.",
        "They do not include a SpaceWire controller, codec, PHY, cable, or link serialization time.",
        "Standalone DWT floor calibration is diagnostic and is never subtracted from measured intervals.",
        "",
    ])
    summary = "\n".join(lines)
    (args.result_dir / "summary.txt").write_text(summary)
    print(summary, end="")


if __name__ == "__main__":
    main()
