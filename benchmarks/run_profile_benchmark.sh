#!/usr/bin/env bash
set -euo pipefail

range="tx_api_native"
warmup="256"
iterations="1024"
payloads="0 1 8 64 256 1024 4096"
counter_hz="0"
output=""
build_dir="build/profile-benchmark"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_profile_benchmark.sh [options]

Options:
  --range NAME          tx_api_backend | tx_backend_provider |
                        tx_provider_native | tx_api_native
  --warmup N            warmup iterations per payload (default: 256)
  --iterations N        measured iterations per payload, 1..4096 (default: 1024)
  --payloads "LIST"     space/comma-separated payload sizes, 0..4096
  --counter-hz N        optional architectural counter frequency override
  --output PATH         write JSON Lines to PATH as well as stdout
  --build-dir PATH      CMake build directory
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --range) range="$2"; shift 2 ;;
    --warmup) warmup="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --payloads) payloads="$2"; shift 2 ;;
    --counter-hz) counter_hz="$2"; shift 2 ;;
    --output) output="$2"; shift 2 ;;
    --build-dir) build_dir="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$range" in
  tx_api_backend)
    start="SPW_PROFILE_ID_TX_API_ENTRY"
    end="SPW_PROFILE_ID_TX_BACKEND_ENTRY"
    ;;
  tx_backend_provider)
    start="SPW_PROFILE_ID_TX_BACKEND_ENTRY"
    end="SPW_PROFILE_ID_TX_PROVIDER_ENTRY"
    ;;
  tx_provider_native)
    start="SPW_PROFILE_ID_TX_PROVIDER_ENTRY"
    end="SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY"
    ;;
  tx_api_native)
    start="SPW_PROFILE_ID_TX_API_ENTRY"
    end="SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY"
    ;;
  *)
    echo "Unsupported benchmark range: $range" >&2
    usage >&2
    exit 2
    ;;
esac

if ! [[ "$warmup" =~ ^[0-9]+$ && "$iterations" =~ ^[0-9]+$ && "$counter_hz" =~ ^[0-9]+$ ]]; then
  echo "warmup, iterations and counter-hz must be non-negative integers" >&2
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

cmake -S benchmarks -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BENCHMARK_PROFILE_START="$start" \
  -DSPWKIT_BENCHMARK_PROFILE_END="$end" \
  -DSPWKIT_BENCHMARK_COUNTER_HZ="$counter_hz"
cmake --build "$build_dir" --target spwkit_profile_benchmark --parallel 2

binary="$build_dir/spwkit_profile_benchmark"
if [[ ! -x "$binary" ]]; then
  echo "benchmark executable was not produced: $binary" >&2
  exit 1
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
