#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reject physical HIL results missing the VSPW protocol attribution evidence."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(host_path: Path, board_path: Path, bulk_bytes: int) -> str:
    host = host_path.read_text(encoding="utf-8")
    board = board_path.read_text(encoding="utf-8")
    if "HOST_RESULT: PASS" not in host or "RESULT: PASS" not in board:
        raise ValueError("missing host/board PASS markers")

    rows = [json.loads(line.strip()) for line in host.splitlines()
            if line.lstrip().startswith("{")]
    full = [r for r in rows
            if r.get("schema") == "spwkit.embedded.vspw-diagnostics.v1"]
    if len(full) != 1:
        raise ValueError(f"expected one host VSPW snapshot, found {len(full)}")
    host_diag = full[0]
    for key in ("tx_data_frames", "rx_data_frames",
                "ack_matched", "retry_attempts", "ack_timeouts"):
        value = host_diag.get(key)
        if not isinstance(value, int) or value < 0:
            raise ValueError(f"invalid host VSPW counter: {key}")
    if host_diag["tx_data_frames"] < 1:
        raise ValueError("host VSPW sent no DATA frames")

    board_diag: dict[str, int] = {}
    for line in board.splitlines():
        if line.startswith("vspw_") and "=" in line:
            key, value = line.split("=", 1)
            try:
                board_diag[key[5:]] = int(value, 0)
            except ValueError as exc:
                raise ValueError(f"invalid board counter: {key}") from exc
    for key in ("tx_data_frames", "rx_data_frames",
                "ack_matched", "retry_attempts", "ack_timeouts"):
        if key not in board_diag or board_diag[key] < 0:
            raise ValueError(f"missing/invalid board VSPW counter: {key}")
    if board_diag["tx_data_frames"] < 1 or board_diag["rx_data_frames"] < 1:
        raise ValueError("board has no VSPW DATA frames in its evidence")

    bulks = [r for r in rows
             if r.get("schema") == "spwkit.embedded.raw-ethernet-bulk.v1"]
    if bulk_bytes:
        if len(bulks) != 1:
            raise ValueError(f"expected one measured bulk record, found {len(bulks)}")
        record = bulks[0]
        if record.get("transferred_bytes") != bulk_bytes:
            raise ValueError("incomplete verified bulk transfer")
        if not isinstance(record.get("packets"), int) or record["packets"] < 1:
            raise ValueError("invalid bulk packet count")
        keys = (
            "vspw_bulk_tx_data_frames", "vspw_bulk_rx_data_frames",
            "vspw_bulk_tx_ack_frames", "vspw_bulk_rx_ack_frames",
            "vspw_bulk_retry_attempts", "vspw_bulk_retransmitted_frames",
            "vspw_bulk_ack_timeouts", "vspw_bulk_ack_matched",
            "vspw_bulk_tx_slot_wait_calls", "vspw_bulk_tx_slot_wait_us",
            "vspw_bulk_provider_receive_us",
        )
        for key in keys:
            value = record.get(key)
            if not isinstance(value, int) or value < 0:
                raise ValueError(f"missing/invalid bulk VSPW delta: {key}")
            total_key = key[len("vspw_bulk_"):]
            if total_key in host_diag and value > host_diag[total_key]:
                raise ValueError(f"bulk delta exceeds cumulative counter: {key}")
        if record["vspw_bulk_tx_data_frames"] < record["packets"]:
            raise ValueError("bulk VSPW DATA frames less than logical packets")
    elif bulks:
        raise ValueError("unexpected bulk record with zero requested bulk bytes")

    return (f"VSPW diagnostics validated: host DATA={host_diag['tx_data_frames']} "
            f"board DATA={board_diag['tx_data_frames']} "
            f"bulk records={len(bulks)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--board", required=True, type=Path)
    parser.add_argument("--bulk-bytes", required=True, type=int)
    args = parser.parse_args()
    try:
        if args.bulk_bytes < 0:
            raise ValueError("negative bulk bytes")
        print(validate(args.host, args.board, args.bulk_bytes))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.exit(1, f"VSPW_DIAGNOSTICS_FAIL: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
