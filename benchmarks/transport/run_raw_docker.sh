#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE="$ROOT_DIR/benchmarks/transport/docker/compose.yml"
PAYLOADS="64 256 1024 1400 4096 16384 65536 262144 1048576"
TOTAL_BYTES=1073741824
MODE=both
OUTPUT_DIR="${SPWKIT_DOCKER_RAW_PERF_RESULTS:-$ROOT_DIR/build/transport-raw-docker}"

usage() {
  cat <<'USAGE'
Usage:
  bash benchmarks/transport/run_raw_docker.sh [options]

Runs verified VSPW RAW Ethernet traffic between two containers over Docker's
bridge/veth Layer-2 path.

Options:
  --payloads "N ..."    logical payload sizes
  --total-bytes N       bytes per direction/case (default 1 GiB)
  --mode uni|duplex|both
  --output-dir DIR
  -h, --help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --payloads) PAYLOADS="$2"; shift 2 ;;
    --total-bytes) TOTAL_BYTES="$2"; shift 2 ;;
    --mode) MODE="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$MODE" in
  uni|duplex|both) ;;
  *) echo "--mode must be uni, duplex, or both" >&2; exit 2 ;;
esac

command -v docker >/dev/null 2>&1 || {
  echo "docker not found" >&2
  exit 2
}
docker compose version >/dev/null

rm -rf -- "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

cleanup() {
  docker compose -f "$COMPOSE" --profile raw-uni --profile raw-duplex     down -v --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

run_profile() {
  local profile="$1"
  local payload="$2"
  local log="$OUTPUT_DIR/${profile}-${payload}.log"
  local exit_service

  if [[ "$profile" == "raw-uni" ]]; then
    exit_service=raw-uni-source
  else
    exit_service=raw-duplex-a
  fi

  echo "[docker-raw] profile=$profile payload=$payload total=$TOTAL_BYTES"
  PAYLOAD_SIZE="$payload" TOTAL_BYTES="$TOTAL_BYTES"     docker compose -f "$COMPOSE" --profile "$profile" up       --build --abort-on-container-exit --exit-code-from "$exit_service"       2>&1 | tee "$log"

  docker compose -f "$COMPOSE" --profile "$profile"     down -v --remove-orphans
}

for payload in $PAYLOADS; do
  if [[ "$MODE" == "uni" || "$MODE" == "both" ]]; then
    run_profile raw-uni "$payload"
  fi
  if [[ "$MODE" == "duplex" || "$MODE" == "both" ]]; then
    run_profile raw-duplex "$payload"
  fi
done

trap - EXIT
cleanup
echo "[docker-raw] PASS logs=$OUTPUT_DIR"
