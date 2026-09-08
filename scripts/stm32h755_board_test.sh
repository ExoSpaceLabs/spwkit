#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STM32_CUBE_H7_DIR="${STM32_CUBE_H7_DIR:-}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
BUILD_ROOT="${SPWKIT_STM32_BUILD_ROOT:-$ROOT_DIR/build}"
DEBUG_TIMEOUT=60
# Official board evidence is clean-by-default. Reuse is available only as an
# explicit diagnostic convenience and is not the reproducible evidence path.
CLEAN=1
SKIP_BUILD=0
GDB_BIN=""
OPENOCD_PID=""
PINNED_CUBE_SHA="f5c0b7a2b1f6eb26fde150f72edb2d7deb647066"

usage() {
  cat <<'USAGE'
Usage:
  scripts/stm32h755_board_test.sh /path/to/STM32CubeH7 [options]
  scripts/stm32h755_board_test.sh --stm32h7-root /path/to/STM32CubeH7 [options]

Builds SpWKit and the STM32H755 DMA/cache evidence firmware from a clean tree
by default, flashes the NUCLEO-H755ZI-Q CM7 image through ST-LINK/OpenOCD,
runs the firmware, and checks g_stm32h755_spwkit_evidence through GDB.

Options:
  --stm32h7-root DIR     STM32CubeH7 checkout root.
  --build-root DIR       Build root (default: <repo>/build).
  --openocd-scripts DIR  OpenOCD scripts directory (default: /usr/share/openocd/scripts).
  --debug-timeout SEC    GDB session timeout (default: 60).
  --clean                Explicitly request the default clean rebuild.
  --reuse-build          Reconfigure/build without deleting existing build trees.
                         Diagnostic convenience only; not reproducible evidence.
  --no-build             Reuse an existing firmware ELF and only flash/test it.
                         Diagnostic convenience only; implies no clean.
  -h, --help             Show this help.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stm32h7-root) STM32_CUBE_H7_DIR="$2"; shift 2 ;;
    --build-root) BUILD_ROOT="$2"; shift 2 ;;
    --openocd-scripts) OPENOCD_SCRIPTS="$2"; shift 2 ;;
    --debug-timeout) DEBUG_TIMEOUT="$2"; shift 2 ;;
    --clean) CLEAN=1; shift ;;
    --reuse-build) CLEAN=0; shift ;;
    --no-build) SKIP_BUILD=1; CLEAN=0; shift ;;
    -h|--help) usage; exit 0 ;;
    --*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    *)
      [[ -z "$STM32_CUBE_H7_DIR" ]] || { echo "Unexpected argument: $1" >&2; exit 2; }
      STM32_CUBE_H7_DIR="$1"
      shift
      ;;
  esac
done

if (( SKIP_BUILD != 0 && CLEAN != 0 )); then
  echo "--no-build cannot be combined with --clean" >&2
  exit 2
fi

[[ "$DEBUG_TIMEOUT" =~ ^[0-9]+$ ]] && (( DEBUG_TIMEOUT > 0 )) || {
  echo "--debug-timeout must be a positive integer" >&2
  exit 2
}

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing command: $1" >&2
    exit 2
  }
}

for command in cmake git openocd arm-none-eabi-gcc arm-none-eabi-nm timeout tee grep; do
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

[[ -n "$STM32_CUBE_H7_DIR" ]] || { usage >&2; exit 2; }
STM32_CUBE_H7_DIR="$(cd "$STM32_CUBE_H7_DIR" 2>/dev/null && pwd)" || {
  echo "Invalid STM32CubeH7 root: $STM32_CUBE_H7_DIR" >&2
  exit 2
}

CMSIS_CORE="$STM32_CUBE_H7_DIR/Drivers/CMSIS/Include/core_cm7.h"
CMSIS_DEVICE="$STM32_CUBE_H7_DIR/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h"
for file in "$CMSIS_CORE" "$CMSIS_DEVICE"; do
  [[ -f "$file" ]] || {
    echo "Incomplete STM32CubeH7 checkout: missing $file" >&2
    exit 2
  }
done

if git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD >/dev/null 2>&1; then
  CUBE_SHA="$(git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD)"
  if [[ "$CUBE_SHA" != "$PINNED_CUBE_SHA" ]]; then
    echo "WARNING: STM32CubeH7 is $CUBE_SHA; SpWKit evidence is pinned to $PINNED_CUBE_SHA" >&2
  fi
fi

