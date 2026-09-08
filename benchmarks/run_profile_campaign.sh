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
build_root="$ROOT_DIR/build/profile-campaign"
output_dir="$ROOT_DIR/build/profile-results"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_profile_campaign.sh [options]

Runs profiling configurations strictly one at a time. Every case receives a
fresh build directory and therefore a fresh CMake configure/build before its
counter-floor calibration and payload sweep.

Options:
  --cases "LIST"        all, or space/comma-separated case names (default: all)
  --warmup N            warmup iterations per payload (default: 256)
  --iterations N        measured iterations per payload (default: 1024)
  --payloads "LIST"     payload sizes in bytes (default: 0 1 8 64 256 1024 4096)
  --counter-hz N        optional architectural counter frequency override
  --settle-seconds N    pause after each clean build before timing (default: 1)
  --build-root PATH     disposable root for per-case build trees
  --output-dir PATH     campaign result directory
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
    --build-root) build_root="$2"; shift 2 ;;
    --output-dir) output_dir="$2"; shift 2 ;;
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

# A campaign itself also starts from a clean state. Per-case runners repeat the
# deletion defensively before configuring their own tree.
rm -rf -- "$build_root" "$output_dir"
mkdir -p "$build_root" "$output_dir/cases" "$output_dir/calibration"
: > "$output_dir/results.jsonl"
: > "$output_dir/calibration.jsonl"

printf 'SpWKit profiling campaign\n' >&2
printf '  cases: %s\n' "${selected_cases[*]}" >&2
printf '  build profile: Release\n' >&2
printf '  clean rebuild per case: yes\n' >&2
printf '  serial cases: yes\n' >&2
printf '  counter-floor calibration per case: yes\n' >&2

case_index=0
for case_name in "${selected_cases[@]}"; do
  case_index=$((case_index + 1))
  case_build="$build_root/$case_name"
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
    --calibration-output "$calibration_output"

  cat "$case_output" >> "$output_dir/results.jsonl"
  cat "$calibration_output" >> "$output_dir/calibration.jsonl"
done

export SPWKIT_CAMPAIGN_CASES="${selected_cases[*]}"
export SPWKIT_CAMPAIGN_WARMUP="$warmup"
export SPWKIT_CAMPAIGN_ITERATIONS="$iterations"
export SPWKIT_CAMPAIGN_PAYLOADS="$payloads"
export SPWKIT_CAMPAIGN_COUNTER_HZ="$counter_hz"
export SPWKIT_CAMPAIGN_SETTLE_SECONDS="$settle_seconds"
export SPWKIT_CAMPAIGN_OUTPUT_DIR="$output_dir"
export SPWKIT_CAMPAIGN_GIT_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || printf unknown)"

python3 - <<'PY'
import json
import os
from pathlib import Path

out = Path(os.environ['SPWKIT_CAMPAIGN_OUTPUT_DIR'])
metadata = {
    'schema': 'spwkit.profile.campaign.v1',
    'git_sha': os.environ['SPWKIT_CAMPAIGN_GIT_SHA'],
    'build_type': 'Release',
    'clean_rebuild_per_case': True,
    'serial_execution': True,
    'counter_floor_calibration_per_case': True,
    'cases': os.environ['SPWKIT_CAMPAIGN_CASES'].split(),
    'warmup_iterations': int(os.environ['SPWKIT_CAMPAIGN_WARMUP']),
    'measured_iterations': int(os.environ['SPWKIT_CAMPAIGN_ITERATIONS']),
    'payloads': os.environ['SPWKIT_CAMPAIGN_PAYLOADS'],
    'counter_hz_override': int(os.environ['SPWKIT_CAMPAIGN_COUNTER_HZ']),
    'settle_seconds_after_build': int(os.environ['SPWKIT_CAMPAIGN_SETTLE_SECONDS']),
}
(out / 'campaign.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY

printf '\nCampaign complete: %s\n' "$output_dir" >&2
