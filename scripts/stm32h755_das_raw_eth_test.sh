#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAS_ROOT="${DAS_ROOT:-$ROOT_DIR/thirdparty/device-abstraction-stack}"
STM32_CUBE_H7_DIR="${STM32_CUBE_H7_DIR:-$ROOT_DIR/thirdparty/STM32CubeH7}"
INTERFACE=""
BUILD_ROOT="${SPWKIT_DAS_RAW_BUILD_ROOT:-$ROOT_DIR/build/das-raw-eth}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
ITERATIONS=128
WARMUP=16
GDB_BIN=""
OPENOCD_PID=""

PINNED_DAS_SHA="4b768ef86b43652c94cc91b1c77e247fa37ebd8a"
PINNED_CUBE_SHA="f5c0b7a2b1f6eb26fde150f72edb2d7deb647066"
source "$ROOT_DIR/scripts/lib/thirdparty.sh"

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/stm32h755_das_raw_eth_test.sh \
    --interface enpXsY [options]

Builds a compact no-heap SpWKit raw-Ethernet backend, DAS for the
NUCLEO-H755ZI-Q CM7, the board responder firmware, and a Linux AF_PACKET peer.
It then flashes the board, runs physical Ethernet echo/RTT traffic, and checks
the debugger-visible board evidence.

Options:
  --das-root DIR          Optional DAS checkout root.
                          Default: <repo>/thirdparty/device-abstraction-stack (auto-managed).
  --stm32h7-root DIR      Optional STM32CubeH7 checkout root.
                          Default: <repo>/thirdparty/STM32CubeH7 (auto-managed).
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
               arm-none-eabi-size arm-none-eabi-addr2line ip sudo timeout tee grep python3; do
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
[[ -n "$INTERFACE" ]] || {
  usage >&2
  exit 2
}

spwkit_prepare_das "$DAS_ROOT" "$PINNED_DAS_SHA"
spwkit_prepare_stm32cubeh7 "$STM32_CUBE_H7_DIR" "$PINNED_CUBE_SHA"
DAS_SHA="$(git -C "$DAS_ROOT" rev-parse HEAD)"
CUBE_SHA="$(git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD)"

[[ -d "/sys/class/net/$INTERFACE" ]] || {
  echo "Network interface not found: $INTERFACE" >&2
  exit 2
}

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
RUN_LOG_DIR="$BUILD_ROOT/logs"
BUILD_BOARD_LOG="$RUN_LOG_DIR/01-board-spwkit.log"
BUILD_DAS_LOG="$RUN_LOG_DIR/02-das.log"
BUILD_FIRMWARE_LOG="$RUN_LOG_DIR/03-firmware.log"
BUILD_HOST_LOG="$RUN_LOG_DIR/04-host.log"
FLASH_LOG="$RUN_LOG_DIR/06-flash.log"

status() {
  printf '[HIL] %s\n' "$*"
}

pass() {
  printf '[HIL] PASS: %s\n' "$*"
}

fail_with_log() {
  local message="$1"
  local log="$2"
  printf '[HIL] FAIL: %s\n' "$message" >&2
  if [[ -f "$log" ]]; then
    printf '[HIL] ---- last 60 log lines: %s ----\n' "$log" >&2
    tail -n 60 "$log" >&2
  fi
  exit 1
}

net_stat() {
  cat "/sys/class/net/$INTERFACE/statistics/$1"
}

board_phase_name() {
  case "$1" in
    0x00000000) echo "reset / not started" ;;
    0x00000001) echo "board clocks/time initialization" ;;
    0x00000002) echo "Ethernet initialized; SpWKit port setup" ;;
    0x00000003) echo "physical link up; waiting for VSPW RUN" ;;
    0x00000004) echo "VSPW RUN; payload echo loop" ;;
    0x0000700d) echo "test complete" ;;
    0xdead0101) echo "board LED/clock/time initialization failed" ;;
    0xdead0102) echo "DAS Ethernet initialization failed" ;;
    0xdead0201) echo "SpWKit workspace requirements failed" ;;
    0xdead0202) echo "SpWKit port open/capabilities failed" ;;
    0xdead0203) echo "SpWKit port start failed" ;;
    0xdead0301) echo "VSPW failed to reach RUN" ;;
    0xdead0302) echo "physical Ethernet link unavailable" ;;
    0xdead0401) echo "SpWKit receive failed" ;;
    0xdead0402) echo "SpWKit statistics read failed" ;;
    0xdead0403) echo "SpWKit echo send failed" ;;
    *) echo "unknown phase" ;;
  esac
}

