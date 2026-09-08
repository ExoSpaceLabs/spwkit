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
settle_explicit=0
controlled_cpu=""
precondition_seconds="0"
governor_mode="keep"
result_type="host"
build_root="$ROOT_DIR/build/profile-campaign"
output_root="$ROOT_DIR/build/profile-results"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_profile_campaign.sh [options]

Runs profiling configurations strictly one at a time. Every measurement case
receives a fresh build directory and therefore a fresh CMake configure/build.
The campaign includes paired DRIVER layers, direct/native DRIVER TX/RX,
DRIVER copied-vs-zero-copy TX, LOOPBACK/SIMULATOR TX/RX, VSPW-TP/UDP TX/RX,
and on Linux DEVICE/VSPD TX/RX.

Child benchmarks write machine-readable JSON into the result set but the
campaign terminal output stays human-readable and ends with a consolidated
numeric summary.

Results are stored under:
  <output-root>/<type>-<UTC timestamp>-<short git commit>/

Options:
  --cases "LIST"        all, or space/comma-separated DRIVER layer case names
  --warmup N            warmup iterations per payload (default: 256)
  --iterations N        measured iterations per payload (default: 1024)
  --payloads "LIST"     payload sizes in bytes (default: 0 1 8 64 256 1024 4096)
  --counter-hz N        optional architectural counter frequency override
  --settle-seconds N    pause after each clean build before timing (default: 1)
  --controlled-cpu CPU  Linux logical CPU number, or auto; pins each child process tree
  --precondition-seconds N
                        busy-warm the selected CPU before each campaign step
  --governor MODE       keep or performance; performance is attempted when writable
  --type NAME           result target/type, e.g. host, github-hosted, stm32h755
                        (default: host)
  --build-root PATH     disposable parent for per-campaign/per-case build trees
  --output-root PATH    parent directory for uniquely named result folders
  --list-cases          print supported DRIVER layer cases and exit
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
    --settle-seconds) settle_seconds="$2"; settle_explicit=1; shift 2 ;;
    --controlled-cpu) controlled_cpu="$2"; shift 2 ;;
    --precondition-seconds) precondition_seconds="$2"; shift 2 ;;
    --governor) governor_mode="$2"; shift 2 ;;
    --type) result_type="$2"; shift 2 ;;
    --build-root) build_root="$2"; shift 2 ;;
    --output-root) output_root="$2"; shift 2 ;;
    --list-cases) spw_profile_case_list; exit 0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if ! [[ "$warmup" =~ ^[0-9]+$ && "$iterations" =~ ^[0-9]+$ && "$counter_hz" =~ ^[0-9]+$ && "$settle_seconds" =~ ^[0-9]+$ && "$precondition_seconds" =~ ^[0-9]+$ ]]; then
  echo "warmup, iterations, counter-hz, settle-seconds and precondition-seconds must be non-negative integers" >&2
  exit 2
fi
if [[ "$governor_mode" != "keep" && "$governor_mode" != "performance" ]]; then
  echo "governor must be keep or performance" >&2
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

controlled_mode=0
selected_cpu=""
governor_path=""
governor_before="unknown"
governor_effective="unknown"
governor_changed=0
governor_change_status="not-requested"
scaling_driver="unknown"
auto_cpu_policy="none"

restore_profile_governor() {
  if (( governor_changed )) && [[ -n "$governor_path" && -w "$governor_path" ]]; then
    printf '%s\n' "$governor_before" > "$governor_path" || true
  fi
}
trap restore_profile_governor EXIT

if [[ -n "$controlled_cpu" ]]; then
  if [[ "$(uname -s)" != "Linux" ]]; then
    echo "controlled CPU profiling is currently supported only on Linux" >&2
    exit 2
  fi
  if ! command -v taskset >/dev/null 2>&1; then
    echo "controlled CPU profiling requires taskset (util-linux)" >&2
    exit 2
  fi

  if [[ "$controlled_cpu" == "auto" ]]; then
    selected_cpu="$(python3 - <<'PY'
import os
from pathlib import Path

