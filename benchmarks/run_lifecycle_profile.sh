#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
warmup=256
iterations=1024
cpu=""
precondition_seconds=0
output_dir=""
build_dir="$ROOT_DIR/build/profile-lifecycle"

usage() {
  cat <<'EOF'
Usage: benchmarks/run_lifecycle_profile.sh [options]

Runs the informational LOOPBACK lifecycle profiling dataset. Heap-backed and
caller-owned construction/destruction are reported separately. Lifecycle data
is secondary to steady-state TX/RX profiling.

Options:
  --warmup N              warmup iterations per lifecycle operation (default: 256)
  --iterations N          measured iterations per operation, 1..4096 (default: 1024)
  --cpu CPU               Linux logical CPU to pin with taskset
  --precondition-seconds N
                          busy-loop selected CPU before measurement (default: 0)
  --output-dir PATH       result directory (default: build/profile-results/lifecycle-<UTC>)
  --build-dir PATH        disposable CMake build tree
  -h, --help              show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --warmup) warmup="$2"; shift 2 ;;
    --iterations) iterations="$2"; shift 2 ;;
    --cpu) cpu="$2"; shift 2 ;;
    --precondition-seconds) precondition_seconds="$2"; shift 2 ;;
    --output-dir) output_dir="$2"; shift 2 ;;
    --build-dir) build_dir="$2"; shift 2 ;;
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
if [[ -n "$cpu" ]] && ! [[ "$cpu" =~ ^[0-9]+$ ]]; then
  echo "cpu must be a non-negative integer" >&2
  exit 2
fi
if [[ -n "$cpu" && "$(uname -s)" != "Linux" ]]; then
  echo "--cpu currently requires Linux taskset" >&2
  exit 2
fi

if [[ -z "$output_dir" ]]; then
  output_dir="$ROOT_DIR/build/profile-results/lifecycle-$(date -u +%Y%m%dT%H%M%SZ)"
fi
mkdir -p "$output_dir"

printf '[lifecycle] removing build tree: %s\n' "$build_dir" >&2
rm -rf -- "$build_dir"

cmake -S "$ROOT_DIR/benchmarks" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BENCHMARK_ENABLE_HEAP=ON
cmake --build "$build_dir" \
  --target spwkit_profile_lifecycle spwkit_profile_counter_floor \
  --parallel 2

lifecycle_binary="$build_dir/spwkit_profile_lifecycle"
floor_binary="$build_dir/spwkit_profile_counter_floor"
for executable in "$lifecycle_binary" "$floor_binary"; do
  if [[ ! -x "$executable" ]]; then
    echo "benchmark executable was not produced: $executable" >&2
    exit 1
  fi
done

run_cmd() {
  if [[ -n "$cpu" ]]; then
    taskset -c "$cpu" "$@"
  else
    "$@"
  fi
}

if (( precondition_seconds > 0 )); then
  printf '[lifecycle] preconditioning CPU for %ss\n' "$precondition_seconds" >&2
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

run_cmd "$floor_binary" --warmup "$warmup" --iterations "$iterations" \
  > "$output_dir/calibration.json"
run_cmd "$lifecycle_binary" --warmup "$warmup" --iterations "$iterations" \
  > "$output_dir/lifecycle.jsonl"

cat > "$output_dir/metadata.json" <<EOF
{"schema":"spwkit.profile.lifecycle.metadata.v1","cpu":${cpu:-null},"precondition_seconds":$precondition_seconds,"warmup_iterations":$warmup,"iterations":$iterations}
EOF

python3 "$ROOT_DIR/benchmarks/summarize_lifecycle_profile.py" \
  "$output_dir/lifecycle.jsonl" "$output_dir/calibration.json"

archive="${output_dir%/}.tar"
rm -f -- "$archive"
if ! tar -C "$(dirname "$output_dir")" -cf "$archive" "$(basename "$output_dir")"; then
  echo "failed to create lifecycle archive: $archive" >&2
  exit 1
fi
if [[ ! -s "$archive" ]]; then
  echo "lifecycle archive is missing or empty: $archive" >&2
  exit 1
fi

printf '\nLifecycle result directory: %s\n' "$output_dir"
printf 'Lifecycle archive         : %s\n' "$archive"
