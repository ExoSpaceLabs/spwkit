#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Two-host SpWKit transport campaign. SSH controls the Pi; no network daemon."""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import ipaddress
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
STATE = ROOT / "build" / "physical-pi"
BIN = STATE / "bin"
READY = STATE / "pi-ready.json"
PEER = Path.home() / ".local" / "bin" / "spwkit-pi-endpoint"
MAC = re.compile(r"^(?:[0-9a-f]{2}:){5}[0-9a-f]{2}$", re.I)
PAYLOADS = (64, 256, 1024, 1400, 4096, 16384, 65536)
WIRE = {"udp": "spwkit.transport.udp-throughput.v1",
        "raw": "spwkit.transport.raw-ethernet-throughput.v1"}
ALIASES = {"pc": "10.77.77.1/24", "pi": "10.77.77.2/24"}


def say(message: str) -> None:
    print(f"[physical-pi] {message}", flush=True)


def execute(cmd: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, check=True, **kwargs)


def sudo(*cmd: str) -> None:
    execute(["sudo", "-n", *cmd])


def prerequisite_packages() -> None:
    packages = {
        "cmake": "cmake", "cc": "build-essential", "make": "build-essential", "git": "git",
        "ip": "iproute2", "setcap": "libcap2-bin",
        "ssh": "openssh-client",
    }
    missing = sorted({package for command, package in packages.items()
                      if shutil.which(command) is None})
    if missing:
        say("Installing missing OS dependencies: " + ", ".join(missing))
        if shutil.which("apt-get") is None:
            raise RuntimeError("Missing tools; automatic package install requires apt-get")
        sudo("apt-get", "update")
        sudo("apt-get", "install", "-y", *missing)


def revision() -> str:
    return execute(["git", "-C", str(ROOT), "rev-parse", "HEAD"],
                   capture_output=True).stdout.strip()


def interface_candidates() -> list[str]:
    names = []
    for path in Path("/sys/class/net").iterdir():
        if path.name == "lo" or not (path / "device").exists():
            continue
        if path.name.startswith(("wl", "ww", "docker", "br-", "veth")):
            continue
        if (path / "carrier").exists():
            try:
                if (path / "carrier").read_text().strip() == "1":
                    names.append(path.name)
            except OSError:
                pass
    return names


def choose_interface(name: str | None, is_pi: bool) -> str:
    if not name and is_pi and Path("/sys/class/net/eth0").exists():
        name = "eth0"
    if not name:
        found = interface_candidates()
        if len(found) != 1:
            raise RuntimeError(
                "Cannot uniquely identify wired Ethernet interface; pass --interface. "
                "Candidates: " + repr(found))
        name = found[0]
    if not re.fullmatch(r"[A-Za-z0-9_.:-]{1,15}", name) or not (
        Path("/sys/class/net") / name
    ).exists():
        raise RuntimeError(f"Invalid or missing interface: {name}")
    return name


def mac_address(interface: str) -> str:
    value = (Path("/sys/class/net") / interface / "address").read_text().strip()
    if MAC.fullmatch(value) is None or (int(value[:2], 16) & 1):
        raise RuntimeError(f"Expected unicast MAC on {interface}, got {value}")
    return value


def configure_network(interface: str, address: str) -> None:
    # Never flush addresses, change default routes, or stop other services.
    address = str(ipaddress.IPv4Interface(address))
    sudo("ip", "link", "set", "dev", interface, "up")
    result = execute(["ip", "-4", "-o", "addr", "show", "dev", interface],
                     capture_output=True).stdout
    if address not in result:
        sudo("ip", "-4", "addr", "add", address, "dev", interface)
    say(f"Ethernet {interface}: {address}; MAC {mac_address(interface)}")


def build_local() -> None:
    execute(["sudo", "-v"])
    prerequisite_packages()
    # Restrict deletion to the build tree owned by this campaign only.
    if STATE.resolve().parent != (ROOT / "build").resolve():
        raise RuntimeError("Refusing unsafe build cleanup")
    build = STATE / "build"
    shutil.rmtree(build, ignore_errors=False) if build.exists() else None
    if BIN.exists():
        shutil.rmtree(BIN)
    BIN.mkdir(parents=True)
    say("Clean Release build of SpWKit UDP and RAW Ethernet peers")
    log = STATE / "build.log"
    STATE.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as stream:
        subprocess.run(["cmake", "-S", str(ROOT / "benchmarks/transport"),
                        "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"],
                       stdout=stream, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["cmake", "--build", str(build), "--parallel",
                        "--target", "spwkit_udp_perf", "spwkit_raw_ethernet_perf"],
                       stdout=stream, stderr=subprocess.STDOUT, check=True)
    for filename in ("spwkit_udp_perf", "spwkit_raw_ethernet_perf"):
        source = build / filename
        if not source.is_file():
            raise RuntimeError(f"Missing built peer {source}; see {log}")
        shutil.copy2(source, BIN / filename)
    sudo("setcap", "cap_net_raw+ep", str(BIN / "spwkit_raw_ethernet_perf"))
    say("Installed into campaign-owned directory " + str(BIN))


