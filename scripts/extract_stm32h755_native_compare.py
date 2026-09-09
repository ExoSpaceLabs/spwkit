#!/usr/bin/env python3
import argparse
import json
import math
import re
import struct
from pathlib import Path

MAGIC = 0x53575050
VERSION = 2
ROWS = 5

META_RE = re.compile(
    r"PROFILE_META magic=0x([0-9a-fA-F]+) version=(\d+) phase=0x([0-9a-fA-F]+) "
    r"result=0x([0-9a-fA-F]+) case_id=(\d+) start_id=(\d+) end_id=(\d+) "
    r"core_hz=(\d+) warmup=(\d+) iterations=(\d+) rows=(\d+)"
)


def percentile_index(count: int, percentile: int) -> int:
    rank = (percentile * count + 99) // 100
    rank = min(max(rank, 1), count)
    return rank - 1


def statistics(samples):
    ordered = sorted(samples)
    count = len(ordered)
    mean = sum(ordered) / count
    if count % 2:
        median = float(ordered[count // 2])
    else:
        median = (ordered[count // 2 - 1] + ordered[count // 2]) / 2.0
    variance = sum((value - mean) ** 2 for value in ordered) / count
    return {
        "minimum": ordered[0],
        "median": median,
        "mean": mean,
        "p95": ordered[percentile_index(count, 95)],
        "p99": ordered[percentile_index(count, 99)],
        "maximum": ordered[-1],
        "standard_deviation": math.sqrt(variance),
    }


def parse_meta(text: str):
    match = META_RE.search(text)
    if not match:
        raise ValueError("PROFILE_META line not found in GDB log")
    keys = [
        "magic", "version", "phase", "result", "case_id", "start_probe_id",
        "end_probe_id", "core_hz", "warmup_iterations", "measured_iterations",
        "row_count",
    ]
    values = [
        int(match.group(1), 16), int(match.group(2)), int(match.group(3), 16),
        int(match.group(4), 16), int(match.group(5)), int(match.group(6)),
        int(match.group(7)), int(match.group(8)), int(match.group(9)),
        int(match.group(10)), int(match.group(11)),
    ]
    return dict(zip(keys, values))


def read_u32(raw: bytes, offset: int):
    if offset + 4 > len(raw):
        raise ValueError("raw evidence ended unexpectedly")
    return struct.unpack_from("<I", raw, offset)[0], offset + 4


def parse_raw(raw: bytes):
    offset = 0
    names = [
        "magic", "version", "phase", "result", "case_id", "start_probe_id",
        "end_probe_id", "core_hz", "warmup_iterations", "measured_iterations",
        "row_count",
    ]
    header = {}
    for name in names:
        header[name], offset = read_u32(raw, offset)

    if header["magic"] != MAGIC:
        raise ValueError(f"unexpected raw magic 0x{header['magic']:08x}")
    if header["version"] != VERSION:
        raise ValueError(f"unexpected raw version {header['version']}")
    if header["row_count"] != ROWS:
        raise ValueError(f"unexpected raw row count {header['row_count']}")
    iterations = header["measured_iterations"]
    if iterations < 1 or iterations > 256:
        raise ValueError(f"invalid measured iteration count {iterations}")

    floor = []
    for _ in range(iterations):
        value, offset = read_u32(raw, offset)
        floor.append(value)

    rows = []
    for _ in range(ROWS):
        payload, offset = read_u32(raw, offset)
        native = []
        spwkit = []
        for _ in range(iterations):
            value, offset = read_u32(raw, offset)
            native.append(value)
        for _ in range(iterations):
            value, offset = read_u32(raw, offset)
            spwkit.append(value)
        delta = [int(spwkit[i]) - int(native[i]) for i in range(iterations)]
        native_stats = statistics(native)
        spwkit_stats = statistics(spwkit)
        delta_stats = statistics(delta)
        native_median = native_stats["median"]
        percent = None if native_median == 0 else 100.0 * delta_stats["median"] / native_median
        rows.append({
            "payload_bytes": payload,
            "native_samples_cycles": native,
            "spwkit_samples_cycles": spwkit,
            "paired_delta_samples_cycles": delta,
            "native_statistics_cycles": native_stats,
            "spwkit_statistics_cycles": spwkit_stats,
            "paired_delta_statistics_cycles": delta_stats,
            "paired_median_delta_percent_of_native_median": percent,
            "aggregate_statistic_deltas_cycles": {
                key: spwkit_stats[key] - native_stats[key]
                for key in ("median", "mean", "p95", "p99")
            },
        })

    if offset != len(raw):
        raise ValueError(f"raw evidence size mismatch: parsed {offset}, file has {len(raw)} bytes")
    return header, floor, rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("gdb_log")
    parser.add_argument("--raw", required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--start", required=True)
    parser.add_argument("--end", required=True)
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--cube-sha", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    log_text = Path(args.gdb_log).read_text(errors="replace")
    if "RESULT: PASS" not in log_text:
        raise SystemExit("GDB log does not contain RESULT: PASS")
    log_meta = parse_meta(log_text)
    raw_meta, floor, rows = parse_raw(Path(args.raw).read_bytes())
    if log_meta != raw_meta:
        raise SystemExit(f"GDB/raw metadata mismatch: log={log_meta} raw={raw_meta}")

    result = {
        "schema": "spwkit.profile.stm32h755-native-comparison.v1",
        "result_type": "physical-board-native-differential",
        "case": args.case,
        "git_sha": args.git_sha,
        "stm32cubeh7_sha": args.cube_sha,
        "compiler": args.compiler,
        "board": "NUCLEO-H755ZI-Q",
        "mcu": "STM32H755ZI",
        "core": "Cortex-M7",
        "core_hz": raw_meta["core_hz"],
        "counter": "DWT_CYCCNT",
        "build_type": "Release",
        "start_probe": args.start,
        "end_probe": args.end,
        "case_id": raw_meta["case_id"],
        "warmup": raw_meta["warmup_iterations"],
        "iterations": raw_meta["measured_iterations"],
        "counter_floor_samples_cycles": floor,
        "counter_floor_statistics_cycles": statistics(floor),
        "counter_floor_subtracted": False,
        "execution_order": "alternating native-first / SpWKit-first per measured iteration",
        "scope": "direct STM32 provider callbacks versus SpWKit public API using the same DMA2 memory-to-memory provider; not SpaceWire PHY/link timing",
        "rows": rows,
    }
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
