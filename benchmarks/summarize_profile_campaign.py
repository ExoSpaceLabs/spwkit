#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import math
import os
import platform
import shlex
import subprocess
from pathlib import Path


def load_json(path: Path):
    return json.loads(path.read_text())


def load_jsonl(path: Path):
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def rounded_tick(value):
    """Round a counter-tick statistic to the nearest representable tick."""
    numeric = float(value)
    if numeric >= 0:
        return math.floor(numeric + 0.5)
    return math.ceil(numeric - 0.5)


def fmt_ticks(value):
    if value is None:
        return "n/a"
    return str(rounded_tick(value))


def fmt_percent(value):
    if value is None:
        return "n/a"
    return f"{float(value):.1f}"


def command_version(command: str) -> str:
    try:
        completed = subprocess.run(
            [*shlex.split(command), "--version"],
            check=False,
            capture_output=True,
            text=True,
            timeout=3,
        )
    except (OSError, subprocess.SubprocessError, ValueError):
        return "unknown"
    combined = "\n".join(part for part in (completed.stdout, completed.stderr) if part)
    for line in combined.splitlines():
        if line.strip():
            return line.strip()
    return "unknown"


def cpu_model() -> str:
    system = platform.system()
    if system == "Linux":
        try:
            for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
                key, separator, value = line.partition(":")
                if separator and key.strip() in {"model name", "Hardware"} and value.strip():
                    return value.strip()
        except OSError:
            pass
    elif system == "Darwin":
        try:
            completed = subprocess.run(
                ["sysctl", "-n", "machdep.cpu.brand_string"],
                check=False,
                capture_output=True,
                text=True,
                timeout=3,
            )
            if completed.stdout.strip():
                return completed.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            pass
    value = platform.processor() or os.environ.get("PROCESSOR_IDENTIFIER", "")
    return value.strip() or "unknown"


def process_affinity() -> str:
    if hasattr(os, "sched_getaffinity"):
        try:
            return ",".join(str(cpu) for cpu in sorted(os.sched_getaffinity(0))) or "unknown"
        except OSError:
            pass
    return "unknown"


def process_priority() -> str:
    if hasattr(os, "getpriority") and hasattr(os, "PRIO_PROCESS"):
        try:
            return str(os.getpriority(os.PRIO_PROCESS, 0))
        except OSError:
            pass
    return "unknown"


def discover_backend_capabilities():
    system = platform.system()
    udp_supported = system in {"Linux", "Darwin", "Windows"}
    device_supported = system == "Linux"

    def capability(platform_supported, build_enabled, benchmark_implemented, reason):
        return {
            "platform_supported": bool(platform_supported),
            "build_enabled": bool(build_enabled),
            "benchmark_implemented": bool(benchmark_implemented),
            "reason": reason,
        }

    return {
        "driver": capability(True, True, True, "portable DRIVER benchmark fixture is part of the campaign"),
        "loopback": capability(True, True, True, "LOOPBACK benchmark fixture is part of the campaign"),
        "simulator": capability(True, True, True, "SIMULATOR benchmark fixture is enabled by the campaign"),
        "udp": capability(
            udp_supported,
            udp_supported,
            True,
            "VSPW-TP/UDP benchmark is available on POSIX/Winsock hosts"
            if udp_supported else "VSPW-TP/UDP runtime is unsupported on this platform",
        ),
        "device": capability(
            device_supported,
            device_supported,
            True,
            "DEVICE/VSPD benchmark is available on Linux"
            if device_supported else "DEVICE/VSPD runtime is Linux-only",
        ),
    }


def finalize_campaign(root: Path, campaign):
    calibrations = load_jsonl(root / "calibration.jsonl")
    calibration = calibrations[0] if calibrations else {}
    calibration_platform = calibration.get("platform", {})
    calibration_compiler = calibration.get("compiler", {})
    calibration_counter = calibration.get("counter", {})
    compiler_command = os.environ.get("CC") or "cc"
    control = campaign.get("host_control", {})
    if control.get("enabled") and control.get("selected_cpu") is not None:
        measurement_affinity = f"cpu:{control['selected_cpu']}"
    else:
        measurement_affinity = process_affinity()

    campaign["schema"] = "spwkit.profile.campaign.v2"
    campaign["host_environment"] = {
        "os_name": platform.system() or "unknown",
        "kernel_release": platform.release() or "unknown",
        "architecture": platform.machine() or calibration_platform.get("architecture", "unknown"),
        "cpu_model": cpu_model(),
        "compiler": {
            "family": calibration_compiler.get("family", "unknown"),
            "command": compiler_command,
            "version": command_version(compiler_command),
        },
        "counter": {
            "kind": calibration_counter.get("kind", "unknown"),
            "width_bits": calibration_counter.get("width_bits", "unknown"),
            "frequency_hz": calibration_counter.get("frequency_hz", "unknown"),
        },
        "measurement_affinity": measurement_affinity,
        "orchestrator_affinity": process_affinity(),
        "process_priority": process_priority(),
    }
    campaign["backend_capabilities"] = discover_backend_capabilities()
    (root / "campaign.json").write_text(json.dumps(campaign, indent=2) + "\n")
    return campaign