def details(interface: str) -> dict[str, object]:
    speed = "unavailable"
    if shutil.which("ethtool"):
        result = subprocess.run(["ethtool", interface], capture_output=True, text=True)
        for line in result.stdout.splitlines():
            if "Speed:" in line:
                speed = line.strip().split(":", 1)[1].strip()
    return {
        "sha": revision(), "interface": interface, "mac": mac_address(interface),
        "system": platform.platform(), "machine": platform.machine(),
        "hostname": socket.gethostname(), "speed": speed,
        "python": platform.python_version(),
    }


def prepare_pi(args: argparse.Namespace) -> None:
    if platform.system() != "Linux":
        raise RuntimeError("Physical Pi mode requires Linux")
    READY.unlink(missing_ok=True)
    build_local()
    interface = choose_interface(args.interface, True)
    configure_network(interface, args.pi_address)
    if (Path("/sys/class/net") / interface / "carrier").read_text().strip() != "1":
        raise RuntimeError(f"No physical Ethernet carrier on Pi interface {interface}")
    READY.write_text(json.dumps(details(interface), indent=2) + "\n")
    PEER.parent.mkdir(parents=True, exist_ok=True)
    # Per-user wrapper for SSH; no global installations or background service.
    wrapper = "#!/bin/sh\nexec python3 " + shlex.quote(str(Path(__file__).resolve())) + ' "$@"\n'
    PEER.write_text(wrapper)
    PEER.chmod(0o700)
    say("Pi preparation PASS; remote endpoint installed at " + str(PEER))
    say("SSH remains the control channel; run --mode pc on the PC.")
    say("Pi interface IP: " + str(args.pi_address))


def endpoint(args: argparse.Namespace) -> None:
    if not READY.is_file():
        raise RuntimeError("Pi not prepared; run --mode pi first")
    saved = json.loads(READY.read_text())
    if saved["sha"] != revision():
        raise RuntimeError("Pi checkout changed; rerun --mode pi")
    if args.transport not in WIRE or args.role not in ("source", "sink"):
        raise RuntimeError("Invalid endpoint request")
    if not 1 <= args.payload <= 1048576 or not 1 <= args.bytes <= (1 << 36):
        raise RuntimeError("Invalid transfer size")
    if args.link_id not in (264, 265, 364, 365):
        raise RuntimeError("Unsupported link ID")
    if args.transport == "udp":
        if args.link_id not in (264, 265):
            raise RuntimeError("Invalid UDP link ID")
        local_port = 43001 if args.link_id == 264 else 44001
        remote_port = 43000 if args.link_id == 264 else 44000
        cmd = [str(BIN / "spwkit_udp_perf"), "--role", args.role,
               "--local-address", args.pi_address.split("/")[0],
               "--remote-address", args.pc_address.split("/")[0],
               "--local-port", str(local_port), "--remote-port", str(remote_port)]
    else:
        if args.link_id not in (364, 365):
            raise RuntimeError("Invalid RAW link ID")
        if not args.remote_mac or MAC.fullmatch(args.remote_mac) is None:
            raise RuntimeError("Expected PC MAC")
        cmd = [str(BIN / "spwkit_raw_ethernet_perf"), "--role", args.role,
               "--interface", str(saved["interface"]),
               "--remote-mac", args.remote_mac,
               "--ether-type", hex(0x88B5 if args.link_id == 364 else 0x88B6)]
    cmd += ["--link-id", str(args.link_id), "--seed", str(args.link_id),
            "--payload-size", str(args.payload), "--total-bytes", str(args.bytes)]
    # Preserve endpoint output over SSH, do not swallow exit status.
    completed = subprocess.run(cmd, timeout=args.endpoint_timeout)
    sys.exit(completed.returncode)


def ssh_cmd(args: argparse.Namespace, control_path: str, parameters: list[str]) -> list[str]:
    # All arguments are produced from validated numeric/interface/MAC inputs.
    return ["ssh", "-S", control_path, "-o", "ControlMaster=auto",
            "-o", "ControlPersist=300", "-o", "ConnectTimeout=10",
            args.pi_host, "$HOME/.local/bin/spwkit-pi-endpoint " +
            shlex.join(parameters)]


