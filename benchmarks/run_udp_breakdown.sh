#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
warmup=256
iterations=1024
payloads="64 1024 1200 1201 2400 4096"
cpu=""
precondition_seconds=0
governor_mode="keep"
build_dir="$ROOT_DIR/build/profile-udp-breakdown"
output_root="$ROOT_DIR/build/profile-results"

governor_path=""
governor_before="unknown"
governor_effective="unknown"
governor_status="not-requested"
governor_changed=0
scaling_driver="unknown"

restore_governor() {
  if (( governor_changed )) && [[ -n "$governor_path" && -w "$governor_path" ]]; then
    printf '%s\n' "$governor_before" > "$governor_path" 2>/dev/null || true
  fi
}
trap restore_governor EXIT

usage() {
  cat <<'EOF'
Usage: benchmarks/run_udp_breakdown.sh [options]

Runs diagnostic VSPW-TP/UDP component microbenchmarks. These stage timings are
for attribution only and are not additive to the production public-API timing.

Options:
  --warmup N              warmup iterations per stage (default: 256)
  --iterations N          measured iterations per stage (default: 1024)
  --payloads "LIST"       payload sizes (default: 64 1024 1200 1201 2400 4096)
  --cpu CPU               Linux logical CPU to pin with taskset
  --precondition-seconds N
                          busy-loop selected CPU before the run (default: 0)
  --governor MODE         keep or performance; performance requires --cpu
  --build-dir PATH        disposable CMake build directory
  --output-root PATH      result parent directory
  -h, --help              show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --warmup) warmup="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --payloads) payloads="${2//,/ }"; shift 2 ;;
    --cpu) cpu="$2"; shift 2 ;;
    --precondition-seconds) precondition_seconds="$2"; shift 2 ;;
    --governor) governor_mode="$2"; shift 2 ;;
    --build-dir) build_dir="$2"; shift 2 ;;
    --output-root) output_root="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if ! [[ "$warmup" =~ ^[0-9]+$ && "$iterations" =~ ^[0-9]+$ && "$precondition_seconds" =~ ^[0-9]+$ ]]; then
  echo "warmup, iterations and precondition-seconds must be non-negative integers" >&2
  exit 2
fi
if (( iterations < 1 || iterations > 4096 )); then
  echo "iterations must be in the range 1..4096" >&2
  exit 2
fi
if [[ "$governor_mode" != "keep" && "$governor_mode" != "performance" ]]; then
  echo "governor must be keep or performance" >&2
  exit 2
fi
if [[ "$governor_mode" == "performance" && -z "$cpu" ]]; then
  echo "--governor performance requires --cpu so the controlled CPU is explicit" >&2
  exit 2
fi
if [[ -n "$cpu" ]]; then
  if [[ "$(uname -s)" != "Linux" ]] || ! command -v taskset >/dev/null 2>&1; then
    echo "--cpu requires Linux taskset" >&2
    exit 2
  fi
  if ! [[ "$cpu" =~ ^[0-9]+$ ]] || ! taskset -c "$cpu" true >/dev/null 2>&1; then
    echo "cpu must be an available non-negative logical CPU" >&2
    exit 2
  fi

  governor_path="/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_governor"
  scaling_driver_path="/sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_driver"
  if [[ -r "$governor_path" ]]; then
    governor_before="$(cat "$governor_path")"
    governor_effective="$governor_before"
  else
    governor_status="unavailable"
  fi
  if [[ -r "$scaling_driver_path" ]]; then
    scaling_driver="$(cat "$scaling_driver_path")"
  fi

  if [[ "$governor_mode" == "performance" ]]; then
    governor_status="unavailable"
    if [[ -e "$governor_path" ]]; then
      if [[ -w "$governor_path" ]]; then
        if printf '%s\n' performance > "$governor_path" 2>/dev/null; then
          governor_effective="$(cat "$governor_path" 2>/dev/null || printf unknown)"
          governor_status="applied"
          if [[ "$governor_effective" != "$governor_before" ]]; then
            governor_changed=1
          fi
        else
          governor_status="write-failed"
        fi
      else
        governor_status="not-writable"
      fi
    fi
  elif [[ -r "$governor_path" ]]; then
    governor_status="kept"
  fi
fi