def classify_coverage_status(capability, is_measured):
    if is_measured:
        return "measured", None
    if not capability["platform_supported"]:
        return "unsupported-platform", capability["reason"]
    if not capability["build_enabled"]:
        return "not-built", capability["reason"]
    if not capability["benchmark_implemented"]:
        return "not-implemented-benchmark", capability["reason"]
    return "not-implemented-benchmark", "benchmark result missing from completed campaign"


def coverage_entries(root: Path, campaign):
    capabilities = campaign["backend_capabilities"]

    def measured(path: str):
        return (root / path).exists() and (root / path).stat().st_size > 0

    def entry(backend, path_name, direction, result_path, measured_reason):
        is_measured = measured(result_path)
        status, fallback_reason = classify_coverage_status(capabilities[backend], is_measured)
        return {
            "backend": backend,
            "path": path_name,
            "direction": direction,
            "status": status,
            "reason": measured_reason if is_measured else fallback_reason,
        }

    entries = [
        entry("driver", "copied", "tx", "comparison/tx_api_native.jsonl",
              "same-process direct/provider comparison"),
        entry("driver", "copied", "rx", "comparison/rx_native_api.jsonl",
              "same-process direct/provider comparison"),
        entry("driver", "zero-copy", "tx", "comparison/driver_tx_copy_zero_copy.jsonl",
              "provider-owned DMA-buffer copied-vs-zero-copy comparison"),
        entry("driver", "zero-copy", "rx", "comparison/driver_rx_copy_zero_copy.jsonl",
              "provider-owned RX DMA-buffer copied-vs-zero-copy comparison"),
        entry("loopback", "standard", "tx", "backends/loopback_tx.jsonl",
              "complete public API operation"),
        entry("loopback", "standard", "rx", "backends/loopback_rx.jsonl",
              "complete public API operation"),
        entry("simulator", "standard", "tx", "backends/simulator_tx.jsonl",
              "complete public API operation"),
        entry("simulator", "standard", "rx", "backends/simulator_rx.jsonl",
              "complete public API operation"),
        entry("udp", "vspw-tp", "tx", "comparison/udp_tx.jsonl",
              "same-process direct UDP socket comparison"),
        entry("udp", "vspw-tp", "rx", "comparison/udp_rx.jsonl",
              "same-process direct UDP socket comparison"),
        entry("device", "vspd", "tx", "comparison/device_tx.jsonl",
              "same-vspwd-daemon direct VSPD comparison"),
        entry("device", "vspd", "rx", "comparison/device_rx.jsonl",
              "same-vspwd-daemon direct VSPD comparison"),
    ]
    counts = {}
    for item in entries:
        counts[item["status"]] = counts.get(item["status"], 0) + 1
    return {
        "schema": "spwkit.profile.coverage.v2",
        "entries": entries,
        "counts": counts,
    }

def append_comparison(lines, title, comparison_path: Path):
    comparisons = load_jsonl(comparison_path)
    if not comparisons:
        return
    lines.append(title)
    lines.append("--------------------------------------------------------------")
    lines.append("Payload   Native med   SpWKit med   Delta   Delta %   p95 delta")
    for row in comparisons:
        native = row["native_statistics"]
        spwkit = row["spwkit_statistics"]
        delta = row["delta"]
        p95_delta = spwkit["p95"] - native["p95"]
        lines.append(
            f"{row['payload_bytes']:>6} B   "
            f"{fmt_ticks(native['median']):>10}   "
            f"{fmt_ticks(spwkit['median']):>10}   "
            f"{fmt_ticks(delta['median_ticks']):>5}   "
            f"{fmt_percent(delta['median_percent']):>7}   "
            f"{fmt_ticks(p95_delta):>9}"
        )
    lines.append("")


