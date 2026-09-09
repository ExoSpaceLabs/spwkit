#!/usr/bin/env python3
import argparse
import json
import math
import re
from pathlib import Path

META_RE = re.compile(r"PROFILE_META (.+)$")
SAMPLE_RE = re.compile(r"PROFILE_SAMPLE row=(\d+) payload=(\d+) index=(\d+) cycles=(\d+)")
FLOOR_RE = re.compile(r"PROFILE_FLOOR index=(\d+) cycles=(\d+)")
CLEAN_RE = re.compile(r"PROFILE_CACHE_CLEAN row=(\d+) payload=(\d+) index=(\d+) cycles=(\d+)")
INVALIDATE_RE = re.compile(r"PROFILE_CACHE_INVALIDATE row=(\d+) payload=(\d+) index=(\d+) cycles=(\d+)")


def parse_int(value):
    return int(value, 0)


def nearest_rank(values, percentile):
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile * len(ordered)))
    return ordered[rank - 1]


def statistics(values):
    if not values:
        raise ValueError("empty sample set")
    ordered = sorted(values)
    n = len(ordered)
    if n % 2:
        median = float(ordered[n // 2])
    else:
        median = (ordered[n // 2 - 1] + ordered[n // 2]) / 2.0
    mean = sum(ordered) / n
    variance = sum((value - mean) ** 2 for value in ordered) / n
    return {
        "min": ordered[0],
        "median": median,
        "mean": mean,
        "p95": nearest_rank(ordered, 0.95),
        "p99": nearest_rank(ordered, 0.99),
        "max": ordered[-1],
        "stddev": math.sqrt(variance),
    }


def add_sample(rows, row, payload, index, cycles):
    entry = rows.setdefault(row, {"payload_bytes": payload, "samples": {}})
    if entry["payload_bytes"] != payload:
        raise ValueError(f"payload changed within row {row}")
    entry["samples"][index] = cycles


def finalize_rows(rows, iterations):
    result = []
    for row_index in sorted(rows):
        entry = rows[row_index]
        samples = [entry["samples"].get(i) for i in range(iterations)]
        if any(value is None for value in samples):
            raise ValueError(f"row {row_index} does not contain {iterations} samples")
        result.append({
            "payload_bytes": entry["payload_bytes"],
            "samples": samples,
            "statistics": statistics(samples),
        })
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--case", required=True)
    parser.add_argument("--start", required=True)
    parser.add_argument("--end", required=True)
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--cube-sha", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    meta = None
    floor = {}
    rows = {}
    clean = {}
    invalidate = {}
    pass_seen = False

    for line in args.log.read_text(errors="replace").splitlines():
        if line.strip() == "RESULT: PASS":
            pass_seen = True
        match = META_RE.search(line)
        if match:
            meta = {}
            for token in match.group(1).split():
                key, value = token.split("=", 1)
                meta[key] = parse_int(value)
            continue
        match = FLOOR_RE.search(line)
        if match:
            floor[int(match.group(1))] = int(match.group(2))
            continue
        match = SAMPLE_RE.search(line)
        if match:
            add_sample(rows, *(int(group) for group in match.groups()))
            continue
        match = CLEAN_RE.search(line)
        if match:
            add_sample(clean, *(int(group) for group in match.groups()))
            continue
        match = INVALIDATE_RE.search(line)
        if match:
            add_sample(invalidate, *(int(group) for group in match.groups()))

    if meta is None or not pass_seen:
        raise SystemExit("profile log does not contain a passing PROFILE_META/RESULT record")
    iterations = meta["iterations"]
    floor_samples = [floor.get(i) for i in range(iterations)]
    if any(value is None for value in floor_samples):
        raise SystemExit("counter-floor sample set is incomplete")

    payload_rows = finalize_rows(rows, iterations)
    if len(payload_rows) != meta["rows"]:
        raise SystemExit("profile payload row count is incomplete")

    document = {
        "schema": "spwkit.profile.stm32h755.v1",
        "measurement_domain": "software",
        "unit": "core_cycles",
        "result_type": "physical-board",
        "board": "NUCLEO-H755ZI-Q",
        "mcu": "STM32H755ZI",
        "core": "Cortex-M7",
        "git_sha": args.git_sha,
        "stm32cubeh7_sha": args.cube_sha,
        "compiler": args.compiler,
        "build_type": "Release",
        "case": args.case,
        "probe_start": args.start,
        "probe_end": args.end,
        "probe_start_id": meta["start_id"],
        "probe_end_id": meta["end_id"],
        "payload_capacity_bytes": 256,
        "counter": {
            "kind": "cortex-m-dwt-cyccnt",
            "width_bits": 32,
            "frequency_hz": meta["core_hz"],
        },
        "warmup_iterations": meta["warmup"],
        "iterations": iterations,
        "counter_floor": {
            "method": "back_to_back_dwt_reads",
            "samples": floor_samples,
            "statistics": statistics(floor_samples),
            "subtracted": False,
        },
        "rows": payload_rows,
        "scope_note": "STM32 DMA2 memory-to-memory provider timing; not SpaceWire PHY/link timing",
    }

    if meta.get("cache_valid") == 1:
        document["cache_maintenance"] = {
            "clean": finalize_rows(clean, iterations),
            "invalidate": finalize_rows(invalidate, iterations),
            "note": "D-cache primitives isolated around the same CMSIS clean/invalidate helpers used by the provider",
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")


if __name__ == "__main__":
    main()