read -r -a payload_array <<< "$payloads"
if (( ${#payload_array[@]} == 0 )); then
  echo "at least one payload is required" >&2
  exit 2
fi
for payload in "${payload_array[@]}"; do
  if ! [[ "$payload" =~ ^[0-9]+$ ]] || (( payload > 4096 )); then
    echo "payload must be in 0..4096: $payload" >&2
    exit 2
  fi
done

git_sha="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || printf unknown)"
git_short="$(git -C "$ROOT_DIR" rev-parse --short=8 HEAD 2>/dev/null || printf unknown)"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
result_name="udp-breakdown-${timestamp}-${git_short}"
output_dir="$output_root/$result_name"
archive="$output_root/${result_name}.tar"

rm -rf -- "$build_dir"
mkdir -p "$output_dir"

cmake -S "$ROOT_DIR/benchmarks" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" \
  --target spwkit_profile_udp_breakdown spwkit_profile_counter_floor \
  --parallel 2

benchmark="$build_dir/spwkit_profile_udp_breakdown"
floor="$build_dir/spwkit_profile_counter_floor"
[[ -x "$benchmark" && -x "$floor" ]] || {
  echo "UDP breakdown benchmark binaries were not produced" >&2
  exit 1
}

compiler_path="$(awk -F= '/^CMAKE_C_COMPILER:FILEPATH=/{print $2; exit}' "$build_dir/CMakeCache.txt" 2>/dev/null || true)"
if [[ -z "$compiler_path" ]]; then
  compiler_path="unknown"
fi
if [[ "$compiler_path" != "unknown" && -x "$compiler_path" ]]; then
  compiler_version="$($compiler_path --version 2>/dev/null | head -n 1 || true)"
  [[ -n "$compiler_version" ]] || compiler_version="unknown"
else
  compiler_version="unknown"
fi

run_cmd() {
  if [[ -n "$cpu" ]]; then
    taskset -c "$cpu" "$@"
  else
    "$@"
  fi
}

if (( precondition_seconds > 0 )); then
  if [[ -n "$cpu" ]]; then
    taskset -c "$cpu" python3 - "$precondition_seconds" <<'PY'
import sys, time
end = time.monotonic() + int(sys.argv[1])
x = 1
while time.monotonic() < end:
    x = (x * 1664525 + 1013904223) & 0xffffffff
raise SystemExit(0 if x >= 0 else 1)
PY
  else
    python3 - "$precondition_seconds" <<'PY'
import sys, time
end = time.monotonic() + int(sys.argv[1])
x = 1
while time.monotonic() < end:
    x = (x * 1664525 + 1013904223) & 0xffffffff
raise SystemExit(0 if x >= 0 else 1)
PY
  fi
fi

run_cmd "$floor" --warmup "$warmup" --iterations "$iterations" \
  > "$output_dir/calibration.json"
: > "$output_dir/breakdown.jsonl"
for payload in "${payload_array[@]}"; do
  printf '[udp-breakdown] payload %s B\n' "$payload" >&2
  run_cmd "$benchmark" --payload "$payload" --warmup "$warmup" --iterations "$iterations" \
    >> "$output_dir/breakdown.jsonl"
done

os_name="$(uname -s 2>/dev/null || printf unknown)"
kernel_release="$(uname -r 2>/dev/null || printf unknown)"
architecture="$(uname -m 2>/dev/null || printf unknown)"
cpu_model="unknown"
if [[ "$os_name" == "Linux" && -r /proc/cpuinfo ]]; then
  cpu_model="$(awk -F: '/^model name[[:space:]]*:/{sub(/^[[:space:]]+/, "", $2); print $2; exit}' /proc/cpuinfo)"
  [[ -n "$cpu_model" ]] || cpu_model="unknown"
fi
nice_level="$(ps -o ni= -p $$ 2>/dev/null | xargs || true)"
[[ -n "$nice_level" ]] || nice_level="unknown"
affinity="unknown"
if command -v taskset >/dev/null 2>&1; then
  affinity="$(taskset -pc $$ 2>/dev/null | sed 's/.*: //' || true)"
  [[ -n "$affinity" ]] || affinity="unknown"
fi

export SPWKIT_UDP_META_OUTPUT="$output_dir/metadata.json"
export SPWKIT_UDP_META_CALIBRATION="$output_dir/calibration.json"
export SPWKIT_UDP_META_GIT_SHA="$git_sha"
export SPWKIT_UDP_META_GIT_SHORT="$git_short"
export SPWKIT_UDP_META_TIMESTAMP="$timestamp"
export SPWKIT_UDP_META_WARMUP="$warmup"
export SPWKIT_UDP_META_ITERATIONS="$iterations"
export SPWKIT_UDP_META_PAYLOADS="$payloads"
export SPWKIT_UDP_META_CPU="$cpu"
export SPWKIT_UDP_META_PRECONDITION="$precondition_seconds"
export SPWKIT_UDP_META_GOVERNOR_REQUEST="$governor_mode"
export SPWKIT_UDP_META_GOVERNOR_BEFORE="$governor_before"
export SPWKIT_UDP_META_GOVERNOR_EFFECTIVE="$governor_effective"
export SPWKIT_UDP_META_GOVERNOR_STATUS="$governor_status"
export SPWKIT_UDP_META_SCALING_DRIVER="$scaling_driver"
export SPWKIT_UDP_META_OS="$os_name"
export SPWKIT_UDP_META_KERNEL="$kernel_release"
export SPWKIT_UDP_META_ARCH="$architecture"
export SPWKIT_UDP_META_CPU_MODEL="$cpu_model"
export SPWKIT_UDP_META_COMPILER_PATH="$compiler_path"
export SPWKIT_UDP_META_COMPILER_VERSION="$compiler_version"
export SPWKIT_UDP_META_NICE="$nice_level"
export SPWKIT_UDP_META_AFFINITY="$affinity"

python3 - <<'PY'
import json
import os
from pathlib import Path

calibration = json.loads(Path(os.environ['SPWKIT_UDP_META_CALIBRATION']).read_text())
cpu_text = os.environ['SPWKIT_UDP_META_CPU']
metadata = {
    'schema': 'spwkit.profile.udp-breakdown.metadata.v2',
    'git_sha': os.environ['SPWKIT_UDP_META_GIT_SHA'],
    'git_short_sha': os.environ['SPWKIT_UDP_META_GIT_SHORT'],
    'timestamp_utc': os.environ['SPWKIT_UDP_META_TIMESTAMP'],
    'build_type': 'Release',
    'warmup_iterations': int(os.environ['SPWKIT_UDP_META_WARMUP']),
    'measured_iterations': int(os.environ['SPWKIT_UDP_META_ITERATIONS']),
    'payloads': os.environ['SPWKIT_UDP_META_PAYLOADS'],
    'platform': {
        'os': os.environ['SPWKIT_UDP_META_OS'],
        'kernel_release': os.environ['SPWKIT_UDP_META_KERNEL'],
        'architecture': os.environ['SPWKIT_UDP_META_ARCH'],
        'cpu_model': os.environ['SPWKIT_UDP_META_CPU_MODEL'],
    },
    'compiler': {
        'path': os.environ['SPWKIT_UDP_META_COMPILER_PATH'],
        'version': os.environ['SPWKIT_UDP_META_COMPILER_VERSION'],
        'family': calibration.get('compiler', {}).get('family', 'unknown'),
    },
    'counter': calibration.get('counter', {}),
    'host_control': {
        'controlled': bool(cpu_text),
        'selected_cpu': int(cpu_text) if cpu_text else None,
        'affinity': os.environ['SPWKIT_UDP_META_AFFINITY'],
        'nice_level': os.environ['SPWKIT_UDP_META_NICE'],
        'precondition_seconds': int(os.environ['SPWKIT_UDP_META_PRECONDITION']),
        'requested_governor': os.environ['SPWKIT_UDP_META_GOVERNOR_REQUEST'],
        'governor_before': os.environ['SPWKIT_UDP_META_GOVERNOR_BEFORE'],
        'governor_effective': os.environ['SPWKIT_UDP_META_GOVERNOR_EFFECTIVE'],
        'governor_change_status': os.environ['SPWKIT_UDP_META_GOVERNOR_STATUS'],
        'scaling_driver': os.environ['SPWKIT_UDP_META_SCALING_DRIVER'],
    },
    'authoritative_api_performance_result': False,
    'diagnostic_component_attribution': True,
    'note': 'diagnostic component attribution; validate production changes with centralized UDP comparison',
}
Path(os.environ['SPWKIT_UDP_META_OUTPUT']).write_text(json.dumps(metadata, indent=2) + '\n')
PY

python3 "$ROOT_DIR/benchmarks/summarize_udp_breakdown.py" \
  "$output_dir/breakdown.jsonl" "$output_dir/calibration.json" \
  --metadata "$output_dir/metadata.json" \
  --output "$output_dir/summary.txt"

python3 - "$output_dir" <<'PY'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
cal = json.loads((root / 'calibration.json').read_text())
meta = json.loads((root / 'metadata.json').read_text())
assert cal['schema'] == 'spwkit.profile.calibration.v1'
assert meta['schema'] == 'spwkit.profile.udp-breakdown.metadata.v2'
assert meta['build_type'] == 'Release'
assert set(meta['platform']) == {'os', 'kernel_release', 'architecture', 'cpu_model'}
assert set(meta['compiler']) == {'path', 'version', 'family'}
assert {'kind', 'width_bits', 'frequency_hz'} <= set(meta['counter'])
assert {'controlled', 'selected_cpu', 'affinity', 'nice_level', 'precondition_seconds',
        'requested_governor', 'governor_before', 'governor_effective',
        'governor_change_status', 'scaling_driver'} <= set(meta['host_control'])
rows = [json.loads(x) for x in (root / 'breakdown.jsonl').read_text().splitlines() if x.strip()]
assert rows
required = {'sendto','poll_sendto','prepare_poll_sendto','header_encode','header_decode',
            'recvfrom','poll_recvfrom','source_validate','clock_gettime','copy_once',
            'copy_twice','ack_encode_validate_send'}
assert required <= {r['stage'] for r in rows}
assert all(r['schema'] == 'spwkit.profile.udp-breakdown.v1' for r in rows)
assert all(r['diagnostic_only'] is True and r['additive_to_api_interval'] is False for r in rows)
for r in rows:
    assert set(r['statistics']) == {'min','median','mean','p95','p99','max','stddev'}
for payload in {r['payload_bytes'] for r in rows if r['payload_bytes'] > 1200}:
    stages = {r['stage'] for r in rows if r['payload_bytes'] == payload}
    assert {'reassembly_push','reassembly_reset_1m','reassembly_delivery'} <= stages
PY

rm -f -- "$archive"
tar -cf "$archive" -C "$output_root" "$result_name"
tar -tf "$archive" >/dev/null

printf '\nUDP breakdown result directory: %s\n' "$output_dir"
printf 'UDP breakdown archive         : %s\n' "$archive"
