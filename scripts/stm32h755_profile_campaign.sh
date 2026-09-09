#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STM32_CUBE_H7_DIR="${STM32_CUBE_H7_DIR:-}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
BUILD_ROOT="${SPWKIT_STM32_PROFILE_BUILD_ROOT:-$ROOT_DIR/build/stm32-profile-campaign}"
OUTPUT_ROOT="${SPWKIT_STM32_PROFILE_OUTPUT_ROOT:-$ROOT_DIR/build/profile-results}"
DEBUG_TIMEOUT=90
WARMUP=16
ITERATIONS=64
CASES="all"
GDB_BIN=""
OPENOCD_PID=""
PINNED_CUBE_SHA="f5c0b7a2b1f6eb26fde150f72edb2d7deb647066"
PROFILE_CASES=(copied_tx copied_rx zc_tx_acquire zc_tx_submit zc_tx_reclaim zc_tx_release zc_rx_acquire zc_rx_release)

usage() {
  cat <<'USAGE'
Usage:
  scripts/stm32h755_profile_campaign.sh --stm32h7-root /path/to/STM32CubeH7 [options]

Runs the physical NUCLEO-H755ZI-Q profiling configurations strictly serially.
Each selected probe pair gets a clean Release SpWKit build, clean profiling
firmware build, flash/run cycle, DWT calibration, and debugger extraction.

Options:
  --stm32h7-root DIR     pinned STM32CubeH7 checkout root
  --cases "LIST"        all or space/comma-separated profile cases
  --warmup N            warmup iterations per payload (default: 16)
  --iterations N        measured iterations, 1..256 (default: 64)
  --build-root DIR       disposable build root
  --output-root DIR      result parent (default: build/profile-results)
  --openocd-scripts DIR  OpenOCD scripts directory
  --debug-timeout SEC    GDB timeout per case (default: 90)
  --list-cases           print supported profile cases
  -h, --help             show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stm32h7-root) STM32_CUBE_H7_DIR="$2"; shift 2 ;;
    --cases) CASES="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --build-root) BUILD_ROOT="$2"; shift 2 ;;
    --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
    --openocd-scripts) OPENOCD_SCRIPTS="$2"; shift 2 ;;
    --debug-timeout) DEBUG_TIMEOUT="$2"; shift 2 ;;
    --list-cases) printf '%s\n' "${PROFILE_CASES[@]}"; exit 0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for value in "$WARMUP" "$ITERATIONS" "$DEBUG_TIMEOUT"; do
  [[ "$value" =~ ^[0-9]+$ ]] || { echo "warmup, iterations and timeout must be integers" >&2; exit 2; }
done
(( ITERATIONS >= 1 && ITERATIONS <= 256 )) || { echo "iterations must be 1..256" >&2; exit 2; }
(( DEBUG_TIMEOUT >= 1 )) || { echo "debug-timeout must be positive" >&2; exit 2; }

need() {
  command -v "$1" >/dev/null 2>&1 || { echo "Missing command: $1" >&2; exit 2; }
}
for command in cmake git openocd arm-none-eabi-gcc arm-none-eabi-nm timeout tee grep python3 tar cp; do
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
for file in \
  "$STM32_CUBE_H7_DIR/Drivers/CMSIS/Include/core_cm7.h" \
  "$STM32_CUBE_H7_DIR/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h"; do
  [[ -f "$file" ]] || { echo "Incomplete STM32CubeH7 checkout: missing $file" >&2; exit 2; }
done

CUBE_SHA="unknown"
if git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD >/dev/null 2>&1; then
  CUBE_SHA="$(git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD)"
  if [[ "$CUBE_SHA" != "$PINNED_CUBE_SHA" ]]; then
    echo "WARNING: STM32CubeH7 is $CUBE_SHA; reference evidence is pinned to $PINNED_CUBE_SHA" >&2
  fi
fi

if [[ "$CASES" == "all" ]]; then
  selected_cases=("${PROFILE_CASES[@]}")
else
  CASES="${CASES//,/ }"
  read -r -a selected_cases <<< "$CASES"
