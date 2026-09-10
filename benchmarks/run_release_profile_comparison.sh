#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
baseline_ref="v0.6.1"
candidate_ref="HEAD"
cases="all"
warmup="256"
iterations="1024"
payloads="0 1 8 64 256 1024 4096"
cpu="auto"
precondition_seconds="2"
governor="keep"
result_type="release-controlled"
relative_threshold="5"
absolute_threshold="20"
output_root="$ROOT_DIR/build/release-performance"
fail_on_attention=0

usage() {
  cat <<'EOF'
Usage: benchmarks/run_release_profile_comparison.sh [options]

Runs the same profiling and lifecycle campaigns from a baseline ref and a
candidate ref on the same Linux host, then produces a structured release
comparison. This is the preferred release-candidate command when run on a
controlled host. Repeat suspect rows before accepting a performance change.

Options:
  --baseline-ref REF         baseline revision (default: v0.6.1)
  --candidate-ref REF        candidate revision (default: HEAD)
  --cases LIST               profiling cases (default: all)
  --warmup N                 warmup iterations (default: 256)
  --iterations N             measured iterations (default: 1024)
  --payloads LIST            payload sizes (default: 0 1 8 64 256 1024 4096)
  --cpu CPU|auto             Linux logical CPU (default: auto)
  --precondition-seconds N   CPU preconditioning before each step (default: 2)
  --governor MODE            keep or performance (default: keep)
  --type NAME                campaign result type (default: release-controlled)
  --relative-threshold PCT   attention threshold (default: 5)
  --absolute-threshold TICKS attention threshold (default: 20)
  --output-root PATH         parent result directory
  --fail-on-attention        return non-zero when comparison flags rows
  -h, --help                 show this help

An attention row is a triage result, not proof of a regression. Release
acceptance still requires reproducible controlled-host evidence.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --baseline-ref) baseline_ref="$2"; shift 2 ;;
    --candidate-ref) candidate_ref="$2"; shift 2 ;;
    --cases) cases="$2"; shift 2 ;;
    --warmup) warmup="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --payloads) payloads="$2"; shift 2 ;;
    --cpu) cpu="$2"; shift 2 ;;
    --precondition-seconds) precondition_seconds="$2"; shift 2 ;;
    --governor) governor="$2"; shift 2 ;;
    --type) result_type="$2"; shift 2 ;;
    --relative-threshold) relative_threshold="$2"; shift 2 ;;
    --absolute-threshold) absolute_threshold="$2"; shift 2 ;;
    --output-root) output_root="$2"; shift 2 ;;
    --fail-on-attention) fail_on_attention=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "release performance comparison currently requires Linux" >&2
  exit 2
fi
for value in "$warmup" "$iterations" "$precondition_seconds"; do
  [[ "$value" =~ ^[0-9]+$ ]] || {
    echo "warmup, iterations and precondition-seconds must be non-negative integers" >&2
    exit 2
  }
done
if [[ "$governor" != "keep" && "$governor" != "performance" ]]; then
  echo "governor must be keep or performance" >&2
  exit 2
fi
if [[ "$cpu" == "auto" ]]; then
  cpu="$(python3 - <<'PY'
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

print(max(allowed, key=lambda item: (max_khz(item), -item)))
PY
)"
elif ! [[ "$cpu" =~ ^[0-9]+$ ]]; then
  echo "cpu must be a logical CPU number or auto" >&2
  exit 2
fi
taskset -c "$cpu" true >/dev/null 2>&1 || {
  echo "logical CPU $cpu is not available in this process affinity mask" >&2
  exit 2
}

baseline_sha="$(git -C "$ROOT_DIR" rev-parse "$baseline_ref^{commit}")"
candidate_sha="$(git -C "$ROOT_DIR" rev-parse "$candidate_ref^{commit}")"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
run_root="$(mkdir -p "$output_root" && cd "$output_root" && pwd)/${stamp}-${baseline_sha:0:7}-${candidate_sha:0:7}"
mkdir -p "$run_root"

