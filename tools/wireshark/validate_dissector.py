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

RAW_DEFAULT_ETHERTYPE = 0x88B5
RAW_CUSTOM_ETHERTYPE = 0x88B6
RAW_UNRELATED_ETHERTYPE = 0x88B7
RAW_SUBTYPE = 0x5357
RAW_VERSION_MAJOR = 2
RAW_VERSION_MINOR = 0

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


def raw_ethernet(
    payload: bytes,
    ethertype: int = RAW_DEFAULT_ETHERTYPE,
    *,
    declared_length: int | None = None,
    padding: bytes = b"",
) -> bytes:
    if declared_length is None:
        declared_length = len(payload)
    ethernet = bytes.fromhex("020000000002020000000001") + struct.pack("!H", ethertype)
    envelope = struct.pack(
        "!HBBH",
        RAW_SUBTYPE,
        RAW_VERSION_MAJOR,
        RAW_VERSION_MINOR,
        declared_length,
    )
    return ethernet + envelope + payload + padding


def sample_packets() -> list[bytes]:
    logical_payload = b"HELLOWORLD"
    udp_packets = [
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
        # Correct magic but unsupported major version: the UDP heuristic must
        # still claim it safely and expose it as unsupported.
        vspw_frame(TYPE_KEEPALIVE, 0, SESSION_A, 12, 0, 0, 0, version_major=2),
        # Unrelated UDP traffic validates that heuristic mode does not
        # over-claim arbitrary UDP payloads.
        b"not-vspw",
    ]

    frames: list[bytes] = []
    for index, payload in enumerate(udp_packets, start=1):
        if index % 2:
            frames.append(
                udp_ipv4_ethernet(payload, "192.0.2.10", "192.0.2.20", PORT_A, PORT_B, index)
            )
        else:
            frames.append(
                udp_ipv4_ethernet(payload, "192.0.2.20", "192.0.2.10", PORT_B, PORT_A, index)
            )

    # Default raw-Ethernet framing. The KEEPALIVE deliberately carries four
    # trailing carrier bytes beyond the declared VSPW length: they must appear
    # only as spwraw.padding and never become VSPW payload.
    frames.append(
        raw_ethernet(
            vspw_frame(TYPE_KEEPALIVE, 0, SESSION_A, 20, 0, 0, 0),
            padding=b"\xaa\xbb\xcc\xdd",
        )
    )
    frames.append(
        raw_ethernet(
            vspw_frame(TYPE_DATA, FLAG_EOP, SESSION_B, 21, 300, 0, 3, b"RAW")
        )
    )

    # Declared VSPW length is larger than the captured VSPW bytes. The raw
    # envelope must be rejected before the inner VSPW dissector is invoked.
    malformed = vspw_frame(TYPE_KEEPALIVE, 0, SESSION_A, 22, 0, 0, 0)
    frames.append(raw_ethernet(malformed, declared_length=len(malformed) + 4))

    # A caller-selected EtherType is decoded only via Decode As, not heuristic
    # Ethernet claiming.
    frames.append(
        raw_ethernet(
            vspw_frame(TYPE_KEEPALIVE, 0, SESSION_B, 23, 0, 0, 0),
            RAW_CUSTOM_ETHERTYPE,
        )
    )

    # Even a syntactically valid SpWKit envelope on another EtherType remains
    # untouched unless the user explicitly binds that EtherType.
    frames.append(
        raw_ethernet(
            vspw_frame(TYPE_KEEPALIVE, 0, SESSION_B, 24, 0, 0, 0),
            RAW_UNRELATED_ETHERTYPE,
        )
    )
    return frames

def write_pcap(path: Path) -> None:
    frames = sample_packets()
    with path.open("wb") as handle:
        # Classic PCAP, little-endian, microsecond timestamps, Ethernet link type.
        handle.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
        for index, frame in enumerate(frames, start=1):
            handle.write(struct.pack("<IIII", 1_700_000_000 + index, index * 1000, len(frame), len(frame)))
            handle.write(frame)


def run_tshark(
    tshark: str,
    lua: Path,
    pcap: Path,
    display_filter: str,
    *,
    decode_as: str | None = None,
) -> list[str]:
    command = [
        tshark,
        "-X",
        f"lua_script:{lua}",
    ]
    if decode_as is not None:
        command.extend(["-d", decode_as])
    command.extend(
        [
            "-r",
            str(pcap),
            "-Y",
            display_filter,
            "-T",
            "fields",
            "-e",
            "frame.number",
        ]
    )
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        print("tshark command failed:", " ".join(command), file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode)
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]

def require_count(
    tshark: str,
    lua: Path,
    pcap: Path,
    display_filter: str,
    expected: int,
    *,
    decode_as: str | None = None,
) -> None:
    matches = run_tshark(tshark, lua, pcap, display_filter, decode_as=decode_as)
    if len(matches) != expected:
        raise SystemExit(
            f"filter {display_filter!r}: expected {expected} packet(s), got {len(matches)}: {matches}"
        )
    print(f"PASS {display_filter} -> {matches}")

def validate(tshark: str, lua: Path, pcap: Path) -> None:
    # UDP behavior remains unchanged: heuristic recognition keys only on VSPW magic.
    # The two valid default raw-Ethernet frames are also handed to the same VSPW decoder.
    require_count(tshark, lua, pcap, "vspw", 8)
    require_count(tshark, lua, pcap, "vspw.valid == true", 7)
    require_count(tshark, lua, pcap, "vspw.valid == false && vspw.version_major == 2", 1)
    require_count(tshark, lua, pcap, "vspw.type == 4", 3)
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

    # Raw-Ethernet v2 owns only the default local-experimental EtherType.
    require_count(tshark, lua, pcap, "spwraw", 3)
    require_count(tshark, lua, pcap, "spwraw.valid == true", 2)
    require_count(tshark, lua, pcap, "spwraw.valid == false", 1)
    require_count(
        tshark,
        lua,
        pcap,
        "spwraw.valid == true && spwraw.subtype == 0x5357 "
        "&& spwraw.version_major == 2 && spwraw.version_minor == 0",
        2,
    )
    require_count(
        tshark,
        lua,
        pcap,
        "spwraw.vspw_length == 40 && spwraw.padding && vspw.type == 4",
        1,
    )
    require_count(
        tshark,
        lua,
        pcap,
        "spwraw.valid == true && vspw.type == 1 && vspw.message_id == 300 "
        "&& vspw.data == 52:41:57",
        1,
    )

    # No Ethernet heuristic is registered. A different EtherType is untouched
    # until the caller explicitly selects the raw dissector through Decode As.
    require_count(
        tshark,
        lua,
        pcap,
        "eth.type == 0x88b6 && spwraw",
        0,
    )
    require_count(
        tshark,
        lua,
        pcap,
        "eth.type == 0x88b7 && spwraw",
        0,
    )
    require_count(
        tshark,
        lua,
        pcap,
        "eth.type == 0x88b6 && spwraw.valid == true && vspw.type == 4",
        1,
        decode_as="ethertype==0x88b6,spwraw",
    )

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
