#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import math
from pathlib import Path


def rounded(value):
    value = float(value)
    return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)


def main():
    parser = argparse.ArgumentParser(description="Summarize VSPW-TP UDP diagnostic component costs")
    parser.add_argument("breakdown", type=Path)
    parser.add_argument("calibration", type=Path)
    parser.add_argument("--metadata", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    rows = [
        json.loads(line)
        for line in args.breakdown.read_text().splitlines()
        if line.strip()
    ]
    calibration = json.loads(args.calibration.read_text())
    metadata = json.loads(args.metadata.read_text()) if args.metadata is not None else None
    floor = calibration["statistics"]

    lines = [
        "VSPW-TP UDP diagnostic stage summary",
        "=====================================",
    ]
    if metadata is not None:
        platform = metadata.get("platform", {})
        compiler = metadata.get("compiler", {})
        control = metadata.get("host_control", {})
        counter = metadata.get("counter", {})
        lines.extend([
            f"Commit      : {metadata.get('git_short_sha', 'unknown')} ({metadata.get('git_sha', 'unknown')})",
            f"Host        : {platform.get('cpu_model', 'unknown')}",
            f"OS/kernel   : {platform.get('os', 'unknown')} {platform.get('kernel_release', 'unknown')} / {platform.get('architecture', 'unknown')}",
            f"Compiler    : {compiler.get('family', 'unknown')} / {compiler.get('version', 'unknown')}",
            f"Counter     : {counter.get('kind', 'unknown')} / {counter.get('frequency_hz', 0)} Hz metadata",
            f"CPU control : selected={control.get('selected_cpu')} affinity={control.get('affinity', 'unknown')} nice={control.get('nice_level', 'unknown')} precondition={control.get('precondition_seconds', 0)}s",
            f"Governor    : requested={control.get('requested_governor', 'unknown')} before={control.get('governor_before', 'unknown')} effective={control.get('governor_effective', 'unknown')} driver={control.get('scaling_driver', 'unknown')} status={control.get('governor_change_status', 'unknown')}",
        ])
    lines.extend([
        f"Counter floor: median {rounded(floor['median'])} ticks, p95 {rounded(floor['p95'])}, p99 {rounded(floor['p99'])}",
        "Counter floor is diagnostic only and is not subtracted.",
        "Component rows are attribution microbenchmarks and are not additive to the public API interval.",
        "",
        "stage                         payload  frags   median     mean      p95      p99",
        "-------------------------------------------------------------------------------",
    ])
    for row in rows:
        stats = row["statistics"]
        lines.append(
            f"{row['stage']:<29} {row['payload_bytes']:>6} B  "
            f"{row['fragment_count']:>5}   {rounded(stats['median']):>7}   "
            f"{rounded(stats['mean']):>7}   {rounded(stats['p95']):>7}   "
            f"{rounded(stats['p99']):>7}"
        )

    by_payload = {}
    for row in rows:
        by_payload.setdefault(row["payload_bytes"], {})[row["stage"]] = row

    lines.extend(["", "Derived diagnostic deltas", "--------------------------"])
    for payload in sorted(by_payload):
        stage = by_payload[payload]
        if "sendto" in stage and "poll_sendto" in stage:
            delta = stage["poll_sendto"]["statistics"]["median"] - stage["sendto"]["statistics"]["median"]
            lines.append(f"{payload:>6} B  extra poll(POLLOUT) send path: {rounded(delta)} ticks")
        if "poll_sendto" in stage and "prepare_poll_sendto" in stage:
            delta = stage["prepare_poll_sendto"]["statistics"]["median"] - stage["poll_sendto"]["statistics"]["median"]
            lines.append(f"{payload:>6} B  extra sockaddr/inet_pton send preparation: {rounded(delta)} ticks")
        if "recvfrom" in stage and "poll_recvfrom" in stage:
            delta = stage["poll_recvfrom"]["statistics"]["median"] - stage["recvfrom"]["statistics"]["median"]
            lines.append(f"{payload:>6} B  extra poll(POLLIN) receive path: {rounded(delta)} ticks")
        if "reassembly_reset_1m" in stage:
            lines.append(
                f"{payload:>6} B  1MiB reassembler reset (128KiB bitmap): "
                f"{rounded(stage['reassembly_reset_1m']['statistics']['median'])} ticks"
            )
        if "reassembly_push" in stage and "reassembly_delivery" in stage:
            delta = stage["reassembly_delivery"]["statistics"]["median"] - stage["reassembly_push"]["statistics"]["median"]
            lines.append(f"{payload:>6} B  reset + two delivery copies beyond push: {rounded(delta)} ticks")

    lines.extend([
        "",
        "Interpretation: use these rows to identify candidate work in #179, then validate any production change with the unchanged centralized UDP comparison campaign.",
    ])
    text = "\n".join(lines) + "\n"
    if args.output is not None:
        args.output.write_text(text)
    print(text, end="")


if __name__ == "__main__":
    main()
