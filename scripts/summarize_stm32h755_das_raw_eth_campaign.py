#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def parse_board(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if "=" not in raw:
            continue
        key, value = raw.split("=", 1)
        try:
            values[key.strip()] = int(value.strip(), 0)
        except ValueError:
            pass
    return values


def parse_host(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        raw = raw.strip()
        if raw.startswith("{"):
            rows.append(json.loads(raw))
    return rows


def parse_dependencies(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise SystemExit(f"missing STM32 dependency provenance in {path}")
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    for name in ("das_release_tag", "das_commit", "stm32cubeh7_commit",
                 "spwkit_commit", "stm32_clock_hz"):
        if not result.get(name):
            raise SystemExit(f"missing {name} in {path}")
    return result


def mermaid_line(title: str, x_label: str, y_label: str,
                 xs: list[int], ys: list[float]) -> list[str]:
    if not xs:
        return []
    ymax = max(ys) if ys else 1.0
    ceiling = max(1, int(ymax * 1.15 + 0.999))
    x_values = ", ".join(str(x) for x in xs)
    y_values = ", ".join(f"{y:.3f}" for y in ys)
    return [
        "```mermaid",
        "xychart-beta",
        f'    title "{title}"',
        f'    x-axis "{x_label}" [{x_values}]',
        f'    y-axis "{y_label}" 0 --> {ceiling}',
        f"    line [{y_values}]",
        "```",
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--linux-host", choices=("pc", "pi"), default="pc")
    parser.add_argument("--profile", choices=("benchmark", "stress"), default="benchmark")
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--markdown", required=True, type=Path)
    args = parser.parse_args()

    records: list[dict[str, object]] = []
    rtt_by_payload: dict[int, list[tuple[int, float]]] = {}
    bulk_points: list[tuple[int, float, float]] = []
    release_identity: dict[str, str] | None = None

    for run_dir in sorted(args.root.glob("clock-*mhz")):
        board_path = run_dir / "evidence" / "board-evidence.log"
        host_path = run_dir / "evidence" / "host-rtt.jsonl"
        if not board_path.exists() or not host_path.exists():
            raise SystemExit(f"incomplete evidence in {run_dir}")

        board = parse_board(board_path)
        clock_hz = board.get("core_hz", 0)
        if clock_hz == 0:
            raise SystemExit(f"missing core_hz in {board_path}")
        clock_mhz = clock_hz // 1_000_000
        deps = parse_dependencies(run_dir / "evidence" / "dependencies.txt")
        if int(deps["stm32_clock_hz"]) != clock_hz:
            raise SystemExit(f"dependency clock does not match MCU readback: {run_dir}")
        identity = {k: v for k, v in deps.items() if k != "stm32_clock_hz"}
        if release_identity is None:
            release_identity = identity
        elif release_identity != identity:
            raise SystemExit(f"mixed dependency revisions across clock runs: {run_dir}")
        if deps["das_release_tag"] != "v0.1.0" or deps["das_commit"] != (
            "8553d903a615fe99e2a3a434f876b51f9542fd46"
        ):
            raise SystemExit(f"not the qualified DAS v0.1.0 release: {run_dir}")

        for row in parse_host(host_path):
            schema = str(row.get("schema", ""))
            if schema == "spwkit.embedded.raw-ethernet-rtt.v1":
                payload = int(row["payload_bytes"])
                median_ns = float(row["median_ns"])
                rtt_by_payload.setdefault(payload, []).append((clock_mhz, median_ns))
                records.append({
                    "kind": "rtt",
                    "das_release_tag": deps["das_release_tag"],
                    "das_commit": deps["das_commit"],
                    "clock_hz": clock_hz,
                    "payload_bytes": payload,
                    "total_bytes": "",
                    "packets": int(row["iterations"]),
                    "elapsed_ns": "",
                    "median_ns": int(row["median_ns"]),
                    "p95_ns": int(row["p95_ns"]),
                    "p99_ns": int(row["p99_ns"]),
                    "one_way_payload_mbps": "",
                    "aggregate_echo_mbps": "",
                })
            elif schema == "spwkit.embedded.raw-ethernet-bulk.v1":
                one_way = float(row["one_way_payload_mbps"])
                aggregate = float(row["aggregate_echo_mbps"])
                bulk_points.append((clock_mhz, one_way, aggregate))
                records.append({
                    "kind": "bulk",
                    "das_release_tag": deps["das_release_tag"],
                    "das_commit": deps["das_commit"],
                    "clock_hz": clock_hz,
                    "payload_bytes": int(row["payload_bytes"]),
                    "total_bytes": int(row["transferred_bytes"]),
                    "packets": int(row["packets"]),
                    "elapsed_ns": int(row["elapsed_ns"]),
                    "median_ns": "",
                    "p95_ns": "",
                    "p99_ns": "",
                    "one_way_payload_mbps": f"{one_way:.6f}",
                    "aggregate_echo_mbps": f"{aggregate:.6f}",
                })

    fieldnames = [
        "kind", "das_release_tag", "das_commit", "clock_hz",
        "payload_bytes", "total_bytes", "packets",
        "elapsed_ns", "median_ns", "p95_ns", "p99_ns",
        "one_way_payload_mbps", "aggregate_echo_mbps",
    ]
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    with args.csv.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)

    host_name = "Raspberry Pi 5" if args.linux_host == "pi" else "PC"
    lines = [
        "# STM32H755 raw-Ethernet clock-scaling campaign",
        "",
        f"Measured evidence from the physical {host_name}↔NUCLEO-H755ZI-Q campaign.",
        "The CSV beside this report is the canonical aggregate dataset.",
        f"DAS release: **{release_identity['das_release_tag']}** "
        f"(`{release_identity['das_commit']}`)." if release_identity
        else "No completed clock runs.",
        f"STM32CubeH7: `{release_identity['stm32cubeh7_commit']}`."
        if release_identity else "",
        f"SpWKit: `{release_identity['spwkit_commit']}`."
        if release_identity else "",
        f"Campaign profile: **{args.profile}**.",
        ("Benchmark: four-clock RTT and 16 MiB verified echo by default. "
         if args.profile == "benchmark"
         else "Stress: sustained 1 GiB echo at 400 MHz by default."),
        "",
    ]

    if bulk_points:
        bulk_points.sort()
        lines += [
            "## Sustained verified echo throughput",
            "",
            "| CM7 clock | One-way logical payload | Aggregate request+echo |",
            "|---:|---:|---:|",
        ]
        for mhz, one_way, aggregate in bulk_points:
            lines.append(
                f"| {mhz} MHz | {one_way:.3f} Mbit/s | {aggregate:.3f} Mbit/s |"
            )
        lines.append("")
        lines += mermaid_line(
            "STM32 raw Ethernet throughput vs CM7 clock",
            "CM7 clock (MHz)", "One-way payload Mbit/s",
            [p[0] for p in bulk_points],
            [p[1] for p in bulk_points],
        )
        lines.append("")

    lines += ["## RTT scaling", ""]
    for payload in sorted(rtt_by_payload):
        points = sorted(rtt_by_payload[payload])
        lines += mermaid_line(
            f"Median RTT at {payload} B logical payload",
            "CM7 clock (MHz)", "Median RTT (ns)",
            [p[0] for p in points],
            [p[1] for p in points],
        )
        lines.append("")

    lines += [
        "## Interpretation",
        "",
        f"- One-way logical throughput counts verified application payload sent {host_name}→STM32.",
        "- Aggregate echo throughput counts both request and validated echo payload.",
        "- The STM32 profile is intentionally limited to 4096-byte logical packets; larger host payload cases are a separate host/Pi campaign.",
        "- Compare throughput scaling with board-side cycles/byte from each run's performance-summary.md before attributing a plateau to CPU frequency.",
        "",
    ]
    args.markdown.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