def read_json(stdout: str, schema: str) -> dict[str, object]:
    found = []
    for line in stdout.splitlines():
        # Transport binaries emit one JSON row; no guessing when malformed.
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if obj.get("schema") == schema:
            found.append(obj)
    if len(found) != 1:
        raise RuntimeError(f"Expected one {schema} result; got {len(found)}: {stdout[-1500:]}")
    return found[0]


def direction_spec(transport: str, direction: str) -> tuple[int, int, int]:
    if transport == "udp":
        return (264, 43000, 43001) if direction == "pc-to-pi" else (265, 44000, 44001)
    return (364 if direction == "pc-to-pi" else 365, 0x88B5, 0x88B6)


def pc_command(args: argparse.Namespace, transport: str, role: str,
               link_id: int, payload: int, total: int, pi_mac: str,
               interface: str) -> list[str]:
    if transport == "udp":
        local_port = 43000 if link_id == 264 else 44000
        remote_port = 43001 if link_id == 264 else 44001
        cmd = [str(BIN / "spwkit_udp_perf"), "--role", role,
               "--local-address", args.pc_address.split("/")[0],
               "--remote-address", args.pi_address.split("/")[0],
               "--local-port", str(local_port), "--remote-port", str(remote_port)]
    else:
        cmd = [str(BIN / "spwkit_raw_ethernet_perf"), "--role", role,
               "--interface", interface, "--remote-mac", pi_mac,
               "--ether-type", hex(0x88B5 if link_id == 364 else 0x88B6)]
    return cmd + ["--link-id", str(link_id), "--seed", str(link_id),
                  "--payload-size", str(payload), "--total-bytes", str(total)]


def test_case(args: argparse.Namespace, control_path: str, interface: str,
              pi_mac: str, transport: str, direction: str, payload: int,
              total: int, repeat: int, output: Path) -> dict[str, object]:
    directions = ("pc-to-pi", "pi-to-pc") if direction == "duplex" else (direction,)
    jobs: list[tuple[str, str, int, subprocess.Popen[str]]] = []
    ident = f"{transport}-{direction}-{payload}-bytes{total}-r{repeat:02d}"
    deadline = max(120, min(3600, int(total / 250000) + 120))
    for flow in directions:
        link_id = direction_spec(transport, flow)[0]
        pc_role = "source" if flow == "pc-to-pi" else "sink"
        pi_role = "sink" if flow == "pc-to-pi" else "source"
        for side, role in (("pi", pi_role), ("pc", pc_role)):
            if role != "sink":
                continue
            if side == "pi":
                params = ["--mode", "endpoint", "--transport", transport,
                          "--role", role, "--payload", str(payload),
                          "--bytes", str(total), "--link-id", str(link_id),
                          "--remote-mac", mac_address(interface),
                          "--pc-address", args.pc_address,
                          "--pi-address", args.pi_address,
                          "--endpoint-timeout", str(deadline)]
                cmd = ssh_cmd(args, control_path, params)
            else:
                cmd = pc_command(args, transport, role, link_id, payload, total, pi_mac, interface)
            jobs.append((side, role, link_id, subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)))
    time.sleep(0.3)
    for flow in directions:
        link_id = direction_spec(transport, flow)[0]
        pc_role = "source" if flow == "pc-to-pi" else "sink"
        pi_role = "sink" if flow == "pc-to-pi" else "source"
        for side, role in (("pi", pi_role), ("pc", pc_role)):
            if role != "source":
                continue
            if side == "pi":
                params = ["--mode", "endpoint", "--transport", transport,
                          "--role", role, "--payload", str(payload),
                          "--bytes", str(total), "--link-id", str(link_id),
                          "--remote-mac", mac_address(interface),
                          "--pc-address", args.pc_address,
                          "--pi-address", args.pi_address,
                          "--endpoint-timeout", str(deadline)]
                cmd = ssh_cmd(args, control_path, params)
            else:
                cmd = pc_command(args, transport, role, link_id, payload, total, pi_mac, interface)
            jobs.append((side, role, link_id, subprocess.Popen(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)))
    results: dict[tuple[str, int], dict[str, object]] = {}
    try:
        for side, role, link_id, process in jobs:
            stdout, stderr = process.communicate(timeout=deadline)
            (output / f"{ident}-{side}-{role}-{link_id}.log").write_text(
                "stdout:\n" + stdout + "\nstderr:\n" + stderr)
            if process.returncode != 0:
                raise RuntimeError(
                    f"{ident} {side}/{role} failed rc={process.returncode}: {stderr[-1200:]}")
            result = read_json(stdout, WIRE[transport])
            if result.get("role") != role or result.get("link_id") != link_id:
                raise RuntimeError(f"{ident}: mismatched {side}/{role} identity")
            if int(result.get("total_bytes", -1)) != total:
                raise RuntimeError(f"{ident}: wrong byte count on {side}/{role}")
            if int(result.get("dropped_packets", -1)) or int(result.get("link_errors", -1)):
                raise RuntimeError(f"{ident}: dropped packets or link errors: {result}")
            results[(role, link_id)] = result
    finally:
        for _side, _role, _id, process in jobs:
            if process.poll() is None:
                process.terminate()
        for _side, _role, _id, process in jobs:
            if process.poll() is None:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
    streams = []
    for flow in directions:
        link_id = direction_spec(transport, flow)[0]
        tx, rx = results[("source", link_id)], results[("sink", link_id)]
        for field in ("total_bytes", "payload_bytes", "packets"):
            if tx[field] != rx[field]:
                raise RuntimeError(f"{ident} link {link_id}: {field} mismatch")
        streams.append({"direction": flow, "source": tx, "sink": rx})
    return {"transport": transport, "mode": direction, "payload_bytes": payload,
            "total_bytes": total, "repeat": repeat, "streams": streams}


