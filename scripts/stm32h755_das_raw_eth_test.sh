#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAS_ROOT="${DAS_ROOT:-}"
STM32_CUBE_H7_DIR="${STM32_CUBE_H7_DIR:-}"
INTERFACE=""
BUILD_ROOT="${SPWKIT_DAS_RAW_BUILD_ROOT:-$ROOT_DIR/build/das-raw-eth}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
ITERATIONS=128
WARMUP=16
GDB_BIN=""
OPENOCD_PID=""

PINNED_DAS_SHA="b10fa1e8ceb021c406d0c15c7020c0114fe0469f"
PINNED_CUBE_SHA="f5c0b7a2b1f6eb26fde150f72edb2d7deb647066"

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/stm32h755_das_raw_eth_test.sh \
    --das-root /path/to/device-abstraction-stack \
    --stm32h7-root /path/to/STM32CubeH7 \
    --interface enpXsY [options]

Builds a compact no-heap SpWKit raw-Ethernet backend, DAS for the
NUCLEO-H755ZI-Q CM7, the board responder firmware, and a Linux AF_PACKET peer.
It then flashes the board, runs physical Ethernet echo/RTT traffic, and checks
the debugger-visible board evidence.

Options:
  --das-root DIR          DAS checkout at the pinned develop commit.
  --stm32h7-root DIR      STM32CubeH7 checkout root.
  --interface IFACE       Linux Ethernet interface connected to CN14.
  --build-root DIR        Clean build root.
  --iterations N          Measured RTT exchanges per payload size (default 128).
  --warmup N              Warm-up exchanges per payload size (default 16).
  --openocd-scripts DIR   OpenOCD scripts root.
  -h, --help              Show this help.

Hardware:
  NUCLEO-H755ZI-Q CM7
  Ethernet cable on CN14
  JP6/JP7 fitted for the board Ethernet route
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --das-root) DAS_ROOT="$2"; shift 2 ;;
    --stm32h7-root) STM32_CUBE_H7_DIR="$2"; shift 2 ;;
    --interface) INTERFACE="$2"; shift 2 ;;
    --build-root) BUILD_ROOT="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --openocd-scripts) OPENOCD_SCRIPTS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 2
  }
}

for command in cmake git openocd arm-none-eabi-gcc arm-none-eabi-nm \
               arm-none-eabi-size ip sudo timeout tee grep python3; do
  need "$command"
done
if command -v gdb-multiarch >/dev/null 2>&1; then
  GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null 2>&1; then
  GDB_BIN=arm-none-eabi-gdb
else
  echo "Install gdb-multiarch or arm-none-eabi-gdb" >&2
  exit 2
fi

[[ "$ITERATIONS" =~ ^[0-9]+$ ]] && (( ITERATIONS >= 1 && ITERATIONS <= 4096 )) || {
  echo "--iterations must be 1..4096" >&2
  exit 2
}
[[ "$WARMUP" =~ ^[0-9]+$ ]] && (( WARMUP <= 4096 )) || {
  echo "--warmup must be 0..4096" >&2
  exit 2
}
[[ -n "$DAS_ROOT" && -n "$STM32_CUBE_H7_DIR" && -n "$INTERFACE" ]] || {
  usage >&2
  exit 2
}

DAS_ROOT="$(cd "$DAS_ROOT" && pwd)"
STM32_CUBE_H7_DIR="$(cd "$STM32_CUBE_H7_DIR" && pwd)"
[[ -d "/sys/class/net/$INTERFACE" ]] || {
  echo "Network interface not found: $INTERFACE" >&2
  exit 2
}

DAS_SHA="$(git -C "$DAS_ROOT" rev-parse HEAD)"
[[ "$DAS_SHA" == "$PINNED_DAS_SHA" ]] || {
  echo "DAS must be at $PINNED_DAS_SHA; found $DAS_SHA" >&2
  exit 2
}
if git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD >/dev/null 2>&1; then
  CUBE_SHA="$(git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD)"
  [[ "$CUBE_SHA" == "$PINNED_CUBE_SHA" ]] || {
    echo "STM32CubeH7 must be at $PINNED_CUBE_SHA; found $CUBE_SHA" >&2
    exit 2
  }
fi
for file in \
  "$STM32_CUBE_H7_DIR/Drivers/CMSIS/Include/core_cm7.h" \
  "$STM32_CUBE_H7_DIR/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h"; do
  [[ -f "$file" ]] || { echo "Missing CMSIS file: $file" >&2; exit 2; }