allowed = sorted(os.sched_getaffinity(0))
if not allowed:
    raise SystemExit("no CPU is available in the process affinity mask")

def max_khz(cpu):
    path = Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/cpuinfo_max_freq")
    try:
        return int(path.read_text().strip())
    except (OSError, ValueError):
        return -1

print(max(allowed, key=lambda cpu: (max_khz(cpu), -cpu)))
PY
)"
    auto_cpu_policy="highest-cpuinfo-max-freq-among-allowed"
  elif [[ "$controlled_cpu" =~ ^[0-9]+$ ]]; then
    selected_cpu="$controlled_cpu"
    auto_cpu_policy="explicit"
  else
    echo "controlled-cpu must be a logical CPU number or auto" >&2
    exit 2
  fi

  if ! taskset -c "$selected_cpu" true >/dev/null 2>&1; then
    echo "logical CPU $selected_cpu is not available in this process affinity mask" >&2
    exit 2
  fi

  controlled_mode=1
  if (( settle_explicit == 0 )); then
    settle_seconds=0
  fi

  governor_path="/sys/devices/system/cpu/cpu${selected_cpu}/cpufreq/scaling_governor"
  driver_path="/sys/devices/system/cpu/cpu${selected_cpu}/cpufreq/scaling_driver"
  if [[ -r "$governor_path" ]]; then
    governor_before="$(cat "$governor_path")"
    governor_effective="$governor_before"
  fi
  if [[ -r "$driver_path" ]]; then
    scaling_driver="$(cat "$driver_path")"
  fi

  if [[ "$governor_mode" == "performance" ]]; then
    governor_change_status="unavailable"
    if [[ -e "$governor_path" ]]; then
      if [[ -w "$governor_path" ]]; then
        if printf '%s\n' performance > "$governor_path" 2>/dev/null; then
          governor_effective="$(cat "$governor_path" 2>/dev/null || printf unknown)"
          governor_change_status="applied"
          if [[ "$governor_effective" != "$governor_before" ]]; then
            governor_changed=1
          fi
        else
          governor_change_status="write-failed"
        fi
      else
        governor_change_status="not-writable"
      fi
    fi
  else
    governor_change_status="kept"
  fi
fi

profile_precondition_cpu() {
  if (( controlled_mode == 0 || precondition_seconds == 0 )); then
    return 0
  fi
  taskset -c "$selected_cpu" python3 - "$precondition_seconds" <<'PY'
import sys
import time

seconds = int(sys.argv[1])
deadline = time.monotonic() + seconds
value = 1
while time.monotonic() < deadline:
    value = ((value * 1664525) + 1013904223) & 0xffffffff
if value == -1:
    print(value)
PY
}

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
         "$output_dir/comparison" \
         "$output_dir/backends" \
         "$output_dir/backend-calibration" \
         "$output_dir/logs"
: > "$output_dir/results.jsonl"
: > "$output_dir/calibration.jsonl"

run_campaign_step() {
  local label="$1"
  local log_file="$2"
  local status
  shift 2

  profile_precondition_cpu
  if (( controlled_mode )); then
    if taskset -c "$selected_cpu" "$@" >"$log_file" 2>&1; then
      return 0
    else
      status=$?
    fi
  else
    if "$@" >"$log_file" 2>&1; then
      return 0
    else
      status=$?
    fi
  fi

  printf '\nERROR: profiling campaign step failed: %s (exit %d)\n' "$label" "$status" >&2
  printf 'Child log: %s\n' "$log_file" >&2
  printf '%s\n' '---------------- child log ----------------' >&2
  cat "$log_file" >&2 || true
  printf '%s\n' '-------------- end child log --------------' >&2
  printf 'Partial results preserved at: %s\n' "$output_dir" >&2
  return "$status"
}