fi
(( ${#selected_cases[@]} > 0 )) || { echo "at least one case is required" >&2; exit 2; }
for selected in "${selected_cases[@]}"; do
  found=0
  for known in "${PROFILE_CASES[@]}"; do [[ "$selected" == "$known" ]] && found=1; done
  (( found == 1 )) || { echo "unknown profile case: $selected" >&2; exit 2; }
done

case_config() {
  case "$1" in
    copied_tx)      CASE_ID=1; START=SPW_PROFILE_ID_TX_API_ENTRY; END=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY ;;
    copied_rx)      CASE_ID=2; START=SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY; END=SPW_PROFILE_ID_RX_API_RETURN ;;
    zc_tx_acquire)  CASE_ID=3; START=SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY; END=SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_RETURN ;;
    zc_tx_submit)   CASE_ID=4; START=SPW_PROFILE_ID_TX_ZC_SUBMIT_API_ENTRY; END=SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_BOUNDARY ;;
    zc_tx_reclaim)  CASE_ID=5; START=SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_BOUNDARY; END=SPW_PROFILE_ID_TX_ZC_RECLAIM_API_RETURN ;;
    zc_tx_release)  CASE_ID=6; START=SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY; END=SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN ;;
    zc_rx_acquire)  CASE_ID=7; START=SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY; END=SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_RETURN ;;
    zc_rx_release)  CASE_ID=8; START=SPW_PROFILE_ID_RX_ZC_RELEASE_API_ENTRY; END=SPW_PROFILE_ID_RX_ZC_RELEASE_API_RETURN ;;
    *) return 1 ;;
  esac
}

cleanup_openocd() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    kill "$OPENOCD_PID" >/dev/null 2>&1 || true
    wait "$OPENOCD_PID" >/dev/null 2>&1 || true
  fi
  OPENOCD_PID=""
}
trap cleanup_openocd EXIT INT TERM

git_sha="$(git -C "$ROOT_DIR" rev-parse HEAD)"
git_short="$(git -C "$ROOT_DIR" rev-parse --short=8 HEAD)"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
result_name="stm32h755-${timestamp}-${git_short}"
output_dir="$OUTPUT_ROOT/$result_name"
campaign_build="$BUILD_ROOT/$result_name"
mkdir -p "$output_dir/cases" "$output_dir/logs"
rm -rf -- "$campaign_build"
mkdir -p "$campaign_build"
compiler="$(arm-none-eabi-gcc --version | head -n 1)"

archive_results() {
  local archive="$1"
  rm -f -- "$archive"
  if ! tar -cf "$archive" -C "$(dirname "$output_dir")" "$(basename "$output_dir")"; then
    printf 'ERROR: tar failed while creating %s\n' "$archive" >&2
    rm -f -- "$archive"
    return 1
  fi
  if [[ ! -s "$archive" ]]; then
    printf 'ERROR: tar reported success but archive is missing/empty: %s\n' "$archive" >&2
    rm -f -- "$archive"
    return 1
  fi
  printf 'Archive       : %s\n' "$archive" >&2
}

fail_campaign() {
  local status="$1"
  shift
  cleanup_openocd
  printf '\nERROR: %s\n' "$*" >&2

  if [[ -n "${raw_build:-}" && -s "${raw_build:-}" && -n "${raw_out:-}" ]]; then
    cp "$raw_build" "$raw_out" || printf 'WARNING: could not preserve raw evidence at %s\n' "$raw_out" >&2
  fi

  if [[ -d "$output_dir" ]]; then
    local failure_archive="${output_dir}.failed.tar"
    printf 'Partial results : %s\n' "$output_dir" >&2
    if ! archive_results "$failure_archive"; then
      printf 'ERROR: automatic failure archive creation failed; results remain in %s\n' "$output_dir" >&2
    fi
  fi
  exit "$status"
}

printf 'SpWKit STM32H755 physical profiling campaign\n' >&2
printf '  commit: %s\n' "$git_sha" >&2
printf '  STM32CubeH7: %s\n' "$CUBE_SHA" >&2
printf '  build: Release, clean per probe pair, serial\n' >&2
printf '  counter: Cortex-M7 DWT CYCCNT at reset-clock 64 MHz\n' >&2
printf '  payloads: 0 1 8 64 256 bytes\n' >&2
printf '  cases: %s\n' "${selected_cases[*]}" >&2
printf '  results: %s\n' "$output_dir" >&2