worktree_root="$(mktemp -d "${TMPDIR:-/tmp}/spwkit-release-profile.XXXXXX")"
baseline_tree="$worktree_root/baseline"
candidate_tree="$worktree_root/candidate"

cleanup() {
  git -C "$ROOT_DIR" worktree remove --force "$baseline_tree" >/dev/null 2>&1 || true
  git -C "$ROOT_DIR" worktree remove --force "$candidate_tree" >/dev/null 2>&1 || true
  rm -rf -- "$worktree_root"
}
trap cleanup EXIT

git -C "$ROOT_DIR" worktree add --detach "$baseline_tree" "$baseline_sha" >/dev/null
git -C "$ROOT_DIR" worktree add --detach "$candidate_tree" "$candidate_sha" >/dev/null

run_campaign() {
  local tree="$1"
  local label="$2"
  local profile_output="$run_root/$label/profile"
  local lifecycle_output="$run_root/$label/lifecycle"
  mkdir -p "$profile_output" "$lifecycle_output"

  local result_dir
  result_dir="$(
    "$tree/benchmarks/run_profile_campaign.sh" \
      --cases "$cases" \
      --warmup "$warmup" \
      --iterations "$iterations" \
      --payloads "$payloads" \
      --controlled-cpu "$cpu" \
      --precondition-seconds "$precondition_seconds" \
      --governor "$governor" \
      --type "$result_type-$label" \
      --build-root "$run_root/$label/build-profile" \
      --output-root "$profile_output" | \
      tail -n 1
  )"
  [[ -d "$result_dir" ]] || {
    echo "$label campaign result directory was not produced: $result_dir" >&2
    exit 1
  }

  "$tree/benchmarks/run_lifecycle_profile.sh" \
    --warmup "$warmup" \
    --iterations "$iterations" \
    --cpu "$cpu" \
    --precondition-seconds "$precondition_seconds" \
    --build-dir "$run_root/$label/build-lifecycle" \
    --output-dir "$lifecycle_output" >/dev/null

  printf '%s\n' "$result_dir"
}

echo "[release-profile] baseline $baseline_ref -> $baseline_sha"
echo "[release-profile] candidate $candidate_ref -> $candidate_sha"
echo "[release-profile] pinned CPU $cpu"

baseline_result="$(run_campaign "$baseline_tree" baseline)"
candidate_result="$(run_campaign "$candidate_tree" candidate)"

compare_args=(
  "$baseline_result"
  "$candidate_result"
  --baseline-lifecycle "$run_root/baseline/lifecycle"
  --candidate-lifecycle "$run_root/candidate/lifecycle"
  --relative-threshold "$relative_threshold"
  --absolute-threshold "$absolute_threshold"
  --require-comparable-environment
  --output-json "$run_root/comparison.json"
  --output-markdown "$run_root/comparison.md"
)
if (( fail_on_attention )); then
  compare_args+=(--fail-on-attention)
fi

python3 "$candidate_tree/benchmarks/compare_release_profiles.py" "${compare_args[@]}"

cat > "$run_root/release-profile.json" <<EOF
{
  "schema": "spwkit.profile.release-run.v1",
  "baseline_ref": $(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$baseline_ref"),
  "baseline_sha": "$baseline_sha",
  "candidate_ref": $(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$candidate_ref"),
  "candidate_sha": "$candidate_sha",
  "cpu": $cpu,
  "warmup": $warmup,
  "iterations": $iterations,
  "precondition_seconds": $precondition_seconds,
  "governor": "$governor",
  "result_type": "$result_type"
}
EOF

# The release artifact needs raw benchmark evidence, not disposable CMake trees.
# Removing the build directories keeps CI artifacts small without discarding any
# campaign JSON/JSONL, summaries, calibration data, lifecycle data or metadata.
rm -rf -- \
  "$run_root/baseline/build-profile" \
  "$run_root/baseline/build-lifecycle" \
  "$run_root/candidate/build-profile" \
  "$run_root/candidate/build-lifecycle"

echo
echo "Release comparison directory: $run_root"