def append_zero_copy(lines, comparison_path: Path):
    rows = load_jsonl(comparison_path)
    if not rows:
        return
    lines.append("DRIVER TX: copied vs zero-copy DMA-buffer preparation")
    lines.append("----------------------------------------------------------------")
    lines.append("Payload   Copied med   ZC med   ZC-Copy   Delta %   Acquire   Submit")
    for row in rows:
        copied = row["copied_statistics"]
        zero_copy = row["zero_copy_statistics"]
        ownership = row["ownership_statistics"]
        delta = row["delta"]
        lines.append(
            f"{row['payload_bytes']:>6} B   "
            f"{fmt_ticks(copied['median']):>10}   "
            f"{fmt_ticks(zero_copy['median']):>6}   "
            f"{fmt_ticks(delta['median_ticks']):>7}   "
            f"{fmt_percent(delta['median_percent']):>7}   "
            f"{fmt_ticks(ownership['acquire']['median']):>7}   "
            f"{fmt_ticks(ownership['submit']['median']):>6}"
        )
    lines.append("")


def append_zero_copy_rx(lines, comparison_path: Path):
    rows = load_jsonl(comparison_path)
    if not rows:
        return
    lines.append("DRIVER RX: copied vs zero-copy DMA-buffer visibility")
    lines.append("----------------------------------------------------------------")
    lines.append("Payload   Copied med   ZC med   ZC-Copy   Delta %   Acquire   Release")
    for row in rows:
        copied = row["copied_visibility_statistics"]
        zero_copy = row["zero_copy_visibility_statistics"]
        ownership = row["ownership_statistics"]
        delta = row["delta"]
        lines.append(
            f"{row['payload_bytes']:>6} B   "
            f"{fmt_ticks(copied['median']):>10}   "
            f"{fmt_ticks(zero_copy['median']):>6}   "
            f"{fmt_ticks(delta['median_ticks']):>7}   "
            f"{fmt_percent(delta['median_percent']):>7}   "
            f"{fmt_ticks(ownership['acquire']['median']):>7}   "
            f"{fmt_ticks(ownership['release']['median']):>7}"
        )
    lines.append("")


def append_backend(lines, title, backend_path: Path):
    rows = load_jsonl(backend_path)
    if not rows:
        return
    lines.append(title)
    lines.append("-----------------------------------------------")
    lines.append("Payload     Median       Mean        p95        p99")
    for row in rows:
        stats = row["statistics"]
        lines.append(
            f"{row['payload_bytes']:>6} B   "
            f"{fmt_ticks(stats['median']):>8}   "
            f"{fmt_ticks(stats['mean']):>8}   "
            f"{fmt_ticks(stats['p95']):>8}   "
            f"{fmt_ticks(stats['p99']):>8}"
        )
    lines.append("")


