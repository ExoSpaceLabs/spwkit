#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=benchmarks/profile_cases.sh
source "$ROOT_DIR/benchmarks/profile_cases.sh"

cases="all"
warmup="256"
iterations="1024"
payloads="0 1 8 64 256 1024 4096"
counter_hz="0"
settle_seconds="1"
result_type="host"
build_root="$ROOT_DIR/build/profile-campaign"
output_root="$ROOT_DIR/build/profile-results"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_profile_campaign.sh [options]

Runs profiling configurations strictly one at a time. Every case receives a
fresh build directory and therefore a fresh CMake configure/build before its
counter-floor calibration and payload sweep. After the layer cases, the
campaign performs separate clean direct/native-versus-SpWKit TX and RX DRIVER
comparison builds.

Child benchmarks write machine-readable JSON into the result set but the
campaign terminal output stays human-readable and ends with a consolidated
numeric summary.

Results are stored under:
  <output-root>/<type>-<UTC timestamp>-<short git commit>/

Options:
  --cases "LIST"        all, or space/comma-separated case names (default: all)
  --warmup N            warmup iterations per payload (default: 256)
  --iterations N        measured iterations per payload (default: 1024)
  --payloads "LIST"     payload sizes in bytes (default: 0 1 8 64 256 1024 4096)
  --counter-hz N        optional architectural counter frequency override
  --settle-seconds N    pause after each clean build before timing (default: 1)
  --type NAME           result target/type, e.g. host, github-hosted, stm32h755
                        (default: host)
  --build-root PATH     disposable parent for per-campaign/per-case build trees
  --output-root PATH    parent directory for uniquely named result folders
  --list-cases          print supported cases and exit
  -h, --help            show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cases) cases="$2"; shift 2 ;;
    --warmup) warmup="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --payloads) payloads="$2"; shift 2 ;;
    --counter-hz) counter_hz="$2"; shift 2 ;;
    --settle-seconds) settle_seconds="$2"; shift 2 ;;
    --type) result_type="$2"; shift 2 ;;
    --build-root) build_root="$2"; shift 2 ;;
    --output-root) output_root="$2"; shift 2 ;;
    --list-cases) spw_profile_case_list; exit 0 ;;
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
if ! [[ "$result_type" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]]; then
  echo "type must contain only letters, digits, '.', '_' or '-' and start with an alphanumeric character" >&2
  exit 2
fi

if [[ "$cases" == "all" ]]; then
  selected_cases=("${SPWKIT_PROFILE_CASES[@]}")
else
  cases="${cases//,/ }"
  read -r -a selected_cases <<< "$cases"
