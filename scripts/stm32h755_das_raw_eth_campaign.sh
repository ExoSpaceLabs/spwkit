#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INTERFACE=""
OUTPUT_ROOT="${SPWKIT_DAS_RAW_CAMPAIGN_ROOT:-$ROOT_DIR/build/das-raw-eth-campaign}"
CLOCKS="64000000 200000000 300000000 400000000"
PAYLOADS="0,16,64,128,256,512,1024,1200,1400,4096"
ITERATIONS=128
WARMUP=16
BULK_BYTES=1073741824
BULK_PAYLOAD=4096
REMOTE_PEER_SSH=""
REMOTE_PEER_BIN=""

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/stm32h755_das_raw_eth_campaign.sh [--interface IFACE] [options]

Runs the physical PC<->NUCLEO-H755ZI-Q raw-Ethernet HIL campaign at every
DAS-supported stock-board CM7 frequency and aggregates the evidence.

Options:
  --interface IFACE       optional if exactly one wired NIC has carrier
  --peer-ssh USER@HOST    run native RAW Ethernet peer on Raspberry Pi via SSH
  --peer-bin PATH         path to the Pi prebuilt host peer executable
  --output-root DIR
  --clocks "HZ ..."       default: 64000000 200000000 300000000 400000000
  --payloads CSV          default: 0,16,64,128,256,512,1024,1200,1400,4096
  --iterations N          default: 128
  --warmup N              default: 16
  --bulk-bytes N          default: 1073741824 (1 GiB)
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
    --payloads) PAYLOADS="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --bulk-bytes) BULK_BYTES="$2"; shift 2 ;;
    --bulk-payload) BULK_PAYLOAD="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

source "$ROOT_DIR/scripts/lib/hil_prerequisites.sh"
spwkit_hil_prepare_tools
INTERFACE="$(spwkit_hil_detect_interface "$INTERFACE")"
echo "[campaign] selected physical NIC: $INTERFACE"

rm -rf -- "$OUTPUT_ROOT"
mkdir -p "$OUTPUT_ROOT"

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

python3 "$ROOT_DIR/scripts/summarize_stm32h755_das_raw_eth_campaign.py"   --root "$OUTPUT_ROOT"   --csv "$OUTPUT_ROOT/results.csv"   --markdown "$OUTPUT_ROOT/README.md"

echo "[campaign] PASS"
echo "[campaign] CSV:      $OUTPUT_ROOT/results.csv"
echo "[campaign] Markdown: $OUTPUT_ROOT/README.md"