done

HOST_MAC="$(cat "/sys/class/net/$INTERFACE/address")"
[[ "$HOST_MAC" =~ ^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$ ]] || {
  echo "Invalid host MAC for $INTERFACE: $HOST_MAC" >&2
  exit 2
}
if (( 0x${HOST_MAC%%:*} & 1 )); then
  echo "Host MAC must be unicast: $HOST_MAC" >&2
  exit 2
fi

TOOLCHAIN="$DAS_ROOT/cmake/toolchains/arm-none-eabi.cmake"
BOARD_SPWKIT_BUILD="$BUILD_ROOT/board-spwkit"
BOARD_SPWKIT_INSTALL="$BUILD_ROOT/board-spwkit-install"
DAS_BUILD="$BUILD_ROOT/das"
DAS_INSTALL="$BUILD_ROOT/das-install"
FIRMWARE_BUILD="$BUILD_ROOT/firmware"
HOST_SPWKIT_BUILD="$BUILD_ROOT/host-spwkit"
HOST_SPWKIT_INSTALL="$BUILD_ROOT/host-spwkit-install"
HOST_PEER_BUILD="$BUILD_ROOT/host-peer"
ELF="$FIRMWARE_BUILD/spwkit_das_stm32h755_raw_eth.elf"
HOST_PEER="$HOST_PEER_BUILD/spwkit_das_host_peer"
LOG_DIR="$BUILD_ROOT/evidence"
OPENOCD_LOG="$LOG_DIR/openocd.log"
HOST_LOG="$LOG_DIR/host-rtt.jsonl"
GDB_LOG="$LOG_DIR/board-evidence.log"
SUMMARY_MD="$LOG_DIR/performance-summary.md"

cleanup() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" 2>/dev/null; then
    kill "$OPENOCD_PID" 2>/dev/null || true
    wait "$OPENOCD_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

rm -rf -- "$BUILD_ROOT"
mkdir -p "$LOG_DIR"

echo "[1/8] Build compact Cortex-M7 SpWKit"
cmake -S "$ROOT_DIR" -B "$BOARD_SPWKIT_BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DDAS_CORE=cm7 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$BOARD_SPWKIT_INSTALL" \
  -DBUILD_SHARED_LIBS=OFF \
  -DSPWKIT_ENABLE_HEAP=OFF \
  -DSPWKIT_BUILD_TESTS=OFF \
  -DSPWKIT_BUILD_CPP_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=OFF \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_SIMULATOR=OFF \
  -DSPWKIT_BUILD_UDP=OFF \
  -DSPWKIT_BUILD_DEVICE=OFF \
  -DSPWKIT_BUILD_VSPWD=OFF \
  -DSPWKIT_BUILD_TOOLS=OFF \
  -DSPWKIT_BUILD_CUSE=OFF \
  -DSPWKIT_ENABLE_CPP=OFF \
  -DSPWKIT_VSPW_PACKET_CAPACITY=4096 \
  -DSPWKIT_VSPW_CARRIER_CAPACITY=1500
cmake --build "$BOARD_SPWKIT_BUILD" --parallel
cmake --install "$BOARD_SPWKIT_BUILD"

echo "[2/8] Build pinned DAS"
cmake -S "$DAS_ROOT" -B "$DAS_BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DDAS_CORE=cm7 \
  -DDAS_DEVICE=nucleo_h755zi_q \
  -DSTM32_CUBE_H7_DIR="$STM32_CUBE_H7_DIR" \
  -DDAS_BUILD_HARDWARE_TESTS=OFF \
  -DDAS_BUILD_LINK_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$DAS_BUILD" --parallel
cmake --install "$DAS_BUILD" --prefix "$DAS_INSTALL"

echo "[3/8] Build STM32H755 SpWKit/DAS raw-Ethernet firmware"
cmake -S "$ROOT_DIR/integrations/das_stm32h755_raw_eth" -B "$FIRMWARE_BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DDAS_CORE=cm7 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$BOARD_SPWKIT_INSTALL;$DAS_INSTALL" \
  -DSPWKIT_DAS_HOST_MAC="$HOST_MAC"
cmake --build "$FIRMWARE_BUILD" --parallel
[[ -s "$ELF" ]] || { echo "Firmware not found: $ELF" >&2; exit 1; }
arm-none-eabi-nm -g "$ELF" | grep -q 'g_spwkit_das_raw_evidence'
arm-none-eabi-size "$ELF"

