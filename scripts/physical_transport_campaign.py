#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Three-device physical qualification. Run ONCE on the Linux PC.

SSH over Wi-Fi provisions/controls Pi; ST-LINK USB always remains on PC.
Reconnection prompts are the only required hands-on operations.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import importlib.util
import json
import os
from pathlib import Path
import posixpath
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
REPO_URL = "https://github.com/ExoSpaceLabs/spwkit.git"
BRANCH = "perf/end-to-end-transport-campaign"
VALID_SSH = re.compile(r"^[a-zA-Z0-9_.@-]+$")
VALID_NIC = re.compile(r"^[a-zA-Z0-9_.:-]{1,15}$")


def status(message: str) -> None:
    print("[three-device] " + message, flush=True)


def execute(cmd: list[str], *, log: Path | None = None,
            env: dict[str, str] | None = None) -> None:
    if log is None:
        subprocess.run(cmd, check=True, env=env)
        return
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w", encoding="utf-8") as stream:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, env=env)
        assert p.stdout is not None
        for line in p.stdout:
            print(line, end="", flush=True)
            stream.write(line)
        if p.wait() != 0:
            raise RuntimeError("Phase failed. Log: " + str(log))


class RemotePi:
    def __init__(self, destination: str, control: str):
        if not VALID_SSH.fullmatch(destination):
            raise ValueError("Invalid Pi SSH destination")
        self.destination = destination
        self.control = control

    def start(self) -> None:
        status("SSH authentication over Pi Wi-Fi: " + self.destination)
        execute(["ssh", "-M", "-f", "-N", "-S", self.control,
                 "-o", "ControlPersist=600", self.destination])
        self.command("true")

    def command(self, command: str, *, interactive: bool = False,
                capture: bool = False) -> str:
        args = ["ssh", "-S", self.control, "-o", "BatchMode=yes"]
        if interactive:
            args.append("-tt")
        args.extend([self.destination, command])
        if capture:
            return subprocess.check_output(
                args, text=True, stderr=subprocess.STDOUT
            ).strip()
        execute(args)
        return ""

    def cleanup_owned_checkout(self, path: str) -> None:
        # Only delete the exact per-SHA HIL directory; preserve user repositories.
        quoted = shlex.quote(path)
        guard = shlex.quote(posixpath.dirname(path))
        self.command(
            f"test -d {guard} && "
            f"case {quoted} in {guard}/*) rm -rf -- {quoted} ;; "
            "*) echo 'unsafe cleanup path' >&2; exit 2 ;; esac"
        )

    def stop(self) -> None:
        subprocess.run(["ssh", "-S", self.control, "-O", "exit",
                        self.destination], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def prepare_checkout(pi: RemotePi, sha: str) -> str:
    home = pi.command('printf "%s" "$HOME"', capture=True)
    if not home.startswith("/") or "\n" in home:
        raise RuntimeError("Could not discover remote Pi HOME")
    path = posixpath.join(home, ".cache", "spwkit-hil", sha)
    q = shlex.quote(path)
    remote_cmd = (
        "command -v git >/dev/null || "
        "(sudo apt-get update && sudo apt-get install -y git); "
        f"mkdir -p {shlex.quote(posixpath.dirname(path))}; "
        f"if [ ! -d {q}/.git ]; then "
        f"git clone --branch {BRANCH} {REPO_URL} {q}; fi; "
        f"test -z \"$(git -C {q} status --porcelain)\" || "
        "{ echo 'Pi HIL checkout has uncommitted changes'; exit 2; }; "
        f"git -C {q} fetch origin {sha} && "
        f"git -C {q} checkout --detach {sha} && "
        f"test \"$(git -C {q} rev-parse HEAD)\" = {sha}"
    )
    status("Provisioning isolated Pi repo and exact Git revision")
    pi.command(remote_cmd, interactive=True)
    return path


def prompt_hardware(message: str, auto_yes: bool) -> None:
    status(message)
    if auto_yes:
        status("Hardware connection externally confirmed by --yes")
        return
    if not sys.stdin.isatty():
        raise RuntimeError("Interactive terminal required for reconnection prompt")
    answer = input("Press ENTER when connected (q to abort): ").strip()
    if answer:
        raise RuntimeError("Operator stopped the hardware campaign")


def completed_pi_baseline() -> Path | None:
    base = ROOT / "build" / "physical-pi"
    for sample in sorted(base.glob("results-*/samples.json"), reverse=True):
        folder = sample.parent
        if not all((folder / name).is_file() for name in (
                "README.md", "results.csv", "environment.json")):
            continue
        try:
            rows = json.loads(sample.read_text())
            modes = {(x["transport"], x["mode"]) for x in rows}
            if (len(rows) >= 134 and
                all((t, m) in modes for t in ("raw", "udp")
                    for m in ("pc-to-pi", "pi-to-pc", "duplex"))):
                return folder
        except (ValueError, KeyError, TypeError):
            pass
    return None


def find_pi_ethernet(pi: RemotePi, remote: str) -> str:
    program = posixpath.join(remote, "benchmarks", "transport", "physical_pi_campaign.py")
    # Reuse exactly the same NIC detection that the Pi transport runner uses.
    py = ("import runpy; m=runpy.run_path(" + repr(program) + "); "
          "print(m['choose_interface'](None, True))")
    iface = pi.command("python3 -c " + shlex.quote(py), capture=True)
    # A helper may log an autodetection warning to stdout; take final line.
    name = iface.splitlines()[-1].strip()
    if not VALID_NIC.fullmatch(name):
        raise RuntimeError("Could not autodetect Pi wired NIC: " + repr(iface))
    status("Pi wired Ethernet automatically detected as " + name)
    return name


def pi_pc(pi: RemotePi, remote: str, args: argparse.Namespace,
          out: Path) -> None:
    prompt_hardware(
        "PHASE 1/3: Ethernet PC ↔ Pi eth0; SSH remains on Wi-Fi.", args.yes
    )
    pi.command(f"cd {shlex.quote(remote)} && "
               f"python3 benchmarks/transport/physical_pi_campaign.py "
               f"--mode pi --interface {shlex.quote(args.pi_interface)}",
               interactive=True)
    execute([sys.executable,
             str(ROOT / "benchmarks/transport/physical_pi_campaign.py"),
             "--mode", "pc", "--pi-host", pi.destination,
             "--ssh-control-path", pi.control,
             "--interface", args.pc_interface], log=out / "pc-pi.log")
    folder = completed_pi_baseline()
    if folder is None:
        raise RuntimeError("PC↔Pi runner returned PASS without complete evidence")
    shutil.copytree(folder, out / "pc-pi")


def pc_stm32(args: argparse.Namespace, out: Path) -> None:
    prompt_hardware(
        "PHASE 2/3: Move Ethernet PC ↔ STM32 CN14. "
        "Connect/keep ST-LINK USB to PC; board powered, JP6/JP7 fitted.",
        args.yes
    )
    execute(["bash", str(ROOT / "scripts/stm32h755_das_raw_eth_campaign.sh"),
             "--interface", args.pc_interface,
             "--output-root", str(out / "pc-stm32")],
            log=out / "pc-stm32.log")


def pi_stm32(pi: RemotePi, remote: str, args: argparse.Namespace,
             out: Path) -> None:
    prompt_hardware(
        "PHASE 3/3: Move Ethernet Pi eth0 ↔ STM32 CN14. "
        "KEEP ST-LINK USB on PC. Pi Wi-Fi SSH stays connected.",
        args.yes
    )
    pi.command(f"cd {shlex.quote(remote)} && "
               f"bash scripts/stm32h755_pi_peer_prepare.sh "
               f"{shlex.quote(args.pi_interface)}", interactive=True)
    peer = posixpath.join(remote, "build", "physical-stm32-peer",
                          "host-peer", "spwkit_das_host_peer")
    env = os.environ.copy()
    env["SPWKIT_HIL_SSH_CONTROL"] = pi.control
    status("PC flashes STM32; Pi is the measured RAW Ethernet endpoint")
    execute(["bash", str(ROOT / "scripts/stm32h755_das_raw_eth_campaign.sh"),
             "--interface", args.pi_interface,
             "--peer-ssh", pi.destination,
             "--peer-bin", peer,
             "--output-root", str(out / "pi-stm32")],
            log=out / "pi-stm32.log", env=env)


def comparison_data(out: Path) -> list[str]:
    """Compare only like-for-like STM32 HIL echo throughput and RTT."""
    records: dict[str, dict[tuple[int, int, str], dict[str, str]]] = {}
    for topology in ("pc-stm32", "pi-stm32"):
        csv_path = out / topology / "results.csv"
        if not csv_path.is_file():
            return []
        with csv_path.open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        records[topology] = {
            (int(row["clock_hz"]), int(row["payload_bytes"]), row["kind"]): row
            for row in rows
        }
    pc, pi = records["pc-stm32"], records["pi-stm32"]
    clocks = (64000000, 200000000, 300000000, 400000000)
    lines = [
        "## Controlled PC vs Pi STM32 comparison", "",
        "Identical STM32 firmware, DAS backend, logical packet size, clocks and",
        "bulk-transfer volume; only the Linux host changes between phases.",
        "These are **echo-derived** throughput/RTT measurements, not direct",
        "one-way network-link capacity and not comparable with PC↔Pi goodput.",
        "",
        "| CM7 clock | PC→STM32 bulk (Mbps) | Pi→STM32 bulk (Mbps) | Pi/PC throughput | PC RTT 4096 B (µs) | Pi RTT 4096 B (µs) |",
        "|---:|---:|---:|---:|---:|---:|",
    ]
    for clock in clocks:
        pbulk = pc.get((clock, 4096, "bulk"))
        ibulk = pi.get((clock, 4096, "bulk"))
        prtt = pc.get((clock, 4096, "rtt"))
        irtt = pi.get((clock, 4096, "rtt"))
        if not all((pbulk, ibulk, prtt, irtt)):
            raise RuntimeError(f"Missing PC/Pi STM32 comparable evidence at {clock} Hz")
        p_rate = float(pbulk["one_way_payload_mbps"])
        i_rate = float(ibulk["one_way_payload_mbps"])
        ratio = i_rate / p_rate if p_rate else 0.0
        lines.append(
            f"| {clock // 1000000} MHz | {p_rate:.3f} | {i_rate:.3f} | "
            f"{ratio:.3f}× | {float(prtt['median_ns']) / 1000:.2f} | "
            f"{float(irtt['median_ns']) / 1000:.2f} |"
        )
    return lines + [
        "", "Cycle, DMA and polling counters remain available inside each",
        "clock-specific STM32 performance-summary.md and board-evidence.log.",
        "Differences may reflect CPU/host scheduling, networking stacks, host",
        "NICs or board firmware interactions. Throughput alone does not",
        "uniquely attribute a bottleneck.", ""
    ]


def summary(out: Path, sha: str) -> None:
    lines = ["# Three-device SpWKit physical qualification", "",
             "Controller Git revision: " + sha, "",
             "PC↔Pi measures one-way/duplex goodput. STM32 campaigns measure",
             "echo RTT and board timing/cycle evidence, which are not directly",
             "comparable to the PC↔Pi one-way goodput.", "",
             "All STM32 campaigns require CM7 64/200/300/400 MHz.", "",
             "## Evidence", ""]
    for key in ("pc-pi", "pc-stm32", "pi-stm32"):
        path = out / key / "README.md"
        if path.is_file():
            lines.append(f"- [{key}]({key}/README.md): COMPLETE")
        else:
            lines.append(f"- {key}: INCOMPLETE")
    lines += comparison_data(out)
    (out / "README.md").write_text("\n".join(lines) + "\n")


def cli() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pi-ssh",
                        help="Existing Pi Wi-Fi SSH, e.g. exn-cam@192.168.1.36")
    parser.add_argument("--pc-interface", help="PC wired Ethernet interface")
    parser.add_argument("--pi-interface", help="Optional Pi Ethernet interface (automatic otherwise)")
    parser.add_argument("--keep-remote-builds", action="store_true",
                        help="Preserve isolated Pi build tree after success")
    parser.add_argument("--fresh-pc-pi", action="store_true",
                        help="Rerun an already-passed PC↔Pi benchmark")
    parser.add_argument("--yes", action="store_true",
                        help="Assume physical reconnection was externally confirmed")
    parser.add_argument("--results-root", type=Path)
    return parser.parse_args()


