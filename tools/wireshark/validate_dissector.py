#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate deterministic VSPW-TP traffic and validate the Lua dissector with tshark."""

from __future__ import annotations

import argparse
import ipaddress
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

MAGIC = 0x56535057
HEADER_SIZE = 40
LINK_ID = 42
SESSION_A = 0x1111222233334444
SESSION_B = 0xAAAABBBBCCCCDDDD
PORT_A = 42000
PORT_B = 42001

TYPE_DATA = 1
TYPE_TIME_CODE = 2
TYPE_KEEPALIVE = 4
TYPE_ACK = 5

FLAG_EOP = 0x01
FLAG_FRAGMENT_START = 0x04
FLAG_FRAGMENT_END = 0x08
FLAG_ACK_REQUIRED = 0x10


def checksum16(data: bytes) -> int:
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack(f"!{len(data) // 2}H", data))
    total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def vspw_frame(
    message_type: int,
    flags: int,
    session_id: int,
    sequence: int,
    message_id: int,
    fragment_offset: int,
    total_size: int,
    payload: bytes = b"",
    *,
    version_major: int = 1,
    version_minor: int = 0,
) -> bytes:
    header = struct.pack(
        "!IBBBBHHIQIIII",
        MAGIC,
        version_major,
        version_minor,
        message_type,
        flags,
        HEADER_SIZE,
        len(payload),
        LINK_ID,
        session_id,
        sequence,
        message_id,
        fragment_offset,
        total_size,
    )
    assert len(header) == HEADER_SIZE
    return header + payload


def udp_ipv4_ethernet(payload: bytes, src: str, dst: str, sport: int, dport: int, ident: int) -> bytes:
    udp = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
    src_ip = ipaddress.IPv4Address(src).packed
    dst_ip = ipaddress.IPv4Address(dst).packed
    total_length = 20 + len(udp)
    ip_without_checksum = struct.pack(
        "!BBHHHBBH4s4s",
        0x45,
        0,
        total_length,
        ident,
        0,
        64,
        17,
        0,
        src_ip,
        dst_ip,
    )
    ip_checksum = checksum16(ip_without_checksum)
    ip_header = struct.pack(
        "!BBHHHBBH4s4s",
        0x45,
        0,
        total_length,
        ident,
        0,
        64,
        17,
        ip_checksum,
        src_ip,
        dst_ip,
    )
    ethernet = bytes.fromhex("0200000000020200000000010800")
    return ethernet + ip_header + udp


def sample_packets() -> list[bytes]:
    logical_payload = b"HELLOWORLD"
    packets = [
        vspw_frame(TYPE_KEEPALIVE, 0, SESSION_A, 1, 0, 0, 0),
        vspw_frame(
            TYPE_DATA,
            FLAG_FRAGMENT_START | FLAG_ACK_REQUIRED,
            SESSION_A,
            2,
            100,
            0,
            len(logical_payload),
            logical_payload[:5],
        ),
        vspw_frame(
            TYPE_DATA,
            FLAG_FRAGMENT_END | FLAG_EOP | FLAG_ACK_REQUIRED,
            SESSION_A,
            3,
            100,
            5,
            len(logical_payload),
            logical_payload[5:],
        ),
        vspw_frame(
            TYPE_ACK,
            0,
            SESSION_B,
            10,
            100,
            0,
            8,
            struct.pack("!Q", SESSION_A),
        ),
        vspw_frame(
            TYPE_TIME_CODE,
            FLAG_ACK_REQUIRED,
            SESSION_B,
            11,
            200,
            0,
            2,
            bytes((17, 0)),
        ),
        # Correct magic but unsupported major version: the dissector must claim
        # it safely and expose the frame as invalid/unsupported.
        vspw_frame(TYPE_KEEPALIVE, 0, SESSION_A, 12, 0, 0, 0, version_major=2),
        # Unrelated UDP traffic on the same port validates that heuristic mode
        # does not over-claim arbitrary UDP payloads.
        b"not-vspw",
    ]

    ethernet_frames: list[bytes] = []
    for index, payload in enumerate(packets, start=1):
        if index % 2:
            ethernet_frames.append(
                udp_ipv4_ethernet(payload, "192.0.2.10", "192.0.2.20", PORT_A, PORT_B, index)
            )
        else:
            ethernet_frames.append(
                udp_ipv4_ethernet(payload, "192.0.2.20", "192.0.2.10", PORT_B, PORT_A, index)
            )
    return ethernet_frames


