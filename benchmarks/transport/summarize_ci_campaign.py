#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import argparse
import csv
import json
import math
import re
import statistics
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

JSON_RE = re.compile(r"\{[^\n]+\}")
SCHEMAS = {
    "udp": "spwkit.transport.udp-throughput.v1",
    "raw": "spwkit.transport.raw-ethernet-throughput.v1",
}


def extract_rows(path: Path, transport: str) -> list[dict[str, object]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    rows: list[dict[str, object]] = []
    for match in JSON_RE.finditer(text):
        raw = match.group(0)
        if SCHEMAS[transport] not in raw:
            continue
        try:
            row = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if row.get("schema") == SCHEMAS[transport]:
            rows.append(row)
    return rows


def file_identity(path: Path, transport: str) -> tuple[str, int, int]:
    stem = path.stem
    prefix = "raw-" if transport == "raw" else ""
    if prefix and stem.startswith(prefix):
        stem = stem[len(prefix):]
    match = re.fullmatch(r"(uni|duplex)-(\d+)-r(\d+)", stem)
    if not match:
        raise ValueError(f"unexpected campaign log name: {path.name}")
    return match.group(1), int(match.group(2)), int(match.group(3))


def sample_from_log(path: Path, transport: str) -> dict[str, object]:
    profile, payload, repeat = file_identity(path, transport)
    rows = extract_rows(path, transport)
    source = [row for row in rows if row.get("role") == "source"]
    sink = [row for row in rows if row.get("role") == "sink"]
    expected_sources = 1 if profile == "uni" else 2
    if len(source) != expected_sources:
        raise ValueError(
            f"{path}: expected {expected_sources} source result(s), got {len(source)}"
        )
    if len(sink) < expected_sources:
        raise ValueError(
            f"{path}: expected at least {expected_sources} sink result(s), got {len(sink)}"
        )

    for row in rows:
        if int(row.get("payload_bytes", -1)) != payload:
            raise ValueError(f"{path}: payload mismatch in result")
        if int(row.get("link_errors", -1)) != 0:
            raise ValueError(f"{path}: non-zero link_errors")

    throughput = sum(float(row["payload_mbps"]) for row in source)
    pps = sum(
        int(row["packets"]) / (int(row["elapsed_ns"]) / 1_000_000_000.0)
        for row in source
    )
    logical_bytes = sum(int(row["total_bytes"]) for row in source)
    logical_packets = sum(int(row["packets"]) for row in source)

    result: dict[str, object] = {
        "transport": transport,
        "profile": profile,
        "payload_bytes": payload,
        "repeat": repeat,
        "source_streams": len(source),
        "logical_bytes": logical_bytes,
        "logical_packets": logical_packets,
        "payload_mbps": throughput,
        "packets_per_second": pps,
        "link_errors": 0,
        "log": str(path),
    }

    if transport == "raw":
        carrier_frames = sum(int(row.get("carrier_tx_frames", 0)) for row in source)
        carrier_bytes = sum(int(row.get("carrier_tx_bytes", 0)) for row in source)
        result["carrier_tx_frames"] = carrier_frames
        result["carrier_tx_bytes"] = carrier_bytes
        result["carrier_frames_per_logical_packet"] = (
            carrier_frames / logical_packets if logical_packets else 0.0
        )
    return result


def stats(values: list[float]) -> dict[str, float]:
    return {
        "median": statistics.median(values),
        "mean": statistics.fmean(values),
        "stdev": statistics.stdev(values) if len(values) > 1 else 0.0,
        "min": min(values),
        "max": max(values),
    }


def aggregate(samples: list[dict[str, object]]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str, int], list[dict[str, object]]] = defaultdict(list)
    for sample in samples:
        grouped[
            (
                str(sample["transport"]),
                str(sample["profile"]),
                int(sample["payload_bytes"]),
            )
        ].append(sample)

    rows: list[dict[str, object]] = []
    for (transport, profile, payload), group in sorted(grouped.items()):
        throughput = [float(row["payload_mbps"]) for row in group]
        pps = [float(row["packets_per_second"]) for row in group]
        t = stats(throughput)
        p = stats(pps)
        out: dict[str, object] = {
            "transport": transport,
            "profile": profile,
            "payload_bytes": payload,
            "repeats": len(group),
            "throughput_mbps_median": t["median"],
            "throughput_mbps_mean": t["mean"],
            "throughput_mbps_stdev": t["stdev"],
            "throughput_mbps_min": t["min"],
            "throughput_mbps_max": t["max"],
            "packets_per_second_median": p["median"],
            "packets_per_second_mean": p["mean"],
        }
        raw_ratio = [
            float(row["carrier_frames_per_logical_packet"])
            for row in group
            if "carrier_frames_per_logical_packet" in row
        ]
        out["carrier_frames_per_logical_packet_median"] = (
            statistics.median(raw_ratio) if raw_ratio else None
        )
        rows.append(out)
    return rows


