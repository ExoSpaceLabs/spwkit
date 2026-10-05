#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE="$ROOT_DIR/benchmarks/transport/docker/compose.yml"
PAYLOADS="64 256 1024 1400 4096 16384 65536 262144 1048576"
TOTAL_BYTES=1073741824
MODE=both
REPEATS=1
OUTPUT_DIR="${SPWKIT_DOCKER_UDP_PERF_RESULTS:-$ROOT_DIR/build/transport-udp-docker}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --payloads) PAYLOADS="$2"; shift 2 ;;
    --total-bytes) TOTAL_BYTES="$2"; shift 2 ;;
    --mode) MODE="$2"; shift 2 ;;
    --repeats) REPEATS="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    -h|--help)
      echo "usage: $0 [--payloads "N ..."] [--total-bytes N] [--mode uni|duplex|both] [--repeats N]"
      exit 0
      ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done

case "$MODE" in uni|duplex|both) ;; *) echo "--mode must be uni, duplex, or both" >&2; exit 2 ;; esac
command -v docker >/dev/null 2>&1 || { echo "docker not found" >&2; exit 2; }
docker compose version >/dev/null
[[ "$REPEATS" =~ ^[1-9][0-9]*$ ]] || { echo "--repeats must be >= 1" >&2; exit 2; }

rm -rf -- "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

echo "[docker] building benchmark image once"
docker compose -f "$COMPOSE" build

run_profile() {
  local profile="$1"
  local payload="$2"
  local repeat="$3"
  local log="$OUTPUT_DIR/${profile}-${payload}-r$(printf '%02d' "$repeat").log"
  echo "[docker-udp] profile=$profile payload=$payload"
  PAYLOAD_SIZE="$payload" TOTAL_BYTES="$TOTAL_BYTES"     docker compose -f "$COMPOSE" --profile "$profile" up --no-build --abort-on-container-exit --exit-code-from       "$([[ "$profile" == "uni" ]] && echo uni-source || echo duplex-a)"       2>&1 | tee "$log"
  docker compose -f "$COMPOSE" --profile "$profile" down -v --remove-orphans
}

for payload in $PAYLOADS; do
  for ((repeat = 1; repeat <= REPEATS; ++repeat)); do
    if [[ "$MODE" == "uni" || "$MODE" == "both" ]]; then
      run_profile uni "$payload" "$repeat"
    fi
    if [[ "$MODE" == "duplex" || "$MODE" == "both" ]]; then
      run_profile duplex "$payload" "$repeat"
    fi
  done
done

echo "[docker-udp] PASS logs=$OUTPUT_DIR"
