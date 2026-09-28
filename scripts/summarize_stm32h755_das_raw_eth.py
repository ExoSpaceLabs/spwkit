#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_board(path: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if "=" not in raw:
            continue
        key, value = raw.split("=", 1)
        key = key.strip()
        value = value.strip()
        if not key or not value:
            continue
        try:
            values[key] = int(value, 0)
        except ValueError:
            pass
    return values


def parse_host(path: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        raw = raw.strip()
        if not raw.startswith("{"):
            continue
        record = json.loads(raw)
        if record.get("schema") == "spwkit.embedded.raw-ethernet-rtt.v1":
            rows.append(record)
    return rows


def mean_cycles(board: dict[str, int], prefix: str) -> float:
    return float(board[f"{prefix}_total_cycles"]) / float(board[f"{prefix}_count"])


def cycles_to_ns(cycles: float, core_hz: int) -> float:
    return cycles * 1_000_000_000.0 / float(core_hz)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--board", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    host = parse_host(args.host)
    board = parse_board(args.board)
    required = [
        "core_hz",
        "app_send_count", "app_send_min_cycles", "app_send_max_cycles",
        "app_send_total_cycles",
        "app_receive_count", "app_receive_min_cycles", "app_receive_max_cycles",
        "app_receive_total_cycles",
        "das_tx_count", "das_tx_min_cycles", "das_tx_max_cycles",
        "das_tx_total_cycles",
        "das_rx_poll_count", "das_rx_poll_min_cycles", "das_rx_poll_max_cycles",
        "das_rx_poll_total_cycles",
        "das_rx_success_count", "das_rx_success_min_cycles",
        "das_rx_success_max_cycles", "das_rx_success_total_cycles",
        "das_rx_empty_polls",
    ]
    missing = [key for key in required if key not in board]
    if missing:
        raise SystemExit("missing board evidence: " + ", ".join(missing))
    if not host:
        raise SystemExit("no host RTT evidence found")

    core_hz = board["core_hz"]
    lines = [
        "# STM32H755 DAS raw-Ethernet performance evidence",
        "",
        f"- Core clock: {core_hz} Hz",
        f"- Link: {board.get('link_speed_mbps', 0)} Mbps",
        f"- Echoed logical packets: {board.get('echoed_packets', 0)}",
        f"- Raw RX empty polls: {board['das_rx_empty_polls']}",
        "- Ethernet mode: polling-only; IRQ-to-worker latency is not applicable.",
        "",
        "## Physical PC↔STM32 RTT",
        "",
        "| Payload | Median | p95 | p99 | Min | Max |",
        "|---:|---:|---:|---:|---:|---:|",
    ]
    for row in host:
        lines.append(
            f"| {int(row['payload_bytes'])} B "
            f"| {int(row['median_ns'])} ns "
            f"| {int(row['p95_ns'])} ns "
            f"| {int(row['p99_ns'])} ns "
            f"| {int(row['min_ns'])} ns "
            f"| {int(row['max_ns'])} ns |"
        )

    lines += [
        "",
        "## Board-side cycle decomposition",
        "",
        "| Boundary | Samples | Mean cycles | Mean ns | Min cycles | Max cycles |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    labels = [
        ("app_send", "spw_port_send() logical echo"),
        ("app_receive", "spw_port_receive() successful call, polling-inclusive"),
        ("das_tx", "das_eth_send() carrier call"),
        ("das_rx_success", "successful das_eth_receive() carrier call"),
        ("das_rx_poll", "all das_eth_receive() polls"),
    ]
    for prefix, label in labels:
        mean = mean_cycles(board, prefix)
        lines.append(
            f"| {label} "
            f"| {board[prefix + '_count']} "
            f"| {mean:.2f} "
            f"| {cycles_to_ns(mean, core_hz):.2f} "
            f"| {board[prefix + '_min_cycles']} "
            f"| {board[prefix + '_max_cycles']} |"
        )

    lines += [
        "",
        "### Interpretation boundary",
        "",
        "- das_eth_send() includes DAS copy/cache work, descriptor submission, and polling until the TX descriptor returns to software.",
        "- Successful das_eth_receive() covers completed-descriptor handling plus DAS cache invalidation/copy.",
        "- spw_port_receive() is intentionally labeled polling-inclusive because the current DAS Ethernet path has no IRQ-driven wakeup.",
        "- These figures are physical Ethernet/MAC/DMA evidence for this STM32H755+DAS integration, not physical SpaceWire timing.",
        "",
    ]
    args.output.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
