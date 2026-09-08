#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=benchmarks/profile_cases.sh
source "$ROOT_DIR/benchmarks/profile_cases.sh"

range="tx_api_native"
warmup="256"
iterations="1024"
payloads="0 1 8 64 256 1024 4096"
counter_hz="0"
output=""
build_dir="$ROOT_DIR/build/profile-benchmark"
settle_seconds="1"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_profile_benchmark.sh [options]

Runs exactly one compiled profiling configuration. The build directory is
ALWAYS removed before configuration so an official measurement never reuses a
CMake cache or object files from another probe pair.

Options:
  --range NAME          one case from benchmarks/profile_cases.sh
  --warmup N            warmup iterations per payload (default: 256)
  --iterations N        measured iterations per payload, 1..4096 (default: 1024)
  --payloads "LIST"     space/comma-separated payload sizes, 0..4096
  --counter-hz N        optional architectural counter frequency override
  --output PATH         write JSON Lines to PATH as well as stdout
  --build-dir PATH      disposable CMake build directory
  --settle-seconds N    pause after build before timing (default: 1)
  --list-ranges         print supported cases and exit
  -h, --help            show this help
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
    --settle-seconds) settle_seconds="$2"; shift 2 ;;
    --list-ranges) spw_profile_case_list; exit 0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if ! spw_profile_case_resolve "$range"; then
  echo "Unsupported benchmark range: $range" >&2
  echo "Supported ranges:" >&2
  spw_profile_case_list >&2
  exit 2
fi
start="$SPW_PROFILE_CASE_START"
end="$SPW_PROFILE_CASE_END"

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

# Build isolation is part of the measurement contract. This is intentionally
# unconditional. A developer wanting incremental builds should use a normal
# build tree, not the benchmark runner.
echo "[benchmark:$range] removing build tree: $build_dir" >&2
rm -rf -- "$build_dir"

cmake -S "$ROOT_DIR/benchmarks" -B "$build_dir" \
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

if (( settle_seconds > 0 )); then
  echo "[benchmark:$range] settling for ${settle_seconds}s before timing" >&2
  sleep "$settle_seconds"
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
