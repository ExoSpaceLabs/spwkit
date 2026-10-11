#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INTERFACE=""
OUTPUT_ROOT="${SPWKIT_DAS_RAW_CAMPAIGN_ROOT:-$ROOT_DIR/build/das-raw-eth-campaign}"
CLOCKS=""
PAYLOADS="0,16,64,128,256,512,1024,1200,1400,4096"
ITERATIONS=128
WARMUP=16
BULK_BYTES=""
PROFILE=benchmark
BULK_PAYLOAD=4096
REMOTE_PEER_SSH=""
REMOTE_PEER_BIN=""

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/stm32h755_das_raw_eth_campaign.sh [--interface IFACE] [options]

Runs a repeatable physical STM32H755 RAW Ethernet campaign. The DEFAULT is a
short performance benchmark covering ALL four CM7 clocks. A separate opt-in
stress profile performs a sustained 1 GiB transfer at 400 MHz by default.

Options:
  --interface IFACE       optional if exactly one wired NIC has carrier
  --peer-ssh USER@HOST    run native RAW Ethernet peer on Raspberry Pi via SSH
  --peer-bin PATH         path to the Pi prebuilt host peer executable
  --output-root DIR
  --profile MODE         benchmark (default) or stress (explicit opt-in)
  --clocks "HZ ..."       benchmark: all four CM7 clocks; stress: 400000000
  --payloads CSV          default: 0,16,64,128,256,512,1024,1200,1400,4096
  --iterations N          default: 128
  --warmup N              default: 16
  --bulk-bytes N          benchmark: 16777216 (16 MiB); stress: 1073741824 (1 GiB)
  --bulk-payload N        default: 4096
  -h, --help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --interface) INTERFACE="$2"; shift 2 ;;
    --peer-ssh) REMOTE_PEER_SSH="$2"; shift 2 ;;
    --peer-bin) REMOTE_PEER_BIN="$2"; shift 2 ;;
    --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
    --clocks) CLOCKS="$2"; shift 2 ;;
    --profile) PROFILE="$2"; shift 2 ;;
    --payloads) PAYLOADS="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --bulk-bytes) BULK_BYTES="$2"; shift 2 ;;
    --bulk-payload) BULK_PAYLOAD="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$PROFILE" in
  benchmark)
    : "${CLOCKS:=64000000 200000000 300000000 400000000}"
    : "${BULK_BYTES:=16777216}"
    ;;
  stress)
    : "${CLOCKS:=400000000}"
    : "${BULK_BYTES:=1073741824}"
    ;;
  *)
    echo "--profile must be benchmark or stress" >&2
    exit 2
    ;;
esac
[[ "$BULK_BYTES" =~ ^[0-9]+$ ]] && (( BULK_BYTES >= 1 )) || {
  echo "--bulk-bytes must be a positive integer" >&2
  exit 2
}
echo "[campaign] profile=$PROFILE clocks=${CLOCKS} bulk_bytes=$BULK_BYTES (verified echo)"
echo "[campaign] benchmark = throughput/RTT/clock scaling; stress = long-term reliability"

source "$ROOT_DIR/scripts/lib/hil_prerequisites.sh"
spwkit_hil_prepare_tools
INTERFACE="$(spwkit_hil_detect_interface "$INTERFACE")"
echo "[campaign] selected physical NIC: $INTERFACE"

rm -rf -- "$OUTPUT_ROOT"
mkdir -p "$OUTPUT_ROOT"

cat > "$OUTPUT_ROOT/profile.txt" <<EOF
profile=$PROFILE
clocks_hz=$CLOCKS
bulk_bytes=$BULK_BYTES
bulk_payload_bytes=$BULK_PAYLOAD
iterations=$ITERATIONS
warmup=$WARMUP
EOF

for clock_hz in $CLOCKS; do
  case "$clock_hz" in
    64000000|200000000|300000000|400000000) ;;
    *) echo "Unsupported DAS clock in --clocks: $clock_hz" >&2; exit 2 ;;
  esac

  mhz=$((clock_hz / 1000000))
  run_root="$OUTPUT_ROOT/clock-${mhz}mhz"
  echo "[campaign] ===== ${mhz} MHz ====="
  extra_peer_args=()
  if [[ -n "$REMOTE_PEER_SSH" ]]; then
    extra_peer_args+=(--peer-ssh "$REMOTE_PEER_SSH" --peer-bin "$REMOTE_PEER_BIN")
  fi
  bash "$ROOT_DIR/scripts/stm32h755_das_raw_eth_test.sh" \
    --interface "$INTERFACE" \
    --build-root "$run_root" \
    --clock-hz "$clock_hz" \
    --payloads "$PAYLOADS" \
    --iterations "$ITERATIONS" \
    --warmup "$WARMUP" \
    --bulk-bytes "$BULK_BYTES" \
    --bulk-payload "$BULK_PAYLOAD" \
    "${extra_peer_args[@]}"
done

host_label=pc
if [[ -n "$REMOTE_PEER_SSH" ]]; then host_label=pi; fi
python3 "$ROOT_DIR/scripts/summarize_stm32h755_das_raw_eth_campaign.py" \
  --root "$OUTPUT_ROOT" --csv "$OUTPUT_ROOT/results.csv" \
  --markdown "$OUTPUT_ROOT/README.md" --linux-host "$host_label" \
  --profile "$PROFILE"

echo "[campaign] PASS"
echo "[campaign] CSV:      $OUTPUT_ROOT/results.csv"
echo "[campaign] Markdown: $OUTPUT_ROOT/README.md"
