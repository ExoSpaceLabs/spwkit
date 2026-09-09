#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
warmup=256
iterations=1024
payloads="64 1024 1200 1201 2400 4096"
cpu=""
precondition_seconds=0
build_dir="$ROOT_DIR/build/profile-udp-breakdown"
output_root="$ROOT_DIR/build/profile-results"

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
if [[ -n "$cpu" ]]; then
  if [[ "$(uname -s)" != "Linux" ]] || ! command -v taskset >/dev/null 2>&1; then
    echo "--cpu requires Linux taskset" >&2
    exit 2
  fi
  if ! [[ "$cpu" =~ ^[0-9]+$ ]] || ! taskset -c "$cpu" true >/dev/null 2>&1; then
    echo "cpu must be an available non-negative logical CPU" >&2
    exit 2
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

cat > "$output_dir/metadata.json" <<EOF
{"schema":"spwkit.profile.udp-breakdown.metadata.v1","git_sha":"$git_sha","git_short_sha":"$git_short","timestamp_utc":"$timestamp","build_type":"Release","warmup_iterations":$warmup,"measured_iterations":$iterations,"payloads":"$payloads","cpu":${cpu:-null},"precondition_seconds":$precondition_seconds,"authoritative_api_performance_result":false,"note":"diagnostic component attribution; validate production changes with centralized UDP comparison"}
EOF

python3 "$ROOT_DIR/benchmarks/summarize_udp_breakdown.py" \
  "$output_dir/breakdown.jsonl" "$output_dir/calibration.json" \
  --output "$output_dir/summary.txt"

python3 - "$output_dir" <<'PY'
import json, sys
from pathlib import Path
root = Path(sys.argv[1])
cal = json.loads((root / 'calibration.json').read_text())
assert cal['schema'] == 'spwkit.profile.calibration.v1'
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