def render_summary(root: Path, campaign, coverage):
    lines = []
    lines.append("SpWKit Host Profiling Summary")
    lines.append("============================")
    lines.append(f"Result set : {campaign['result_directory_name']}")
    lines.append(f"Type       : {campaign['result_type']}")
    lines.append(f"UTC        : {campaign['timestamp_utc']}")
    lines.append(f"Commit     : {campaign['git_short_sha']} ({campaign['git_sha']})")
    lines.append(f"Build      : {campaign['build_type']} / clean serial cases")
    environment = campaign.get("host_environment", {})
    compiler = environment.get("compiler", {})
    counter = environment.get("counter", {})
    lines.append(f"Host       : {environment.get('os_name', 'unknown')} {environment.get('kernel_release', 'unknown')} / {environment.get('architecture', 'unknown')}")
    lines.append(f"CPU model  : {environment.get('cpu_model', 'unknown')}")
    lines.append(f"Compiler   : {compiler.get('family', 'unknown')} / {compiler.get('version', 'unknown')}")
    lines.append(f"Counter    : {counter.get('kind', 'unknown')} / {counter.get('width_bits', 'unknown')} bits / {counter.get('frequency_hz', 'unknown')} Hz")
    lines.append(f"Affinity   : measurement={environment.get('measurement_affinity', 'unknown')} orchestrator={environment.get('orchestrator_affinity', 'unknown')}")
    lines.append(f"Priority   : {environment.get('process_priority', 'unknown')}")
    control = campaign.get("host_control", {})
    if control.get("enabled"):
        lines.append(f"CPU        : pinned logical CPU {control.get('selected_cpu')} ({control.get('auto_cpu_policy')})")
        lines.append(f"Precond.   : {control.get('precondition_seconds', 0)}s before each campaign step")
        lines.append(f"Governor   : requested={control.get('requested_governor')} effective={control.get('governor_effective')} driver={control.get('scaling_driver')} status={control.get('governor_change_status')}")
    else:
        lines.append("CPU        : uncontrolled / inherited scheduler affinity")
    lines.append("Units      : architectural counter ticks")
    lines.append("Display    : tick values rounded to nearest integer; JSON retains full precision")
    lines.append("")

    append_comparison(lines, "DRIVER copied TX: direct/provider vs SpWKit", root / "comparison" / "tx_api_native.jsonl")
    append_comparison(lines, "DRIVER copied RX: direct/provider vs SpWKit", root / "comparison" / "rx_native_api.jsonl")
    append_zero_copy(lines, root / "comparison" / "driver_tx_copy_zero_copy.jsonl")
    append_zero_copy_rx(lines, root / "comparison" / "driver_rx_copy_zero_copy.jsonl")
    append_comparison(lines, "UDP VSPW-TP TX: direct socket vs SpWKit", root / "comparison" / "udp_tx.jsonl")
    append_comparison(lines, "UDP VSPW-TP RX: direct socket vs SpWKit", root / "comparison" / "udp_rx.jsonl")
    append_comparison(lines, "DEVICE VSPD TX: direct VSPD vs SpWKit", root / "comparison" / "device_tx.jsonl")
    append_comparison(lines, "DEVICE VSPD RX: direct VSPD vs SpWKit", root / "comparison" / "device_rx.jsonl")

    case_files = [
        root / "cases" / f"{case_name}.jsonl"
        for case_name in campaign.get("cases", [])
        if (root / "cases" / f"{case_name}.jsonl").exists()
    ]
    if case_files:
        lines.append("DRIVER copied TX: paired layer ranges")
        lines.append("------------------------------------------------")
        lines.append("Case                     Payload   Median   p95   p99")
        for case_file in case_files:
            for row in load_jsonl(case_file):
                stats = row["statistics"]
                lines.append(
                    f"{case_file.stem:<24} "
                    f"{row['payload_bytes']:>6} B   "
                    f"{fmt_ticks(stats['median']):>6}   "
                    f"{fmt_ticks(stats['p95']):>4}   "
                    f"{fmt_ticks(stats['p99']):>4}"
                )
        lines.append("")

    append_backend(lines, "LOOPBACK TX: complete public API operation", root / "backends" / "loopback_tx.jsonl")
    append_backend(lines, "LOOPBACK RX: complete public API operation", root / "backends" / "loopback_rx.jsonl")
    append_backend(lines, "SIMULATOR TX: complete public API operation", root / "backends" / "simulator_tx.jsonl")
    append_backend(lines, "SIMULATOR RX: complete public API operation", root / "backends" / "simulator_rx.jsonl")

    lines.append("Hosted-backend coverage")
    lines.append("-----------------------")
    measured_count = coverage["counts"].get("measured", 0)
    total = len(coverage["entries"])
    lines.append(f"Measured: {measured_count}/{total}")
    for entry in coverage["entries"]:
        label = f"{entry['backend']}/{entry['path']}/{entry['direction']}"
        lines.append(f"  {label:<30} {entry['status']:<26} {entry['reason']}")
    lines.append("")

    lines.append("Result artifacts")
    lines.append("----------------")
    lines.append("  summary.txt                    ordered human-readable result")
    lines.append("  comparison/*.jsonl             exact comparison data")
    lines.append("  cases/*.jsonl                  exact paired-range data")
    lines.append("  calibration/*.json             exact counter-floor calibration")
    lines.append("  coverage.json / campaign.json  campaign metadata and coverage")
    lines.append("")

    if campaign["result_type"] == "github-hosted":
        lines.append("NOTE: GitHub-hosted timings are informational/regression data only.")
    else:
        control = campaign.get("host_control", {})
        if not control.get("enabled"):
            lines.append("WARNING: Local campaign was not CPU-pinned; use run_controlled_profile_campaign.sh for reference characterization.")
        elif control.get("requested_governor") == "performance" and control.get("governor_effective") != "performance":
            lines.append("WARNING: performance governor was requested but not effective; cross-case absolute timing may still drift.")
        else:
            lines.append("NOTE: Controlled-host affinity and cpufreq state were recorded for this campaign.")
    lines.append("Standalone counter-floor calibration is diagnostic and is not subtracted from measured intervals.")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description="Summarize a SpWKit profiling result set")
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()

    root = args.result_dir
    campaign = finalize_campaign(root, load_json(root / "campaign.json"))
    coverage = coverage_entries(root, campaign)
    (root / "coverage.json").write_text(json.dumps(coverage, indent=2) + "\n")
    summary = render_summary(root, campaign, coverage)
    (root / "summary.txt").write_text(summary)
    print(summary, end="")


if __name__ == "__main__":
    main()
