#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/spwkit_udp_peer" >&2
    exit 2
fi

peer_bin="$(readlink -f "$1")"
if [[ ! -x "$peer_bin" ]]; then
    echo "peer executable not found: $peer_bin" >&2
    exit 2
fi

workdir="$(mktemp -d)"
a_pid=""
b_pid=""
cleanup() {
    [[ -z "$a_pid" ]] || kill "$a_pid" 2>/dev/null || true
    [[ -z "$b_pid" ]] || kill "$b_pid" 2>/dev/null || true
    rm -rf "$workdir"
}
trap cleanup EXIT

base_port=$((47000 + ($$ % 500) * 2))
a_port=$base_port
b_port=$((base_port + 1))
link_id=$((0x310000 + ($$ % 65535)))

a_log="$workdir/a.log"
b1_log="$workdir/b-initial.log"
b2_log="$workdir/b-restart.log"

"$peer_bin" \
    --id A --scenario survivor \
    --local-port "$a_port" --remote-port "$b_port" \
    --link-id "$link_id" >"$a_log" 2>&1 &
a_pid=$!

# Deliberately start the second process later. Endpoint A must remain usable in
# CONNECTING until the peer appears rather than requiring server/client order.
sleep 0.25

"$peer_bin" \
    --id B --scenario initial \
    --local-port "$b_port" --remote-port "$a_port" \
    --link-id "$link_id" >"$b1_log" 2>&1 &
b_pid=$!
wait "$b_pid"
b_pid=""

# Do not restart B until A has publicly observed the disconnect. Otherwise a
# sufficiently fast restart could legitimately replace the session before the
# peer-timeout transition becomes observable.
peer_lost=0
for _ in $(seq 1 300); do
    if grep -q '^PEER_LOST id=A$' "$a_log"; then
        peer_lost=1
        break
    fi
    if ! kill -0 "$a_pid" 2>/dev/null; then
        break
    fi
    sleep 0.05
done

if [[ "$peer_lost" -ne 1 ]]; then
    echo "A did not observe peer loss" >&2
    cat "$a_log" >&2
    cat "$b1_log" >&2
    exit 1
fi

"$peer_bin" \
    --id B --scenario restart \
    --local-port "$b_port" --remote-port "$a_port" \
    --link-id "$link_id" >"$b2_log" 2>&1 &
b_pid=$!

wait "$b_pid"
b_pid=""
wait "$a_pid"
a_pid=""

grep -q '^ROUND 1 OK id=A ' "$a_log"
grep -q '^ROUND 1 OK id=B ' "$b1_log"
grep -q '^PEER_RECOVERED id=A$' "$a_log"
grep -q '^ROUND 2 OK id=A ' "$a_log"
grep -q '^ROUND 2 OK id=B ' "$b2_log"
grep -q '^PASS id=A scenario=survivor$' "$a_log"
grep -q '^PASS id=B scenario=initial$' "$b1_log"
grep -q '^PASS id=B scenario=restart$' "$b2_log"

cat "$a_log"
cat "$b1_log"
cat "$b2_log"
echo "MULTI_PROCESS_PASS"