printf 'SpWKit profiling campaign\n' >&2
printf '  type: %s\n' "$result_type" >&2
printf '  timestamp UTC: %s\n' "$timestamp_utc" >&2
printf '  commit: %s (%s)\n' "$git_sha" "$git_short_sha" >&2
printf '  result directory: %s\n' "$output_dir" >&2
printf '  DRIVER layer cases: %s\n' "${selected_cases[*]}" >&2
printf '  build profile: Release\n' >&2
printf '  clean rebuild per measurement configuration: yes\n' >&2
printf '  serial execution: yes\n' >&2
if (( controlled_mode )); then
  printf '  controlled host: CPU %s, precondition %ss, settle %ss\n' "$selected_cpu" "$precondition_seconds" "$settle_seconds" >&2
  printf '  cpufreq: driver=%s governor(before=%s effective=%s requested=%s status=%s)\n' "$scaling_driver" "$governor_before" "$governor_effective" "$governor_mode" "$governor_change_status" >&2
else
  printf '  controlled host: disabled\n' >&2
fi
printf '  direct/native comparison: DRIVER copied TX + RX\n' >&2
printf '  copy-elimination comparison: DRIVER copied vs zero-copy TX/RX\n' >&2
printf '  in-memory backends: LOOPBACK + SIMULATOR TX/RX\n' >&2
printf '  UDP backend: VSPW-TP TX/RX vs direct loopback UDP socket\n' >&2
if [[ "$(uname -s)" == "Linux" ]]; then
  printf '  DEVICE backend: VSPD TX/RX vs direct VSPD SOCK_SEQPACKET client\n' >&2
fi

case_index=0
for case_name in "${selected_cases[@]}"; do
  case_index=$((case_index + 1))
  case_build="$campaign_build_root/$case_name"
  case_output="$output_dir/cases/$case_name.jsonl"
  calibration_output="$output_dir/calibration/$case_name.json"
  printf '\n[campaign DRIVER layer %d/%d] %s\n' "$case_index" "${#selected_cases[@]}" "$case_name" >&2

  run_campaign_step "DRIVER layer $case_name" "$output_dir/logs/driver-layer-$case_name.log" \
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

tx_comparison_output="$output_dir/comparison/tx_api_native.jsonl"
tx_comparison_calibration="$output_dir/comparison/calibration.json"
printf '\n[campaign comparison] direct/native vs SpWKit copied DRIVER TX\n' >&2
run_campaign_step "DRIVER copied TX native comparison" "$output_dir/logs/native-tx-comparison.log" \
  "$ROOT_DIR/benchmarks/run_native_comparison.sh" \
  --warmup "$warmup" \
  --iterations "$iterations" \
  --payloads "$payloads" \
  --counter-hz "$counter_hz" \
  --settle-seconds "$settle_seconds" \
  --build-dir "$campaign_build_root/native-tx-comparison" \
  --output "$tx_comparison_output" \
  --calibration-output "$tx_comparison_calibration"

zero_copy_comparison_output="$output_dir/comparison/driver_tx_copy_zero_copy.jsonl"
zero_copy_comparison_calibration="$output_dir/comparison/driver_tx_copy_zero_copy_calibration.json"
printf '\n[campaign comparison] DRIVER copied vs zero-copy TX\n' >&2
run_campaign_step "DRIVER copied vs zero-copy TX" "$output_dir/logs/zero-copy-tx.log" \
  bash "$ROOT_DIR/benchmarks/run_zero_copy_comparison.sh" \
  --warmup "$warmup" \
  --iterations "$iterations" \
  --payloads "$payloads" \
  --counter-hz "$counter_hz" \
  --settle-seconds "$settle_seconds" \
  --build-dir "$campaign_build_root/driver-tx-copy-zero-copy" \
  --output "$zero_copy_comparison_output" \
  --calibration-output "$zero_copy_comparison_calibration"

rx_comparison_output="$output_dir/comparison/rx_native_api.jsonl"
rx_comparison_calibration="$output_dir/comparison/rx_calibration.json"
printf '\n[campaign comparison] direct/native vs SpWKit copied DRIVER RX\n' >&2
run_campaign_step "DRIVER copied RX native comparison" "$output_dir/logs/native-rx-comparison.log" \
  bash "$ROOT_DIR/benchmarks/run_native_receive_comparison.sh" \
  --warmup "$warmup" \
  --iterations "$iterations" \
  --payloads "$payloads" \
  --counter-hz "$counter_hz" \
  --settle-seconds "$settle_seconds" \
  --build-dir "$campaign_build_root/native-rx-comparison" \
  --output "$rx_comparison_output" \
  --calibration-output "$rx_comparison_calibration"