for case_name in "${selected_cases[@]}"; do
  case_config "$case_name"
  case_root="$campaign_build/$case_name"
  spw_build="$case_root/spwkit"
  install_dir="$case_root/install"
  fw_build="$case_root/firmware"
  elf="$fw_build/spwkit_stm32h755_profile.elf"
  build_log="$output_dir/logs/${case_name}-build.log"
  openocd_log="$output_dir/logs/${case_name}-openocd.log"
  gdb_log="$output_dir/logs/${case_name}-gdb.log"
  raw_build="$case_root/stm32h755-profile.raw"
  raw_out="$output_dir/cases/${case_name}.raw"
  json_out="$output_dir/cases/${case_name}.json"

  printf '\n[STM32 profile] %s: %s -> %s\n' "$case_name" "$START" "$END" >&2
  rm -rf -- "$case_root"
  mkdir -p "$case_root"

  {
    cmake -S "$ROOT_DIR" -B "$spw_build" \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
      -DCMAKE_INSTALL_PREFIX="$install_dir" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSPWKIT_ENABLE_HEAP=OFF \
      -DSPWKIT_ENABLE_PROFILING=ON \
      -DSPWKIT_PROFILE_START="$START" \
      -DSPWKIT_PROFILE_END="$END" \
      -DSPWKIT_BUILD_TESTS=OFF \
      -DSPWKIT_BUILD_CPP_TESTS=OFF \
      -DSPWKIT_BUILD_EXAMPLES=OFF \
      -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
      -DSPWKIT_BUILD_SIMULATOR=OFF \
      -DSPWKIT_BUILD_UDP=OFF \
      -DSPWKIT_BUILD_DEVICE=OFF
    cmake --build "$spw_build" --parallel 2
    cmake --install "$spw_build"

    cmake -S "$ROOT_DIR/integrations/stm32h755_dma" -B "$fw_build" \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
      -DCMAKE_PREFIX_PATH="$install_dir" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSTM32_CUBE_H7_DIR="$STM32_CUBE_H7_DIR" \
      -DSPWKIT_STM32_PROFILE_FIRMWARE=ON \
      -DSPWKIT_SOURCE_DIR="$ROOT_DIR" \
      -DSPWKIT_STM32_PROFILE_START="$START" \
      -DSPWKIT_STM32_PROFILE_END="$END" \
      -DSPWKIT_STM32_PROFILE_CASE="$CASE_ID" \
      -DSPWKIT_STM32_PROFILE_WARMUP="$WARMUP" \
      -DSPWKIT_STM32_PROFILE_ITERATIONS="$ITERATIONS"
    cmake --build "$fw_build" --target spwkit_stm32h755_profile --parallel 2
  } >"$build_log" 2>&1 || {
    cat "$build_log" >&2
    fail_campaign 1 "Build failed for $case_name"
  }

  [[ -s "$elf" ]] || fail_campaign 1 "Profile ELF missing: $elf"
  arm-none-eabi-nm -g "$elf" | grep -q 'g_stm32h755_spwkit_profile' || \
    fail_campaign 1 "Profile ELF lacks g_stm32h755_spwkit_profile"

  cleanup_openocd
  openocd -s "$OPENOCD_SCRIPTS" \
    -f "$ROOT_DIR/scripts/openocd_h755.cfg" \
    -c "init; reset halt" >"$openocd_log" 2>&1 &
  OPENOCD_PID=$!
  for ((attempt = 0; attempt < 100; ++attempt)); do
    grep -q "Listening on port 3333 for gdb connections" "$openocd_log" 2>/dev/null && break
    if ! kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
      cat "$openocd_log" >&2
      fail_campaign 1 "OpenOCD exited before GDB server ready"
    fi
    sleep 0.1
  done
  if ! grep -q "Listening on port 3333 for gdb connections" "$openocd_log"; then
    cat "$openocd_log" >&2
    fail_campaign 1 "OpenOCD GDB server timeout"
  fi

  rm -f -- "$raw_build"
  set +e
  (
    cd "$case_root"
    timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$elf" -batch \
      -x "$ROOT_DIR/scripts/gdb/stm32h755_profile.gdb"
  ) 2>&1 | tee "$gdb_log"
  gdb_rc=${PIPESTATUS[0]}
  set -e
  cleanup_openocd

  [[ -s "$raw_build" ]] && cp "$raw_build" "$raw_out"

  if (( gdb_rc == 124 )); then
    fail_campaign 124 "GDB timed out after ${DEBUG_TIMEOUT}s for $case_name"
  fi
  if (( gdb_rc != 0 )) || ! grep -q '^RESULT: PASS$' "$gdb_log"; then
    status="$gdb_rc"
    (( status == 0 )) && status=1
    fail_campaign "$status" "Physical profile case $case_name failed"
  fi
  [[ -s "$raw_out" ]] || fail_campaign 1 "Bulk profile evidence dump missing for $case_name"

  python3 "$ROOT_DIR/scripts/extract_stm32h755_profile.py" "$gdb_log" \
    --raw "$raw_out" \
    --case "$case_name" --start "$START" --end "$END" \
    --git-sha "$git_sha" --cube-sha "$CUBE_SHA" --compiler "$compiler" \
    --output "$json_out" || fail_campaign $? "Profile extraction failed for $case_name"