SPWKIT_BUILD="$BUILD_ROOT/stm32-spwkit"
INSTALL_DIR="$BUILD_ROOT/stm32-install"
FIRMWARE_BUILD="$BUILD_ROOT/stm32h755-dma"
ELF="$FIRMWARE_BUILD/spwkit_stm32h755_dma.elf"
LOG_DIR="$FIRMWARE_BUILD/board-test"
OPENOCD_LOG="$LOG_DIR/openocd.log"
GDB_LOG="$LOG_DIR/gdb.log"

cleanup_openocd() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    kill "$OPENOCD_PID" >/dev/null 2>&1 || true
    wait "$OPENOCD_PID" >/dev/null 2>&1 || true
  fi
  OPENOCD_PID=""
}
trap cleanup_openocd EXIT INT TERM

if (( CLEAN != 0 )); then
  echo "[board] removing prior STM32 build/install trees"
  rm -rf -- "$SPWKIT_BUILD" "$INSTALL_DIR" "$FIRMWARE_BUILD"
else
  echo "[board] WARNING: reusing build state; this run is diagnostic, not reproducible clean-build evidence" >&2
fi

if (( SKIP_BUILD == 0 )); then
  echo "[1/4] Cross-building SpWKit for Cortex-M7"
  cmake -S "$ROOT_DIR" -B "$SPWKIT_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSPWKIT_ENABLE_HEAP=OFF \
    -DSPWKIT_BUILD_TESTS=OFF \
    -DSPWKIT_BUILD_CPP_TESTS=OFF \
    -DSPWKIT_BUILD_EXAMPLES=OFF \
    -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
    -DSPWKIT_BUILD_SIMULATOR=OFF \
    -DSPWKIT_BUILD_UDP=OFF \
    -DSPWKIT_BUILD_DEVICE=OFF
  cmake --build "$SPWKIT_BUILD" --parallel
  cmake --install "$SPWKIT_BUILD"

  echo "[2/4] Building STM32H755 DMA/cache board evidence firmware"
  cmake -S "$ROOT_DIR/integrations/stm32h755_dma" -B "$FIRMWARE_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
    -DCMAKE_PREFIX_PATH="$INSTALL_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSTM32_CUBE_H7_DIR="$STM32_CUBE_H7_DIR"
  cmake --build "$FIRMWARE_BUILD" --parallel
else
  echo "[1/4] Build skipped"
  echo "[2/4] Reusing existing firmware"
fi

[[ -s "$ELF" ]] || {
  echo "Firmware ELF not found: $ELF" >&2
  exit 1
}
arm-none-eabi-nm -g "$ELF" | grep -q 'g_stm32h755_spwkit_evidence' || {
  echo "Firmware does not expose g_stm32h755_spwkit_evidence" >&2
  exit 1
}

mkdir -p "$LOG_DIR"
cleanup_openocd

echo "[3/4] Starting OpenOCD for NUCLEO-H755ZI-Q CM7"
openocd -s "$OPENOCD_SCRIPTS" \
  -f "$ROOT_DIR/scripts/openocd_h755.cfg" \
  -c "init; reset halt" >"$OPENOCD_LOG" 2>&1 &
OPENOCD_PID=$!

for ((attempt = 0; attempt < 100; ++attempt)); do
  if grep -q "Listening on port 3333 for gdb connections" "$OPENOCD_LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    cat "$OPENOCD_LOG" >&2
    echo "OpenOCD exited before the GDB server became ready" >&2
    exit 1
  fi
  sleep 0.1
done

if ! grep -q "Listening on port 3333 for gdb connections" "$OPENOCD_LOG" 2>/dev/null; then
  cat "$OPENOCD_LOG" >&2
  echo "OpenOCD GDB server timeout" >&2
  exit 1
fi

echo "[4/4] Flashing, running, and checking board evidence with $GDB_BIN"
set +e
timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$ELF" -batch \
  -x "$ROOT_DIR/scripts/gdb/stm32h755_board_test.gdb" 2>&1 | tee "$GDB_LOG"
GDB_RC=${PIPESTATUS[0]}
set -e
cleanup_openocd

if (( GDB_RC != 0 )); then
  echo "Board test GDB session failed with exit code $GDB_RC" >&2
  exit "$GDB_RC"
fi
if ! grep -q '^RESULT: PASS$' "$GDB_LOG"; then
  echo "STM32H755 board evidence did not satisfy the SpWKit acceptance contract" >&2
  exit 1
fi

echo "STM32H755 board test: PASS"
echo "Evidence log: $GDB_LOG"
