#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

warmup="256"
iterations="1024"
payloads="0 1 8 64 256 1024 4096"
counter_hz="0"
output=""
calibration_output=""
build_dir="$ROOT_DIR/build/profile-native-comparison"
settle_seconds="1"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_native_comparison.sh [options]

Runs one clean Release build of the direct/native versus SpWKit copied DRIVER
comparison. Both paths execute in the same process, share the same provider
submission function and alternate order per measured iteration.

Options:
  --warmup N            warmup iterations per payload (default: 256)
  --iterations N        measured iterations per path/payload, 1..4096
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

printf '[native-comparison] removing build tree: %s\n' "$build_dir" >&2
rm -rf -- "$build_dir"

cmake -S "$ROOT_DIR/benchmarks" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BENCHMARK_PROFILE_START=SPW_PROFILE_ID_TX_API_ENTRY \
  -DSPWKIT_BENCHMARK_PROFILE_END=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY \
  -DSPWKIT_BENCHMARK_COUNTER_HZ="$counter_hz"
cmake --build "$build_dir" \
  --target spwkit_profile_native_comparison spwkit_profile_counter_floor \
  --parallel 2

binary="$build_dir/spwkit_profile_native_comparison"
floor_binary="$build_dir/spwkit_profile_counter_floor"
for executable in "$binary" "$floor_binary"; do
  if [[ ! -x "$executable" ]]; then
    echo "comparison executable was not produced: $executable" >&2
    exit 1
  fi
done

if (( settle_seconds > 0 )); then
  printf '[native-comparison] settling for %ss before timing\n' "$settle_seconds" >&2
  sleep "$settle_seconds"
fi

calibration_line="$($floor_binary --warmup "$warmup" --iterations "$iterations")"
printf '[native-comparison] diagnostic counter floor: %s\n' "$calibration_line" >&2
if [[ -n "$calibration_output" ]]; then
  mkdir -p "$(dirname "$calibration_output")"
  printf '%s\n' "$calibration_line" > "$calibration_output"
fi

if [[ -n "$output" ]]; then
  mkdir -p "$(dirname "$output")"
  : > "$output"
fi

for payload in "${payload_array[@]}"; do
  line="$($binary --warmup "$warmup" --iterations "$iterations" --payload "$payload")"
  printf '%s\n' "$line"
  if [[ -n "$output" ]]; then
    printf '%s\n' "$line" >> "$output"
  fi
done