echo "[4/8] Build native compact SpWKit and AF_PACKET peer"
cmake -S "$ROOT_DIR" -B "$HOST_SPWKIT_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOST_SPWKIT_INSTALL" \
  -DBUILD_SHARED_LIBS=OFF \
  -DSPWKIT_BUILD_TESTS=OFF \
  -DSPWKIT_BUILD_CPP_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=OFF \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_SIMULATOR=OFF \
  -DSPWKIT_BUILD_UDP=OFF \
  -DSPWKIT_BUILD_DEVICE=OFF \
  -DSPWKIT_BUILD_VSPWD=OFF \
  -DSPWKIT_BUILD_TOOLS=OFF \
  -DSPWKIT_BUILD_CUSE=OFF \
  -DSPWKIT_ENABLE_HEAP=ON \
  -DSPWKIT_ENABLE_CPP=OFF \
  -DSPWKIT_VSPW_PACKET_CAPACITY=4096 \
  -DSPWKIT_VSPW_CARRIER_CAPACITY=1500
cmake --build "$HOST_SPWKIT_BUILD" --parallel
cmake --install "$HOST_SPWKIT_BUILD"
cmake -S "$ROOT_DIR/integrations/das_stm32h755_raw_eth/host" \
  -B "$HOST_PEER_BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOST_SPWKIT_INSTALL"
cmake --build "$HOST_PEER_BUILD" --parallel
[[ -x "$HOST_PEER" ]] || { echo "Host peer not found: $HOST_PEER" >&2; exit 1; }

echo "[5/8] Bring up Linux Ethernet interface $INTERFACE ($HOST_MAC)"
sudo ip link set dev "$INTERFACE" up

echo "[6/8] Start OpenOCD and flash/run CM7 firmware"
openocd -s "$OPENOCD_SCRIPTS" \
  -f "$ROOT_DIR/scripts/openocd_h755.cfg" \
  -c "init; reset halt" >"$OPENOCD_LOG" 2>&1 &
OPENOCD_PID=$!
for _ in $(seq 1 100); do
  grep -q "Listening on port 3333 for gdb connections" "$OPENOCD_LOG" 2>/dev/null && break
  kill -0 "$OPENOCD_PID" 2>/dev/null || {
    cat "$OPENOCD_LOG" >&2
    exit 1
  }
  sleep 0.1
done
grep -q "Listening on port 3333 for gdb connections" "$OPENOCD_LOG" || {
  cat "$OPENOCD_LOG" >&2
  echo "OpenOCD GDB server did not become ready" >&2
  exit 1
}
"$GDB_BIN" -q "$ELF" -batch \
  -x "$ROOT_DIR/scripts/gdb/stm32h755_das_raw_eth_start.gdb"

for _ in $(seq 1 100); do
  [[ "$(cat "/sys/class/net/$INTERFACE/carrier" 2>/dev/null || echo 0)" == "1" ]] && break
  sleep 0.1
done
[[ "$(cat "/sys/class/net/$INTERFACE/carrier" 2>/dev/null || echo 0)" == "1" ]] || {
  echo "No Ethernet carrier on $INTERFACE after board start" >&2
  exit 1
}

echo "[7/8] Run physical AF_PACKET/VSPW echo and RTT campaign"
sudo "$HOST_PEER" \
  --interface "$INTERFACE" \
  --iterations "$ITERATIONS" \
  --warmup "$WARMUP" | tee "$HOST_LOG"
grep -q '^HOST_RESULT: PASS$' "$HOST_LOG"

echo "[8/8] Read board evidence"
"$GDB_BIN" -q "$ELF" -batch \
  -x "$ROOT_DIR/scripts/gdb/stm32h755_das_raw_eth_evidence.gdb" 2>&1 | tee "$GDB_LOG"
grep -q '^RESULT: PASS "$GDB_LOG"

python3 "$ROOT_DIR/scripts/summarize_stm32h755_das_raw_eth.py" \
  --host "$HOST_LOG" \
  --board "$GDB_LOG" \
  --output "$SUMMARY_MD"

echo "STM32H755 DAS raw-Ethernet HIL: PASS"
echo "Host RTT evidence:  $HOST_LOG"
echo "Board evidence:     $GDB_LOG"
echo "Summary:            $SUMMARY_MD"
echo "OpenOCD log:        $OPENOCD_LOG"