done

export SPWKIT_STM32_RESULT_DIR="$output_dir"
export SPWKIT_STM32_RESULT_NAME="$result_name"
export SPWKIT_STM32_GIT_SHA="$git_sha"
export SPWKIT_STM32_CUBE_SHA="$CUBE_SHA"
export SPWKIT_STM32_COMPILER="$compiler"
export SPWKIT_STM32_CASES="${selected_cases[*]}"
export SPWKIT_STM32_WARMUP="$WARMUP"
export SPWKIT_STM32_ITERATIONS="$ITERATIONS"
python3 - <<'PY'
import json
import os
from pathlib import Path
root = Path(os.environ['SPWKIT_STM32_RESULT_DIR'])
meta = {
    'schema': 'spwkit.profile.stm32h755-campaign.v1',
    'result_type': 'physical-board',
    'result_directory_name': os.environ['SPWKIT_STM32_RESULT_NAME'],
    'git_sha': os.environ['SPWKIT_STM32_GIT_SHA'],
    'stm32cubeh7_sha': os.environ['SPWKIT_STM32_CUBE_SHA'],
    'compiler': os.environ['SPWKIT_STM32_COMPILER'],
    'board': 'NUCLEO-H755ZI-Q',
    'mcu': 'STM32H755ZI',
    'core': 'Cortex-M7',
    'core_hz': 64000000,
    'counter': 'DWT_CYCCNT',
    'build_type': 'Release',
    'clean_rebuild_per_case': True,
    'serial_execution': True,
    'payloads': [0, 1, 8, 64, 256],
    'warmup': int(os.environ['SPWKIT_STM32_WARMUP']),
    'iterations': int(os.environ['SPWKIT_STM32_ITERATIONS']),
    'cases': os.environ['SPWKIT_STM32_CASES'].split(),
    'counter_floor_subtracted': False,
    'scope': 'DMA2 memory-to-memory provider; not SpaceWire PHY/link timing',
}
(root / 'campaign.json').write_text(json.dumps(meta, indent=2) + '\n')
PY

printf '\n================ STM32H755 PROFILING RESULTS ================\n' >&2
python3 "$ROOT_DIR/scripts/summarize_stm32h755_profile.py" "$output_dir" | tee "$output_dir/summary.console.txt" >&2
printf '==============================================================\n' >&2
archive="${output_dir}.tar"
archive_results "$archive" || exit 1
printf 'Human summary : %s/summary.txt\n' "$output_dir" >&2
printf 'Machine data  : %s/cases/*.json\n' "$output_dir" >&2
printf 'Raw evidence  : %s/cases/*.raw\n' "$output_dir" >&2
printf '%s\n' "$output_dir"