def write_pcap(path: Path) -> None:
    frames = sample_packets()
    with path.open("wb") as handle:
        # Classic PCAP, little-endian, microsecond timestamps, Ethernet link type.
        handle.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        for index, frame in enumerate(frames, start=1):
            handle.write(struct.pack("<IIII", 1_700_000_000 + index, index * 1000, len(frame), len(frame)))
            handle.write(frame)


def run_tshark(tshark: str, lua: Path, pcap: Path, display_filter: str) -> list[str]:
    command = [
        tshark,
        "-X",
        f"lua_script:{lua}",
        "-r",
        str(pcap),
        "-Y",
        display_filter,
        "-T",
        "fields",
        "-e",
        "frame.number",
    ]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        print("tshark command failed:", " ".join(command), file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def require_count(tshark: str, lua: Path, pcap: Path, display_filter: str, expected: int) -> None:
    matches = run_tshark(tshark, lua, pcap, display_filter)
    if len(matches) != expected:
        raise SystemExit(
            f"filter {display_filter!r}: expected {expected} packet(s), got {len(matches)}: {matches}"
        )
    print(f"PASS {display_filter} -> {matches}")


def validate(tshark: str, lua: Path, pcap: Path) -> None:
    # Heuristic UDP registration recognizes VSPW magic and ignores unrelated UDP.
    require_count(tshark, lua, pcap, "vspw", 6)
    require_count(tshark, lua, pcap, "vspw.valid == true", 5)
    require_count(tshark, lua, pcap, "vspw.valid == false && vspw.version_major == 2", 1)
    require_count(tshark, lua, pcap, "vspw.type == 4", 2)
    require_count(
        tshark,
        lua,
        pcap,
        "vspw.type == 1 && vspw.message_id == 100 && vspw.fragment_offset == 0 "
        "&& vspw.flag.fragment_start == true && vspw.flag.ack_required == true",
        1,
    )
    require_count(
        tshark,
        lua,
        pcap,
        "vspw.type == 1 && vspw.message_id == 100 && vspw.fragment_offset == 5 "
        "&& vspw.flag.fragment_end == true && vspw.flag.eop == true",
        1,
    )
    require_count(
        tshark,
        lua,
        pcap,
        "vspw.type == 5 && vspw.message_id == 100 "
        "&& vspw.acknowledged_session_id == 0x1111222233334444",
        1,
    )
    require_count(tshark, lua, pcap, "vspw.type == 2 && vspw.time_count == 17", 1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lua", type=Path, default=Path(__file__).with_name("vspw_tp.lua"))
    parser.add_argument("--tshark", default="tshark")
    parser.add_argument("--output", type=Path, help="also write the deterministic sample PCAP here")
    args = parser.parse_args()

    tshark = shutil.which(args.tshark)
    if tshark is None:
        print(f"tshark not found: {args.tshark}", file=sys.stderr)
        return 2
    lua = args.lua.resolve()
    if not lua.is_file():
        print(f"Lua dissector not found: {lua}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="spwkit-vspw-") as temp_dir:
        pcap = Path(temp_dir) / "vspw_tp_sample.pcap"
        write_pcap(pcap)
        validate(tshark, lua, pcap)
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(pcap, args.output)
            print(f"sample capture written to {args.output}")

    print("VSPW_TP_DISSECTOR_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
