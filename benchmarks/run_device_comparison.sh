#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

direction=""
warmup="256"
iterations="1024"
payloads="0 1 8 64 256 1024 4096"
counter_hz="0"
output=""
calibration_output=""
build_dir="$ROOT_DIR/build/profile-device-comparison"
settle_seconds="1"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_device_comparison.sh --direction tx|rx [options]

Runs one clean Release Linux DEVICE/VSPD comparison build against a raw VSPD
SOCK_SEQPACKET client using the same vspwd daemon. A fresh daemon instance is
started for each payload size so daemon/protocol state cannot leak between
measurements.

Options:
  --warmup N            warmup iterations per payload (default: 256)
  --iterations N        measured iterations per payload, 1..4096
  --payloads "LIST"     space/comma-separated payload sizes, 0..4096
  --counter-hz N        optional architectural counter frequency override
  --output PATH         write comparison JSON Lines to PATH as well as stdout
  --calibration-output PATH
                        write standalone counter-floor diagnostic JSON
  --build-dir PATH      disposable CMake build directory
  --settle-seconds N    pause after build before timing (default: 1)
  -h, --help            show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --direction) direction="$2"; shift 2 ;;
    --warmup) warmup="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --payloads) payloads="$2"; shift 2 ;;
    --counter-hz) counter_hz="$2"; shift 2 ;;
    --output) output="$2"; shift 2 ;;
    --calibration-output) calibration_output="$2"; shift 2 ;;
    --build-dir) build_dir="$2"; shift 2 ;;
    --settle-seconds) settle_seconds="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "DEVICE/VSPD benchmark currently requires Linux" >&2
  exit 2
fi

case "$direction" in
  tx)
    # Select the opposite probe domain so measured TX internals remain no-ops.
    profile_start=SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
    profile_end=SPW_PROFILE_ID_RX_API_RETURN
    ;;
  rx)
    profile_start=SPW_PROFILE_ID_TX_API_ENTRY
    profile_end=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
    ;;
  *) echo "direction must be tx or rx" >&2; exit 2 ;;
esac

if ! [[ "$warmup" =~ ^[0-9]+$ && "$iterations" =~ ^[0-9]+$ && "$counter_hz" =~ ^[0-9]+$ && "$settle_seconds" =~ ^[0-9]+$ ]]; then
  echo "warmup, iterations, counter-hz and settle-seconds must be non-negative integers" >&2
  exit 2
fi
if (( iterations < 1 || iterations > 4096 )); then
  echo "iterations must be in the range 1..4096" >&2
  exit 2
fi

payloads="${payloads//,/ }"
read -r -a payload_array <<< "$payloads"
if (( ${#payload_array[@]} == 0 )); then
  echo "at least one payload size is required" >&2
  exit 2
fi
for payload in "${payload_array[@]}"; do
  if ! [[ "$payload" =~ ^[0-9]+$ ]] || (( payload > 4096 )); then
    echo "invalid payload size: $payload (expected 0..4096)" >&2
    exit 2
  fi
done

printf '[device-comparison] %s: removing build tree: %s\n' "$direction" "$build_dir" >&2
rm -rf -- "$build_dir"

cmake -S "$ROOT_DIR/benchmarks" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BENCHMARK_PROFILE_START="$profile_start" \
  -DSPWKIT_BENCHMARK_PROFILE_END="$profile_end" \
  -DSPWKIT_BENCHMARK_COUNTER_HZ="$counter_hz" \
  -DSPWKIT_BENCHMARK_BUILD_DEVICE=ON
cmake --build "$build_dir" \
  --target vspwd spwkit_profile_device_comparison spwkit_profile_counter_floor \
  --parallel 2

binary="$build_dir/spwkit_profile_device_comparison"
floor_binary="$build_dir/spwkit_profile_counter_floor"
daemon="$build_dir/spwkit-runtime/vspwd"
for executable in "$binary" "$floor_binary" "$daemon"; do
  if [[ ! -x "$executable" ]]; then
    echo "benchmark executable was not produced: $executable" >&2
    exit 1
  fi
done

if (( settle_seconds > 0 )); then
  printf '[device-comparison] %s: settling for %ss before timing\n' "$direction" "$settle_seconds" >&2
  sleep "$settle_seconds"
fi

calibration_line="$($floor_binary --warmup "$warmup" --iterations "$iterations")"
printf '[device-comparison] %s diagnostic counter floor: %s\n' "$direction" "$calibration_line" >&2
if [[ -n "$calibration_output" ]]; then
  mkdir -p "$(dirname "$calibration_output")"
  printf '%s\n' "$calibration_line" > "$calibration_output"
fi

if [[ -n "$output" ]]; then
  mkdir -p "$(dirname "$output")"
  : > "$output"
fi

for payload in "${payload_array[@]}"; do
  tmpdir="$(mktemp -d)"
  socket_path="$tmpdir/vspwd.sock"
  daemon_log="$tmpdir/vspwd.log"
  daemon_pid=""

  cleanup_daemon() {
    set +e
    if [[ -n "$daemon_pid" ]] && kill -0 "$daemon_pid" 2>/dev/null; then
      kill -TERM "$daemon_pid" 2>/dev/null || true
      wait "$daemon_pid" 2>/dev/null || true
    fi
    rm -rf "$tmpdir"
  }
  trap cleanup_daemon EXIT

  "$daemon" --socket "$socket_path" >"$daemon_log" 2>&1 &
  daemon_pid=$!
  for _ in $(seq 1 100); do
    [[ -S "$socket_path" ]] && break
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
      cat "$daemon_log" >&2 || true
      exit 1
    fi
    sleep 0.02
  done
  if [[ ! -S "$socket_path" ]]; then
    cat "$daemon_log" >&2 || true
    exit 1
  fi

  if ! line="$(timeout 30s "$binary" \
      --socket "$socket_path" \
      --direction "$direction" \
      --warmup "$warmup" \
      --iterations "$iterations" \
      --payload "$payload")"; then
    printf '[device-comparison] %s payload %s failed\n' "$direction" "$payload" >&2
    cat "$daemon_log" >&2 || true
    exit 1
  fi

  printf '%s\n' "$line"
  if [[ -n "$output" ]]; then
    printf '%s\n' "$line" >> "$output"
  fi

  cleanup_daemon
  trap - EXIT
done
