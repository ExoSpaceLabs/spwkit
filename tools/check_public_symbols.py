#!/usr/bin/env python3
"""Verify the shared libspwkit dynamic symbol surface against a manifest."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

IGNORED_TOOLCHAIN_SYMBOLS = {"_init", "_fini"}

def load_manifest(path: pathlib.Path) -> set[str]:
    symbols: set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        symbols.add(line)
    return symbols

def load_dynamic_symbols(nm: str, library: pathlib.Path) -> set[str]:
    proc = subprocess.run(
        [nm, "-D", "--defined-only", str(library)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    symbols: set[str] = set()
    for raw in proc.stdout.splitlines():
        parts = raw.split()
        if not parts:
            continue
        name = parts[-1].split("@", 1)[0]
        if name in IGNORED_TOOLCHAIN_SYMBOLS or name.startswith("__"):
            continue
        symbols.add(name)
    return symbols

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=pathlib.Path)
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("--nm", default="nm")
    args = parser.parse_args()

    expected = load_manifest(args.manifest)
    actual = load_dynamic_symbols(args.nm, args.library)
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)

    if missing:
        print("missing public ABI symbols:", file=sys.stderr)
        for symbol in missing:
            print(f"  {symbol}", file=sys.stderr)
    if unexpected:
        print("unexpected exported symbols:", file=sys.stderr)
        for symbol in unexpected:
            print(f"  {symbol}", file=sys.stderr)
    if missing or unexpected:
        return 1

    print(f"public ABI symbol surface OK: {len(expected)} symbols")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
