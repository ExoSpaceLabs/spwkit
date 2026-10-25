#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${SPWKIT_UDP_PERF_BUILD:-$ROOT_DIR/build/transport-udp}"
PAYLOADS="64 256 1024 1400 4096 16384 65536 262144 1048576"
TOTAL_BYTES=1073741824
LOCAL_ADDRESS="127.0.0.1"

usage() {
  cat <<'USAGE'
Usage:
  bash benchmarks/transport/run_udp_pair.sh [options]

Runs a same-host two-process UDP/VSPW throughput baseline.

Options:
  --payloads "N ..."      logical payload sizes
  --total-bytes N         bytes per direction/case (default 1 GiB)
  --build-dir DIR
  --local-address IPv4    default 127.0.0.1
  -h, --help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --payloads) PAYLOADS="$2"; shift 2 ;;
    --total-bytes) TOTAL_BYTES="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --local-address) LOCAL_ADDRESS="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

rm -rf -- "$BUILD_DIR"
cmake -S "$ROOT_DIR/benchmarks/transport" -B "$BUILD_DIR"   -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel --target spwkit_udp_perf

BIN="$BUILD_DIR/spwkit_udp_perf"
RESULTS="$BUILD_DIR/results.jsonl"
: >"$RESULTS"

for payload in $PAYLOADS; do
  echo "[udp-perf] payload=$payload bytes total=$TOTAL_BYTES"
  "$BIN" --role sink     --local-address "$LOCAL_ADDRESS" --remote-address "$LOCAL_ADDRESS"     --local-port 43001 --remote-port 43000 --link-id 264     --payload-size "$payload" --total-bytes "$TOTAL_BYTES" >>"$RESULTS" &
  sink_pid=$!
  sleep 0.1
  set +e
  "$BIN" --role source     --local-address "$LOCAL_ADDRESS" --remote-address "$LOCAL_ADDRESS"     --local-port 43000 --remote-port 43001 --link-id 264     --payload-size "$payload" --total-bytes "$TOTAL_BYTES" >>"$RESULTS"
  source_rc=$?
  wait "$sink_pid"
  sink_rc=$?
  set -e
  if (( source_rc != 0 || sink_rc != 0 )); then
    echo "[udp-perf] FAIL payload=$payload source=$source_rc sink=$sink_rc" >&2
    exit 1
  fi
done

echo "[udp-perf] PASS results=$RESULTS"
