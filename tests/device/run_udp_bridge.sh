#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
  echo "usage: $0 VSPWD DEVICE_PEER UDP_PEER SPWCTL DEVICE_EXAMPLE" >&2
  exit 2
fi

vspwd="$1"
device_peer="$2"
udp_peer="$3"
spwctl="$4"
device_example="$5"
tmpdir="$(mktemp -d)"
socket="$tmpdir/vspwd.sock"
base=$((42000 + ($$ % 1000) * 2))
bridge_udp_port="$base"
remote_udp_port=$((base + 1))
daemon_pid=""
device_pid=""

cleanup() {
  set +e
  [[ -n "$device_pid" ]] && kill "$device_pid" 2>/dev/null || true
  [[ -n "$device_pid" ]] && wait "$device_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && kill -TERM "$daemon_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && wait "$daemon_pid" 2>/dev/null || true
  rm -rf "$tmpdir"
}
trap cleanup EXIT

"$vspwd" --socket "$socket" \
  --bridge-port 1 \
  --udp-local-port "$bridge_udp_port" \
  --udp-remote-port "$remote_udp_port" \
  --udp-link-id 4242 \
  --udp-ack-timeout-ms 50 \
  --udp-keepalive-ms 100 \
  --udp-peer-timeout-ms 500 >"$tmpdir/vspwd.log" 2>&1 &
daemon_pid=$!
for _ in $(seq 1 200); do
  [[ -S "$socket" ]] && break
  sleep 0.02
done
[[ -S "$socket" ]] || { cat "$tmpdir/vspwd.log"; exit 1; }

"$spwctl" --socket "$socket" list >"$tmpdir/list.log"
grep -Eq '^1[[:space:]]+yes[[:space:]]+no[[:space:]]+yes' "$tmpdir/list.log"
if timeout 3s "$device_example" "$socket" 1 >"$tmpdir/reserved.log" 2>&1; then
  echo "bridged port unexpectedly accepted a normal device attachment" >&2
  cat "$tmpdir/reserved.log"
  exit 1
fi

"$device_peer" "$socket" 0 >"$tmpdir/device.log" 2>&1 &
device_pid=$!
sleep 0.1

timeout 20s "$udp_peer" \
  --id B \
  --local-port "$remote_udp_port" \
  --remote-port "$bridge_udp_port" \
  --link-id 4242 \
  --scenario initial >"$tmpdir/udp-initial.log" 2>&1 || {
    cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-initial.log"
    exit 1
  }

for _ in $(seq 1 500); do
  grep -q '^PEER_LOST$' "$tmpdir/device.log" && break
  sleep 0.02
done
grep -q '^PEER_LOST$' "$tmpdir/device.log" || {
  cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-initial.log"
  exit 1
}

timeout 20s "$udp_peer" \
  --id B \
  --local-port "$remote_udp_port" \
  --remote-port "$bridge_udp_port" \
  --link-id 4242 \
  --scenario restart >"$tmpdir/udp-restart.log" 2>&1 || {
    cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-restart.log"
    exit 1
  }

wait "$device_pid" || {
  cat "$tmpdir/vspwd.log" "$tmpdir/device.log" "$tmpdir/udp-initial.log" "$tmpdir/udp-restart.log"
  exit 1
}
device_pid=""
grep -q '^PEER_RECOVERED$' "$tmpdir/device.log"
grep -q '^PASS device survivor$' "$tmpdir/device.log"
grep -q '^PASS id=B scenario=initial$' "$tmpdir/udp-initial.log"
grep -q '^PASS id=B scenario=restart$' "$tmpdir/udp-restart.log"