zero_copy_rx_output="$output_dir/comparison/driver_rx_copy_zero_copy.jsonl"
zero_copy_rx_calibration="$output_dir/comparison/driver_rx_copy_zero_copy_calibration.json"
printf '\n[campaign comparison] DRIVER copied vs zero-copy RX\n' >&2
run_campaign_step "DRIVER copied vs zero-copy RX" "$output_dir/logs/zero-copy-rx.log" \
  bash "$ROOT_DIR/benchmarks/run_zero_copy_receive_comparison.sh" \
  --warmup "$warmup" \
  --iterations "$iterations" \
  --payloads "$payloads" \
  --counter-hz "$counter_hz" \
  --settle-seconds "$settle_seconds" \
  --build-dir "$campaign_build_root/driver-rx-copy-zero-copy" \
  --output "$zero_copy_rx_output" \
  --calibration-output "$zero_copy_rx_calibration"

host_backend_cases=(
  "loopback tx"
  "loopback rx"
  "simulator tx"
  "simulator rx"
)
for backend_case in "${host_backend_cases[@]}"; do
  read -r backend direction <<< "$backend_case"
  case_name="${backend}_${direction}"
  backend_output="$output_dir/backends/${case_name}.jsonl"
  backend_calibration="$output_dir/backend-calibration/${case_name}.json"
  printf '\n[campaign backend] %s/%s\n' "$backend" "$direction" >&2
  run_campaign_step "$backend/$direction backend" "$output_dir/logs/${case_name}.log" \
    bash "$ROOT_DIR/benchmarks/run_host_backend_benchmark.sh" \
    --backend "$backend" \
    --direction "$direction" \
    --warmup "$warmup" \
    --iterations "$iterations" \
    --payloads "$payloads" \
    --counter-hz "$counter_hz" \
    --settle-seconds "$settle_seconds" \
    --build-dir "$campaign_build_root/$case_name" \
    --output "$backend_output" \
    --calibration-output "$backend_calibration"
done

for direction in tx rx; do
  udp_output="$output_dir/comparison/udp_${direction}.jsonl"
  udp_calibration="$output_dir/comparison/udp_${direction}_calibration.json"
  printf '\n[campaign UDP comparison] %s\n' "$direction" >&2
  run_campaign_step "UDP $direction comparison" "$output_dir/logs/udp_${direction}.log" \
    bash "$ROOT_DIR/benchmarks/run_udp_comparison.sh" \
    --direction "$direction" \
    --warmup "$warmup" \
    --iterations "$iterations" \
    --payloads "$payloads" \
    --counter-hz "$counter_hz" \
    --settle-seconds "$settle_seconds" \
    --build-dir "$campaign_build_root/udp_$direction" \
    --output "$udp_output" \
    --calibration-output "$udp_calibration"
done

device_comparison_cases=()
if [[ "$(uname -s)" == "Linux" ]]; then
  for direction in tx rx; do
    device_output="$output_dir/comparison/device_${direction}.jsonl"
    device_calibration="$output_dir/comparison/device_${direction}_calibration.json"
    printf '\n[campaign DEVICE comparison] %s\n' "$direction" >&2
    run_campaign_step "DEVICE $direction comparison" "$output_dir/logs/device_${direction}.log" \
      bash "$ROOT_DIR/benchmarks/run_device_comparison.sh" \
      --direction "$direction" \
      --warmup "$warmup" \
      --iterations "$iterations" \
      --payloads "$payloads" \
      --counter-hz "$counter_hz" \
      --settle-seconds "$settle_seconds" \
      --build-dir "$campaign_build_root/device_$direction" \
      --output "$device_output" \
      --calibration-output "$device_calibration"
    device_comparison_cases+=("device_$direction")
  done