def main() -> int:
    args = cli()
    if not args.pi_ssh:
        if not sys.stdin.isatty():
            raise RuntimeError("Pi SSH destination required; use --pi-ssh USER@WIFI_HOST")
        args.pi_ssh = input("Pi SSH via Wi-Fi (username@hostname.local or IP): ").strip()
    if not VALID_SSH.fullmatch(args.pi_ssh):
        raise RuntimeError("Invalid Pi SSH destination")
    if args.pi_interface and not VALID_NIC.fullmatch(args.pi_interface):
        raise RuntimeError("Invalid Pi Ethernet interface")
    if not args.pc_interface:
        path = ROOT / "benchmarks/transport/physical_pi_campaign.py"
        spec = importlib.util.spec_from_file_location("spwkit_pi", path)
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        args.pc_interface = module.choose_interface(None, False)
    if not VALID_NIC.fullmatch(args.pc_interface):
        raise RuntimeError("Invalid PC Ethernet interface")
    sha = subprocess.check_output(["git", "-C", str(ROOT),
                                   "rev-parse", "HEAD"], text=True).strip()
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = args.results_root or ROOT / "build" / "three-device-campaign" / ("results-" + stamp)
    if out.exists():
        raise RuntimeError("Refusing to overwrite existing result path: " + str(out))
    out.mkdir(parents=True)
    status("Results: " + str(out))
    with tempfile.TemporaryDirectory(prefix="spwkit-hil-ssh-") as temp:
        pi = RemotePi(args.pi_ssh, str(Path(temp) / "mux"))
        try:
            pi.start()
            remote = prepare_checkout(pi, sha)
            if not args.pi_interface:
                args.pi_interface = find_pi_ethernet(pi, remote)
            existing = None if args.fresh_pc_pi else completed_pi_baseline()
            if existing:
                status("Reusing VERIFIED existing PC↔Pi baseline: " + str(existing))
                status("Original measurement revision is preserved in its environment.json")
                shutil.copytree(existing, out / "pc-pi")
            else:
                pi_pc(pi, remote, args, out)
            summary(out, sha)
            pc_stm32(args, out)
            summary(out, sha)
            pi_stm32(pi, remote, args, out)
            summary(out, sha)
            status("PASS. All three physical topologies: " + str(out / "README.md"))
            if not args.keep_remote_builds:
                pi.cleanup_owned_checkout(remote)
                status("Cleaned isolated Pi HIL staging checkout; original Pi work untouched")
            return 0
        except (RuntimeError, subprocess.SubprocessError, OSError) as exc:
            summary(out, sha)
            status("FAIL: " + str(exc))
            status("Available results/logs preserved at " + str(out))
            return 1
        finally:
            pi.stop()


if __name__ == "__main__":
    sys.exit(main())