def report(output: Path, cases: list[dict[str, object]], metadata: dict[str, object]) -> None:
    rows: list[dict[str, object]] = []
    samples = []
    groups: dict[tuple[str, str, int, int], list[float]] = {}
    for case in cases:
        # Duplex is the sum of separately timed stream rates, not physical line capacity.
        rate = sum(float(s["source"]["payload_mbps"]) for s in case["streams"])
        key = (str(case["transport"]), str(case["mode"]),
               int(case["payload_bytes"]), int(case["total_bytes"]))
        groups.setdefault(key, []).append(rate)
        samples.append({**{k: v for k, v in case.items() if k != "streams"},
                        "combined_sender_mbps": rate, "streams": case["streams"]})
    for (transport, mode, payload, total), values in sorted(groups.items()):
        rows.append({"transport": transport, "mode": mode, "payload_bytes": payload,
                     "transfer_bytes": total, "repeats": len(values),
                     "median_mbps": round(statistics.median(values), 4),
                     "mean_mbps": round(statistics.mean(values), 4),
                     "stdev_mbps": round(statistics.stdev(values), 4)
                     if len(values) > 1 else 0.,
                     "min_mbps": round(min(values), 4),
                     "max_mbps": round(max(values), 4)})
    (output / "samples.json").write_text(json.dumps(samples, indent=2) + "\n")
    (output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    with (output / "results.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    lines = ["# Physical PC ↔ Raspberry Pi 5 SpWKit benchmark", "",
             "This is **real hardware evidence**, unlike GitHub-hosted Docker CI.",
             "Each sample checks equal sender/receiver bytes and packet counts,",
             "and rejects link errors or application packet drops.",
             "",
             "**Duplex:** Mbit/s is the sum of independently timed sender streams;",
             "it is not a synchronized physical-link capacity figure.", "",
             f"Source commit: `{metadata['pc']['sha']}`", "",
             "| Transport | Direction | Payload (B) | Transfer (B) | Repeats | Median Mbit/s | Stdev | Min | Max |",
             "|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for row in rows:
        lines.append("| {transport} | {mode} | {payload_bytes} | {transfer_bytes} | {repeats} | {median_mbps:.2f} | {stdev_mbps:.2f} | {min_mbps:.2f} | {max_mbps:.2f} |".format(**row))
    lines.extend(["", "Raw endpoint JSON and stderr logs are retained per test case.",
                  "See `environment.json`, `samples.json`, and `results.csv`."])
    (output / "README.md").write_text("\n".join(lines) + "\n")


def run_pc(args: argparse.Namespace) -> None:
    if not args.pi_host or not re.fullmatch(r"[A-Za-z0-9_.@-]+", args.pi_host):
        raise RuntimeError("--pi-host must be an SSH host or user@host (no arbitrary options)")
    if not 1 <= args.repeats <= 20:
        raise RuntimeError("--repeats must be 1..20")
    if args.total_bytes < 1 or args.bulk_bytes < 1:
        raise RuntimeError("transfer bytes must be positive")
    payloads = [int(x) for x in args.payloads.split(",")]
    if not payloads or not all(1 <= x <= 1048576 for x in payloads):
        raise RuntimeError("payloads must be comma-separated values 1..1048576")
    build_local()
    interface = choose_interface(args.interface, False)
    configure_network(interface, args.pc_address)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = STATE / ("results-" + stamp)
    output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="spwkit-pi-ssh-") as temp:
        control = str(Path(temp) / "master")
        # Initial SSH authentication may require the normal password/key prompt.
        execute(ssh_cmd(args, control, ["--mode", "info"]), capture_output=True)
        try:
            ready = execute(ssh_cmd(args, control, ["--mode", "info"]), capture_output=True)
            remote = json.loads(ready.stdout.strip())
            local = details(interface)
            if local["sha"] != remote["sha"]:
                raise RuntimeError(f"Git revision mismatch: PC={local['sha']} Pi={remote['sha']}")
            pi_mac = str(remote["mac"])
            if MAC.fullmatch(pi_mac) is None:
                raise RuntimeError("Invalid Pi Ethernet MAC reported via SSH")
            metadata = {"pc": local, "pi": remote,
                        "pc_address": args.pc_address, "pi_address": args.pi_address,
                        "completed_utc": stamp, "repeats": args.repeats}
            (output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
            cases: list[dict[str, object]] = []
            try:
                for transport in ("udp", "raw"):
                    for payload in payloads:
                        for repetition in range(1, args.repeats + 1):
                            for mode in ("pc-to-pi", "pi-to-pc", "duplex"):
                                say(f"{transport} {mode} payload={payload} bytes={args.total_bytes} repeat={repetition}/{args.repeats}")
                                case = test_case(args, control, interface, pi_mac, transport,
                                                 mode, payload, args.total_bytes,
                                                 repetition, output)
                                cases.append(case)
                                (output / "samples-in-progress.json").write_text(
                                    json.dumps(cases, indent=2) + "\n")
                    for bulk_payload in (4096, 65536):
                        for mode in ("pc-to-pi", "pi-to-pc"):
                            say(f"{transport} sustained {mode} payload={bulk_payload} bytes={args.bulk_bytes}")
                            case = test_case(args, control, interface, pi_mac, transport,
                                             mode, bulk_payload, args.bulk_bytes,
                                             1, output)
                            cases.append(case)
                            (output / "samples-in-progress.json").write_text(
                                json.dumps(cases, indent=2) + "\n")
                report(output, cases, metadata)
            except Exception:
                say("FAIL; partial evidence retained under " + str(output))
                raise
            say("PASS; report: " + str(output / "README.md"))
            say("CSV: " + str(output / "results.csv"))
        finally:
            subprocess.run(["ssh", "-S", control, "-O", "exit", args.pi_host],
                           capture_output=True, text=True, check=False)


def info() -> None:
    if not READY.is_file():
        raise RuntimeError("Pi not prepared; run --mode pi")
    saved = json.loads(READY.read_text())
    if saved["sha"] != revision():
        raise RuntimeError("Pi revision has changed; rerun --mode pi")
    print(json.dumps(saved))


def cli() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", required=True, choices=("pi", "pc", "info", "endpoint"))
    parser.add_argument("--interface", help="Ethernet interface; autodetected if unambiguous")
    parser.add_argument("--pi-host", help="SSH destination, e.g. username@10.77.77.2")
    parser.add_argument("--pc-address", default=ALIASES["pc"])
    parser.add_argument("--pi-address", default=ALIASES["pi"])
    parser.add_argument("--payloads", default=",".join(map(str, PAYLOADS)))
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--total-bytes", type=int, default=8 * 1024 * 1024)
    parser.add_argument("--bulk-bytes", type=int, default=1 << 30)
    parser.add_argument("--transport", choices=("udp", "raw"))
    parser.add_argument("--role", choices=("source", "sink"))
    parser.add_argument("--payload", type=int)
    parser.add_argument("--bytes", type=int)
    parser.add_argument("--link-id", type=int)
    parser.add_argument("--remote-mac")
    parser.add_argument("--endpoint-timeout", type=int, default=3600)
    return parser.parse_args()


if __name__ == "__main__":
    args = cli()
    try:
        if args.mode == "pi":
            prepare_pi(args)
        elif args.mode == "pc":
            run_pc(args)
        elif args.mode == "info":
            info()
        elif args.mode == "endpoint":
            endpoint(args)
    except (RuntimeError, OSError, subprocess.SubprocessError, ValueError) as exc:
        print(f"[physical-pi] FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