cleanup() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" 2>/dev/null; then
    kill "$OPENOCD_PID" 2>/dev/null || true
    wait "$OPENOCD_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

rm -rf -- "$BUILD_ROOT"
mkdir -p "$LOG_DIR" "$RUN_LOG_DIR"

status "SpWKit STM32H755 raw-Ethernet HIL"
status "interface=$INTERFACE host_mac=$HOST_MAC"
status "DAS revision=$DAS_SHA"
status "STM32CubeH7 revision=$CUBE_SHA"
status "full build/debug logs: $RUN_LOG_DIR"

status "[1/8] Build compact Cortex-M7 SpWKit"
if ! ( set -e
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

) >"$BUILD_BOARD_LOG" 2>&1; then
  fail_with_log "Cortex-M7 SpWKit build failed" "$BUILD_BOARD_LOG"
fi
pass "[1/8] Cortex-M7 SpWKit built"

status "[2/8] Build pinned DAS"
if ! ( set -e
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

) >"$BUILD_DAS_LOG" 2>&1; then
  fail_with_log "pinned DAS build failed" "$BUILD_DAS_LOG"
fi
pass "[2/8] DAS built"

status "[3/8] Build STM32H755 SpWKit/DAS raw-Ethernet firmware"
if ! ( set -e
cmake -S "$ROOT_DIR/integrations/das_stm32h755_raw_eth" -B "$FIRMWARE_BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DDAS_CORE=cm7 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$BOARD_SPWKIT_INSTALL;$DAS_INSTALL" \
  -DSPWKIT_DAS_HOST_MAC="$HOST_MAC"
cmake --build "$FIRMWARE_BUILD" --parallel
[[ -s "$ELF" ]] || { echo "Firmware not found: $ELF" >&2; exit 1; }
arm-none-eabi-nm -g "$ELF" | grep -q 'g_spwkit_das_raw_evidence'

) >"$BUILD_FIRMWARE_LOG" 2>&1; then
  fail_with_log "STM32H755 HIL firmware build failed" "$BUILD_FIRMWARE_LOG"
fi
pass "[3/8] firmware built: $(arm-none-eabi-size "$ELF" | tail -n 1)"

status "[4/8] Build native compact SpWKit and AF_PACKET peer"
if ! ( set -e
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

) >"$BUILD_HOST_LOG" 2>&1; then
  fail_with_log "Linux AF_PACKET peer build failed" "$BUILD_HOST_LOG"
fi
pass "[4/8] host AF_PACKET peer built"

status "[5/8] Bring up Linux Ethernet interface $INTERFACE ($HOST_MAC)"
sudo ip link set dev "$INTERFACE" up
status "host link: $(ip -br link show dev "$INTERFACE")"
status "carrier(before board start)=$(cat "/sys/class/net/$INTERFACE/carrier" 2>/dev/null || echo 0)"
pass "[5/8] host interface prepared"

status "[6/8] Start OpenOCD and flash/run CM7 firmware"
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
if ! "$GDB_BIN" -q "$ELF" -batch \
  -x "$ROOT_DIR/scripts/gdb/stm32h755_das_raw_eth_start.gdb" >"$FLASH_LOG" 2>&1; then
  fail_with_log "flash/start GDB sequence failed" "$FLASH_LOG"
fi

for _ in $(seq 1 100); do
  [[ "$(cat "/sys/class/net/$INTERFACE/carrier" 2>/dev/null || echo 0)" == "1" ]] && break
  sleep 0.1
done
[[ "$(cat "/sys/class/net/$INTERFACE/carrier" 2>/dev/null || echo 0)" == "1" ]] || {
  fail_with_log "no Ethernet carrier on $INTERFACE after board start" "$OPENOCD_LOG"
}
status "carrier(after board start)=1"
pass "[6/8] firmware flashed and physical Ethernet carrier detected"

HOST_TX_PACKETS_BEFORE="$(net_stat tx_packets)"
HOST_RX_PACKETS_BEFORE="$(net_stat rx_packets)"
HOST_TX_BYTES_BEFORE="$(net_stat tx_bytes)"
HOST_RX_BYTES_BEFORE="$(net_stat rx_bytes)"

status "[7/8] Run physical AF_PACKET/VSPW echo and RTT campaign"
set +e
sudo "$HOST_PEER" \
  --interface "$INTERFACE" \
  --iterations "$ITERATIONS" \
  --warmup "$WARMUP" 2>&1 | tee "$HOST_LOG"
