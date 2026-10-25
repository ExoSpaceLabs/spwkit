#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Called by the PC controller over SSH on Raspberry Pi OS/Debian Linux.
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="$ROOT_DIR/build/physical-stm32-peer"
INTERFACE="${1:-eth0}"
[[ "$INTERFACE" =~ ^[a-zA-Z0-9_.:-]{1,15}$ ]] || { echo "Invalid Pi Ethernet NIC" >&2; exit 2; }
[[ -e "/sys/class/net/$INTERFACE/address" ]] || { echo "Pi Ethernet NIC missing: $INTERFACE" >&2; exit 2; }
command -v sudo >/dev/null || { echo "sudo is required for package setup and capabilities" >&2; exit 2; }
sudo -v
declare -a missing=()
for entry in git:git cmake:cmake cc:build-essential make:build-essential ip:iproute2 setcap:libcap2-bin; do
  cmd="${entry%%:*}"
  package="${entry#*:}"
  if ! command -v "$cmd" >/dev/null 2>&1 &&
     [[ ! " ${missing[*]:-} " == *" $package "* ]]; then
    missing+=("$package")
  fi
done
if (( ${#missing[@]} > 0 )); then
  sudo apt-get update
  sudo apt-get install -y "${missing[@]}"
fi
if [[ -e "$BUILD_ROOT" ]]; then
  rm -rf -- "$BUILD_ROOT"
fi
mkdir -p "$BUILD_ROOT"
LOG="$BUILD_ROOT/build.log"
echo "[pi-stm32] preparing native host peer on $(uname -m); logs: $LOG"
if ! (
  set -e
  cmake -S "$ROOT_DIR" -B "$BUILD_ROOT/spwkit" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$BUILD_ROOT/install" \
    -DBUILD_SHARED_LIBS=OFF \
    -DSPWKIT_BUILD_TESTS=OFF -DSPWKIT_BUILD_CPP_TESTS=OFF \
    -DSPWKIT_BUILD_EXAMPLES=OFF -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
    -DSPWKIT_BUILD_SIMULATOR=OFF -DSPWKIT_BUILD_UDP=OFF \
    -DSPWKIT_BUILD_DEVICE=OFF -DSPWKIT_BUILD_VSPWD=OFF \
    -DSPWKIT_BUILD_TOOLS=OFF -DSPWKIT_BUILD_CUSE=OFF \
    -DSPWKIT_ENABLE_HEAP=ON -DSPWKIT_ENABLE_CPP=OFF \
    -DSPWKIT_VSPW_PACKET_CAPACITY=4096 \
    -DSPWKIT_VSPW_CARRIER_CAPACITY=1500
  cmake --build "$BUILD_ROOT/spwkit" --parallel
  cmake --install "$BUILD_ROOT/spwkit"
  cmake -S "$ROOT_DIR/integrations/das_stm32h755_raw_eth/host" \
    -B "$BUILD_ROOT/host-peer" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$BUILD_ROOT/install"
  cmake --build "$BUILD_ROOT/host-peer" --parallel
) >"$LOG" 2>&1; then
  echo "[pi-stm32] build failed; last 60 lines:" >&2
  tail -n 60 "$LOG" >&2
  exit 1
fi
BIN="$BUILD_ROOT/host-peer/spwkit_das_host_peer"
[[ -x "$BIN" ]] || { echo "[pi-stm32] peer executable missing: $BIN" >&2; exit 1; }
sudo -v
sudo setcap cap_net_raw+ep "$BIN"
sudo ip link set dev "$INTERFACE" up
echo "[pi-stm32] PASS: executable=$BIN interface=$INTERFACE mac=$(cat "/sys/class/net/$INTERFACE/address")"
