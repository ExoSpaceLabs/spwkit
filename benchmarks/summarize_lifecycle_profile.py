#!/usr/bin/env python3
import json
import sys
from pathlib import Path


def rounded(value):
    return int(value + 0.5) if value >= 0 else int(value - 0.5)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: summarize_lifecycle_profile.py lifecycle.jsonl calibration.json", file=sys.stderr)
        return 2

    lifecycle_path = Path(sys.argv[1])
    calibration_path = Path(sys.argv[2])
    rows = [json.loads(line) for line in lifecycle_path.read_text().splitlines() if line.strip()]
    calibration = json.loads(calibration_path.read_text())

    if len(rows) != 7:
        raise SystemExit(f"expected 7 lifecycle rows, found {len(rows)}")
    for row in rows:
        if row.get("schema") != "spwkit.profile.lifecycle.v1":
            raise SystemExit(f"unexpected lifecycle schema: {row.get('schema')}")
    if calibration.get("schema") != "spwkit.profile.calibration.v1":
        raise SystemExit("unexpected calibration schema")

    floor = calibration["statistics"]
    print("\nLifecycle profiling summary")
    print(f"Counter floor: median {rounded(floor['median'])} ticks, p95 {floor['p95']}, p99 {floor['p99']}")
    print("Counter floor is diagnostic only and is not subtracted.\n")
    print(f"{'operation':<20} {'allocation':<13} {'median':>8} {'mean':>8} {'p95':>8} {'p99':>8}")
    print("-" * 70)
    for row in rows:
        stats = row["statistics"]
        print(
            f"{row['operation']:<20} {row['allocation']:<13} "
            f"{rounded(stats['median']):>8} {rounded(stats['mean']):>8} "
            f"{stats['p95']:>8} {stats['p99']:>8}"
        )
    print("\nThese are lifecycle/startup costs, not steady-state TX/RX latency metrics.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
