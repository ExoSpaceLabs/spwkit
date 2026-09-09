#!/usr/bin/env python3
import argparse
import json
import math
import re
import struct
from pathlib import Path

META_RE = re.compile(r"PROFILE_META (.+)$")
PROFILE_MAGIC = 0x53575050
PROFILE_VERSION = 1
PROFILE_COMPLETE_PHASE = 0x0000700D


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


def parse_log(path):
    meta = None
    pass_seen = False
    raw_seen = False
    for line in path.read_text(errors="replace").splitlines():
        if line.strip() == "RESULT: PASS":
            pass_seen = True
        if line.startswith("PROFILE_RAW "):
            raw_seen = True
        match = META_RE.search(line)
        if match:
            meta = {}
            for token in match.group(1).split():
                key, value = token.split("=", 1)
                meta[key] = parse_int(value)
    if meta is None or not pass_seen:
        raise SystemExit("profile log does not contain a passing PROFILE_META/RESULT record")
    if not raw_seen:
        raise SystemExit("profile log does not confirm a bulk raw-memory dump")
    return meta


class RawReader:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def u32(self):
        if self.offset + 4 > len(self.data):
            raise ValueError("raw profile evidence is truncated")
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def u32_array(self, count):
        return [self.u32() for _ in range(count)]


def parse_row(reader, iterations):
    payload = reader.u32()
    samples = reader.u32_array(iterations)
    return {
        "payload_bytes": payload,
        "samples": samples,
        "statistics": statistics(samples),
    }


def parse_raw(path):
    reader = RawReader(path.read_bytes())
    header_names = (
        "magic", "version", "phase", "result", "case_id", "start_id", "end_id",
        "core_hz", "warmup", "iterations", "rows",
    )
    header = {name: reader.u32() for name in header_names}
    iterations = header["iterations"]
    rows = header["rows"]

    if not 1 <= iterations <= 256:
        raise SystemExit(f"invalid measured iteration count in raw evidence: {iterations}")
    if rows != 5:
        raise SystemExit(f"unexpected raw payload row count: {rows}")

    floor_samples = reader.u32_array(iterations)
    payload_rows = [parse_row(reader, iterations) for _ in range(rows)]
    cache_valid = reader.u32()
    clean_rows = [parse_row(reader, iterations) for _ in range(rows)]
    invalidate_rows = [parse_row(reader, iterations) for _ in range(rows)]

    if reader.offset != len(reader.data):
        raise SystemExit(
            f"raw evidence size mismatch: parsed {reader.offset} bytes, file has {len(reader.data)}"
        )

    return header, floor_samples, payload_rows, cache_valid, clean_rows, invalidate_rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--start", required=True)
    parser.add_argument("--end", required=True)
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--cube-sha", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    meta = parse_log(args.log)
    header, floor_samples, payload_rows, cache_valid, clean_rows, invalidate_rows = parse_raw(args.raw)

    for key in (
        "magic", "version", "phase", "result", "case_id", "start_id", "end_id",
        "core_hz", "warmup", "iterations", "rows",
    ):
        if header[key] != meta[key]:
            raise SystemExit(
                f"raw/log metadata mismatch for {key}: raw={header[key]} log={meta[key]}"
            )

    if header["magic"] != PROFILE_MAGIC or header["version"] != PROFILE_VERSION:
        raise SystemExit("raw evidence has an invalid magic/version")
    if header["phase"] != PROFILE_COMPLETE_PHASE or header["result"] != 0:
        raise SystemExit(
            f"raw evidence is not complete: phase=0x{header['phase']:08x} result=0x{header['result']:08x}"
        )

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
        "probe_start_id": header["start_id"],
        "probe_end_id": header["end_id"],
        "payload_capacity_bytes": 256,
        "counter": {
            "kind": "cortex-m-dwt-cyccnt",
            "width_bits": 32,
            "frequency_hz": header["core_hz"],
        },
        "warmup_iterations": header["warmup"],
        "iterations": header["iterations"],
        "counter_floor": {
            "method": "back_to_back_dwt_reads",
            "samples": floor_samples,
            "statistics": statistics(floor_samples),
            "subtracted": False,
        },
        "rows": payload_rows,
        "scope_note": "STM32 DMA2 memory-to-memory provider timing; not SpaceWire PHY/link timing",
    }

    if cache_valid == 1:
        document["cache_maintenance"] = {
            "clean": clean_rows,
            "invalidate": invalidate_rows,
            "note": "D-cache primitives isolated around the same CMSIS clean/invalidate helpers used by the provider",
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")


if __name__ == "__main__":
    main()