fi

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
export SPWKIT_CAMPAIGN_DEVICE_CASES="${device_comparison_cases[*]}"
export SPWKIT_CAMPAIGN_CONTROLLED_MODE="$controlled_mode"
export SPWKIT_CAMPAIGN_CONTROLLED_CPU_REQUEST="$controlled_cpu"
export SPWKIT_CAMPAIGN_SELECTED_CPU="$selected_cpu"
export SPWKIT_CAMPAIGN_PRECONDITION_SECONDS="$precondition_seconds"
export SPWKIT_CAMPAIGN_GOVERNOR_REQUEST="$governor_mode"
export SPWKIT_CAMPAIGN_GOVERNOR_BEFORE="$governor_before"
export SPWKIT_CAMPAIGN_GOVERNOR_EFFECTIVE="$governor_effective"
export SPWKIT_CAMPAIGN_GOVERNOR_STATUS="$governor_change_status"
export SPWKIT_CAMPAIGN_SCALING_DRIVER="$scaling_driver"
export SPWKIT_CAMPAIGN_AUTO_CPU_POLICY="$auto_cpu_policy"

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
    'zero_copy_comparison_cases': ['driver_tx_copy_zero_copy', 'driver_rx_copy_zero_copy'],
    'udp_comparison_cases': ['udp_tx', 'udp_rx'],
    'device_comparison_cases': os.environ['SPWKIT_CAMPAIGN_DEVICE_CASES'].split(),
    'direct_native_counter_floor_subtracted': False,
    'host_backend_cases': ['loopback_tx', 'loopback_rx', 'simulator_tx', 'simulator_rx'],
    'summary_file': 'summary.txt',
    'coverage_file': 'coverage.json',
    'cases': os.environ['SPWKIT_CAMPAIGN_CASES'].split(),
    'warmup_iterations': int(os.environ['SPWKIT_CAMPAIGN_WARMUP']),
    'measured_iterations': int(os.environ['SPWKIT_CAMPAIGN_ITERATIONS']),
    'payloads': os.environ['SPWKIT_CAMPAIGN_PAYLOADS'],
    'counter_hz_override': int(os.environ['SPWKIT_CAMPAIGN_COUNTER_HZ']),
    'settle_seconds_after_build': int(os.environ['SPWKIT_CAMPAIGN_SETTLE_SECONDS']),
    'host_control': {
        'enabled': os.environ['SPWKIT_CAMPAIGN_CONTROLLED_MODE'] == '1',
        'requested_cpu': os.environ['SPWKIT_CAMPAIGN_CONTROLLED_CPU_REQUEST'] or None,
        'selected_cpu': int(os.environ['SPWKIT_CAMPAIGN_SELECTED_CPU']) if os.environ['SPWKIT_CAMPAIGN_SELECTED_CPU'] else None,
        'auto_cpu_policy': os.environ['SPWKIT_CAMPAIGN_AUTO_CPU_POLICY'],
        'precondition_seconds': int(os.environ['SPWKIT_CAMPAIGN_PRECONDITION_SECONDS']),
        'requested_governor': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_REQUEST'],
        'governor_before': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_BEFORE'],
        'governor_effective': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_EFFECTIVE'],
        'governor_change_status': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_STATUS'],
        'scaling_driver': os.environ['SPWKIT_CAMPAIGN_SCALING_DRIVER'],
        'child_process_affinity': 'taskset-single-cpu' if os.environ['SPWKIT_CAMPAIGN_CONTROLLED_MODE'] == '1' else 'inherited',
    },
}
(out / 'campaign.json').write_text(json.dumps(metadata, indent=2) + '\n')
PY

printf '\n==================== PROFILING RESULTS ====================\n' >&2
python3 "$ROOT_DIR/benchmarks/summarize_profile_campaign.py" "$output_dir" >&2
printf '===========================================================\n' >&2
printf 'Human-readable summary : %s/summary.txt\n' "$output_dir" >&2
printf 'Machine-readable data  : %s/*.json, %s/*.jsonl\n' "$output_dir" "$output_dir" >&2
printf 'Campaign complete      : %s\n' "$output_dir" >&2
printf '%s\n' "$output_dir"