HOST_RC=${PIPESTATUS[0]}
set -e
HOST_TX_PACKETS_AFTER="$(net_stat tx_packets)"
HOST_RX_PACKETS_AFTER="$(net_stat rx_packets)"
HOST_TX_BYTES_AFTER="$(net_stat tx_bytes)"
HOST_RX_BYTES_AFTER="$(net_stat rx_bytes)"
status "host NIC delta: tx_packets=$((HOST_TX_PACKETS_AFTER-HOST_TX_PACKETS_BEFORE)) rx_packets=$((HOST_RX_PACKETS_AFTER-HOST_RX_PACKETS_BEFORE)) tx_bytes=$((HOST_TX_BYTES_AFTER-HOST_TX_BYTES_BEFORE)) rx_bytes=$((HOST_RX_BYTES_AFTER-HOST_RX_BYTES_BEFORE))"
if (( HOST_RC == 0 )); then
  pass "[7/8] VSPW raw-Ethernet campaign completed"
else
  status "[7/8] VSPW campaign failed; collecting board evidence before exit"
fi

status "[8/8] Read board/fault evidence"
set +e
"$GDB_BIN" -q "$ELF" -batch \
  -x "$ROOT_DIR/scripts/gdb/stm32h755_das_raw_eth_evidence.gdb" >"$GDB_LOG" 2>&1
GDB_RC=$?
set -e

if [[ -f "$GDB_LOG" ]]; then
  grep -E '^(halt_pc|halt_lr|halt_xpsr|halt_msp|halt_psp|fault_sp|stacked_r0|stacked_r1|stacked_r2|stacked_r3|stacked_r12|stacked_lr|stacked_pc|stacked_xpsr|scb_cfsr|scb_hfsr|scb_mmfar|scb_bfar|magic|phase|result|workspace_bytes|max_packet_size|link_speed_mbps|link_duplex|das_tx_count|das_rx_poll_count|das_rx_success_count|das_rx_empty_polls)=' "$GDB_LOG" || true
  grep -E '^RESULT:' "$GDB_LOG" || true

  BOARD_PHASE="$(sed -n 's/^phase=//p' "$GDB_LOG" | tail -n 1)"
  HALT_PC="$(sed -n 's/^halt_pc=//p' "$GDB_LOG" | tail -n 1)"
  STACKED_PC="$(sed -n 's/^stacked_pc=//p' "$GDB_LOG" | tail -n 1)"
  if [[ -n "$BOARD_PHASE" ]]; then
    status "board phase: $BOARD_PHASE ($(board_phase_name "$BOARD_PHASE"))"
  fi
  if [[ -n "$HALT_PC" ]]; then
    HALT_LOCATION="$(arm-none-eabi-addr2line -f -C -e "$ELF" "$HALT_PC" | paste -sd ' ' -)"
    status "MCU handler location: $HALT_LOCATION"
  fi
  if [[ -n "$STACKED_PC" ]]; then
    STACKED_LOCATION="$(arm-none-eabi-addr2line -f -C -e "$ELF" "$STACKED_PC" | paste -sd ' ' -)"
    status "MCU faulting instruction: $STACKED_LOCATION"
  fi
fi

if (( HOST_RC != 0 )); then
  echo "[HIL] FAIL: host VSPW campaign exited $HOST_RC" >&2
  echo "[HIL] inspect: $HOST_LOG" >&2
  echo "[HIL] inspect: $GDB_LOG" >&2
  echo "[HIL] inspect: $OPENOCD_LOG" >&2
  exit "$HOST_RC"
fi
if (( GDB_RC != 0 )) || ! grep -q '^RESULT: PASS$' "$GDB_LOG"; then
  echo "[HIL] FAIL: board evidence did not satisfy the HIL contract" >&2
  echo "[HIL] inspect: $HOST_LOG" >&2
  echo "[HIL] inspect: $GDB_LOG" >&2
  echo "[HIL] inspect: $OPENOCD_LOG" >&2
  exit 1
fi

python3 "$ROOT_DIR/scripts/summarize_stm32h755_das_raw_eth.py" \
  --host "$HOST_LOG" \
  --board "$GDB_LOG" \
  --output "$SUMMARY_MD"

pass "[8/8] board evidence validated"
echo
echo "========== HIL RESULT =========="
echo "STM32H755 DAS raw-Ethernet HIL: PASS"
echo "Performance summary: $SUMMARY_MD"
echo "Host RTT evidence:   $HOST_LOG"
echo "Board evidence:      $GDB_LOG"
echo "OpenOCD log:         $OPENOCD_LOG"
echo "Build/debug logs:    $RUN_LOG_DIR"
echo "================================"