fi
if (( ${#selected_cases[@]} == 0 )); then
  echo "at least one profiling case is required" >&2
  exit 2
fi
for case_name in "${selected_cases[@]}"; do
  if ! spw_profile_case_known "$case_name"; then
    echo "unknown profiling case: $case_name" >&2
    echo "Supported cases:" >&2
    spw_profile_case_list >&2
    exit 2
  fi
done

git_sha="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || printf unknown)"
git_short_sha="$(git -C "$ROOT_DIR" rev-parse --short=8 HEAD 2>/dev/null || printf unknown)"
timestamp_utc="$(date -u +%Y%m%dT%H%M%SZ)"
result_dir_name="${result_type}-${timestamp_utc}-${git_short_sha}"
output_dir="$output_root/$result_dir_name"
campaign_build_root="$build_root/$result_dir_name"

rm -rf -- "$campaign_build_root"
if [[ -e "$output_dir" ]]; then
  echo "result directory already exists: $output_dir" >&2
  exit 1
fi
mkdir -p "$campaign_build_root" \
         "$output_dir/cases" \
         "$output_dir/calibration" \
         "$output_dir/comparison"
: > "$output_dir/results.jsonl"
: > "$output_dir/calibration.jsonl"

printf 'SpWKit profiling campaign\n' >&2
printf '  type: %s\n' "$result_type" >&2
printf '  timestamp UTC: %s\n' "$timestamp_utc" >&2
printf '  commit: %s (%s)\n' "$git_sha" "$git_short_sha" >&2
printf '  result directory: %s\n' "$output_dir" >&2
printf '  cases: %s\n' "${selected_cases[*]}" >&2
printf '  build profile: Release\n' >&2
printf '  clean rebuild per case: yes\n' >&2
printf '  serial cases: yes\n' >&2
printf '  counter-floor calibration per case: yes\n' >&2
printf '  direct/native comparison: DRIVER copied TX + RX (separate clean builds)\n' >&2

case_index=0
for case_name in "${selected_cases[@]}"; do
  case_index=$((case_index + 1))
  case_build="$campaign_build_root/$case_name"
  case_output="$output_dir/cases/$case_name.jsonl"
  calibration_output="$output_dir/calibration/$case_name.json"
  printf '\n[campaign %d/%d] %s\n' "$case_index" "${#selected_cases[@]}" "$case_name" >&2

  "$ROOT_DIR/benchmarks/run_profile_benchmark.sh" \
    --range "$case_name" \
    --warmup "$warmup" \
    --iterations "$iterations" \
    --payloads "$payloads" \
    --counter-hz "$counter_hz" \
    --settle-seconds "$settle_seconds" \
    --build-dir "$case_build" \
    --output "$case_output" \
    --calibration-output "$calibration_output" \
    > /dev/null

  cat "$case_output" >> "$output_dir/results.jsonl"
  cat "$calibration_output" >> "$output_dir/calibration.jsonl"
done

tx_comparison_output="$output_dir/comparison/tx_api_native.jsonl"
tx_comparison_calibration="$output_dir/comparison/tx_calibration.json"
printf '\n[campaign comparison] direct/native vs SpWKit copied DRIVER TX\n' >&2
"$ROOT_DIR/benchmarks/run_native_comparison.sh" \
  --warmup "$warmup" \
  --iterations "$iterations" \
  --payloads "$payloads" \
  --counter-hz "$counter_hz" \
  --settle-seconds "$settle_seconds" \
  --build-dir "$campaign_build_root/native-tx-comparison" \
  --output "$tx_comparison_output" \
  --calibration-output "$tx_comparison_calibration" \
  > /dev/null

rx_comparison_output="$output_dir/comparison/rx_native_api.jsonl"
rx_comparison_calibration="$output_dir/comparison/rx_calibration.json"
printf '\n[campaign comparison] direct/native vs SpWKit copied DRIVER RX\n' >&2
bash "$ROOT_DIR/benchmarks/run_native_receive_comparison.sh" \
  --warmup "$warmup" \
  --iterations "$iterations" \
  --payloads "$payloads" \
  --counter-hz "$counter_hz" \
  --settle-seconds "$settle_seconds" \
  --build-dir "$campaign_build_root/native-rx-comparison" \
  --output "$rx_comparison_output" \
  --calibration-output "$rx_comparison_calibration" \
  > /dev/null

export SPWKIT_CAMPAIGN_CASES="${selected_cases[*]}"
export SPWKIT_CAMPAIGN_WARMUP="$warmup"
export SPWKIT_CAMPAIGN_ITERATIONS="$iterations"
export SPWKIT_CAMPAIGN_PAYLOADS="$payloads"
export SPWKIT_CAMPAIGN_COUNTER_HZ="$counter_hz"
export SPWKIT_CAMPAIGN_SETTLE_SECONDS="$settle_seconds"
export SPWKIT_CAMPAIGN_OUTPUT_DIR="$output_dir"
export SPWKIT_CAMPAIGN_RESULT_TYPE="$result_type"
export SPWKIT_CAMPAIGN_TIMESTAMP_UTC="$timestamp_utc"
export SPWKIT_CAMPAIGN_GIT_SHA="$git_sha"
export SPWKIT_CAMPAIGN_GIT_SHORT_SHA="$git_short_sha"
export SPWKIT_CAMPAIGN_RESULT_DIR_NAME="$result_dir_name"

python3 - <<'PY'
import json
import os
from pathlib import Path

out = Path(os.environ['SPWKIT_CAMPAIGN_OUTPUT_DIR'])
metadata = {
    'schema': 'spwkit.profile.campaign.v1',
    'result_type': os.environ['SPWKIT_CAMPAIGN_RESULT_TYPE'],
    'timestamp_utc': os.environ['SPWKIT_CAMPAIGN_TIMESTAMP_UTC'],
    'git_sha': os.environ['SPWKIT_CAMPAIGN_GIT_SHA'],
    'git_short_sha': os.environ['SPWKIT_CAMPAIGN_GIT_SHORT_SHA'],
    'result_directory_name': os.environ['SPWKIT_CAMPAIGN_RESULT_DIR_NAME'],
    'build_type': 'Release',
    'clean_rebuild_per_case': True,
    'serial_execution': True,
    'counter_floor_calibration_per_case': True,
    'direct_native_comparison': True,
    'direct_native_comparison_case': 'tx_api_native',
    'direct_native_comparison_cases': ['tx_api_native', 'rx_native_api'],
    'direct_native_counter_floor_subtracted': False,
    'summary_file': 'summary.txt',
    'coverage_file': 'coverage.json',
    'cases': os.environ['SPWKIT_CAMPAIGN_CASES'].split(),
    'warmup_iterations': int(os.environ['SPWKIT_CAMPAIGN_WARMUP']),
    'measured_iterations': int(os.environ['SPWKIT_CAMPAIGN_ITERATIONS']),
    'payloads': os.environ['SPWKIT_CAMPAIGN_PAYLOADS'],
    'counter_hz_override': int(os.environ['SPWKIT_CAMPAIGN_COUNTER_HZ']),
    'settle_seconds_after_build': int(os.environ['SPWKIT_CAMPAIGN_SETTLE_SECONDS']),
}
(out / 'campaign.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY

printf '\n' >&2
python3 "$ROOT_DIR/benchmarks/summarize_profile_campaign.py" "$output_dir" >&2
printf 'Campaign complete: %s\n' "$output_dir" >&2
printf '%s\n' "$output_dir"