def mermaid_series(rows: list[dict[str, object]], profile: str) -> list[str]:
    chosen = [row for row in rows if row["profile"] == profile]
    payloads = sorted({int(row["payload_bytes"]) for row in chosen})
    if not payloads:
        return []
    lines = [
        "~~~mermaid",
        "xychart-beta",
        f'    title "CI {profile} transport performance vs logical payload"',
        '    x-axis "Logical payload bytes" [' + ", ".join(map(str, payloads)) + "]",
    ]
    maximum = max(float(row["throughput_mbps_median"]) for row in chosen)
    lines.append(
        f'    y-axis "Median payload Mbit/s" 0 --> {max(1, math.ceil(maximum * 1.15))}'
    )
    for transport in ("udp", "raw"):
        by_payload = {
            int(row["payload_bytes"]): float(row["throughput_mbps_median"])
            for row in chosen
            if row["transport"] == transport
        }
        if len(by_payload) == len(payloads):
            values = ", ".join(f"{by_payload[p]:.3f}" for p in payloads)
            lines.append(f"    line [{values}]")
    lines += ["~~~", "", "Series order: UDP, then RAW Ethernet when both are present."]
    return lines


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    fields = [
        "transport", "profile", "payload_bytes", "repeats",
        "throughput_mbps_median", "throughput_mbps_mean",
        "throughput_mbps_stdev", "throughput_mbps_min",
        "throughput_mbps_max", "packets_per_second_median",
        "packets_per_second_mean", "carrier_frames_per_logical_packet_median",
    ]
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--udp-dir", type=Path, required=True)
    parser.add_argument("--raw-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--commit", default="unknown")
    parser.add_argument("--runner", default="unknown")
    args = parser.parse_args()

    samples: list[dict[str, object]] = []
    for transport, root in (("udp", args.udp_dir), ("raw", args.raw_dir)):
        for path in sorted(root.glob("*.log")):
            samples.append(sample_from_log(path, transport))

    if not samples:
        raise SystemExit("no transport campaign samples found")

    rows = aggregate(samples)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "samples.json").write_text(
        json.dumps(samples, indent=2) + "\n", encoding="utf-8"
    )
    (args.output_dir / "summary.json").write_text(
        json.dumps(rows, indent=2) + "\n", encoding="utf-8"
    )
    write_csv(args.output_dir / "results.csv", rows)

    now = datetime.now(timezone.utc).isoformat()
    lines = [
        "# SpWKit transport performance evidence",
        "",
        "> **CI interpretation boundary:** these absolute throughput values are specific",
        "> to the ephemeral GitHub-hosted runner used for this workflow. They are not",
        "> hardware qualification numbers and must not be compared across unrelated",
        "> runs as if the host were controlled. Within one workflow, however, the",
        "> repeated payload sweep is useful for understanding SpWKit/VSPW transport",
        "> behavior, fragmentation cost, packet-rate pressure, UDP-vs-RAW relative",
        "> behavior, and gross regressions.",
        "",
        "## Campaign identity",
        "",
        f"- Generated: {now}",
        f"- SpWKit commit: {args.commit}",
        f"- Runner: {args.runner}",
        "- Topology: two Docker containers on one Linux bridge/veth network",
        "- UDP path: SpWKit -> VSPW-TP -> UDP/IP -> container veth -> Linux bridge",
        "- RAW path: SpWKit -> VSPW-TP -> AF_PACKET RAW Ethernet -> veth -> Linux bridge",
        "- Payload validation: deterministic byte-for-byte verification at receiver",
        "",
        "## What this CI measures",
        "",
        "The sweep intentionally varies the **logical SpWKit packet size**, not just",
        "the carrier frame size. Small payloads emphasize per-packet API/session/",
        "syscall overhead and packet rate. Larger payloads exercise VSPW-TP",
        "fragmentation/reassembly and amortize fixed costs. RAW carrier-frame counts",
        "show how many Layer-2 frames are required per logical SpWKit packet.",
        "",
        "Each table cell below is aggregated from repeated complete transfers in the",
        "same workflow run. Median is the primary comparison value; mean, standard",
        "deviation and range expose scheduler/host noise instead of hiding it.",
        "",
        "## CI statistics",
        "",
        "| Transport | Mode | Payload | Repeats | Median Mbit/s | Mean | Stddev | Min | Max | Median pkt/s | RAW frames/logical packet |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        ratio = row["carrier_frames_per_logical_packet_median"]
        ratio_text = "-" if ratio is None else f"{float(ratio):.2f}"
        lines.append(
            f"| {str(row['transport']).upper()} | {row['profile']} "
            f"| {int(row['payload_bytes'])} B | {int(row['repeats'])} "
            f"| {float(row['throughput_mbps_median']):.2f} "
            f"| {float(row['throughput_mbps_mean']):.2f} "
            f"| {float(row['throughput_mbps_stdev']):.2f} "
            f"| {float(row['throughput_mbps_min']):.2f} "
            f"| {float(row['throughput_mbps_max']):.2f} "
            f"| {float(row['packets_per_second_median']):.0f} "
            f"| {ratio_text} |"
        )

    lines += ["", "## Payload-size trend", ""]
    lines += mermaid_series(rows, "uni")
    lines += [
        "",
        "## Physical-device result registry",
        "",
        "These rows are intentionally left pending until measured on controlled",
        "hardware. CI values must **not** be copied into this table.",
        "",
        "| Topology | Transport | Direction/mode | Required sweep | Status | Evidence |",
        "|---|---|---|---|---|---|",
        "| Native PC <-> Raspberry Pi 5 | UDP | both directions + duplex | logical payload sweep, sustained transfer | **PENDING** | - |",
        "| Native PC <-> Raspberry Pi 5 | RAW Ethernet | both directions + duplex | logical payload sweep, sustained transfer | **PENDING** | - |",
        "| Native PC <-> STM32H755 | RAW Ethernet | request/echo baseline | 64/200/300/400 MHz + payload sweep + 1 GiB | **PENDING** | - |",
        "| Docker <-> Raspberry Pi 5 | UDP | deployment comparison | selected baseline payloads | OPTIONAL | - |",
        "| Docker <-> Raspberry Pi 5 | RAW Ethernet | deployment comparison | selected baseline payloads | OPTIONAL | - |",
        "| Docker <-> STM32H755 | RAW Ethernet | deployment comparison | 400 MHz first | OPTIONAL | - |",
        "",
        "For physical runs, retain machine-readable JSON/CSV, exact commit IDs, CPU/",
        "clock state, interface/link settings, toolchain/build type, and environment",
        "metadata next to the rendered report.",
        "",
        "## Canonical evidence",
        "",
        "- samples.json: every individual repeated run extracted from transport logs.",
        "- summary.json: aggregated statistics by transport/mode/payload.",
        "- results.csv: tabular aggregate dataset used by this report.",
        "- original UDP/RAW Docker logs are retained alongside this report by CI.",
        "",
    ]
    (args.output_dir / "README.md").write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
