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
if ! command -v ip >/dev/null 2>&1; then
    echo "iproute2 is required for network-namespace integration" >&2
    exit 2
fi
if ! command -v sudo >/dev/null 2>&1 || ! sudo -n true 2>/dev/null; then
    echo "passwordless sudo/CAP_NET_ADMIN is required for network namespaces" >&2
    exit 2
fi

suffix="$$"
ns_a="spwkit-a-${suffix}"
ns_b="spwkit-b-${suffix}"
if_a="vsa${suffix}"
if_b="vsb${suffix}"
# Linux interface names are limited to IFNAMSIZ-1 (15) bytes.
if_a="${if_a:0:15}"
if_b="${if_b:0:15}"

workdir="$(mktemp -d)"
a_pid=""
b_pid=""
cleanup() {
    [[ -z "$a_pid" ]] || kill "$a_pid" 2>/dev/null || true
    [[ -z "$b_pid" ]] || kill "$b_pid" 2>/dev/null || true
    sudo ip netns del "$ns_a" 2>/dev/null || true
    sudo ip netns del "$ns_b" 2>/dev/null || true
    rm -rf "$workdir"
}
trap cleanup EXIT

sudo ip netns add "$ns_a"
sudo ip netns add "$ns_b"
sudo ip link add "$if_a" type veth peer name "$if_b"
sudo ip link set "$if_a" netns "$ns_a"
sudo ip link set "$if_b" netns "$ns_b"

sudo ip -n "$ns_a" link set lo up
sudo ip -n "$ns_b" link set lo up
sudo ip -n "$ns_a" address add 10.231.0.1/24 dev "$if_a"
sudo ip -n "$ns_b" address add 10.231.0.2/24 dev "$if_b"
sudo ip -n "$ns_a" link set "$if_a" up
sudo ip -n "$ns_b" link set "$if_b" up

# Keep the normal Ethernet MTU so the 8 KiB application packet must traverse
# VSPW-TP fragmentation/reassembly across an actual isolated IP boundary.
sudo ip -n "$ns_a" link set "$if_a" mtu 1500
sudo ip -n "$ns_b" link set "$if_b" mtu 1500

a_port=48000
b_port=48001
link_id=$((0x311000 + ($$ % 4095)))
a_log="$workdir/a.log"
b1_log="$workdir/b-initial.log"
b2_log="$workdir/b-restart.log"

sudo ip netns exec "$ns_a" "$peer_bin" \
    --id A --scenario survivor \
    --local-address 10.231.0.1 --remote-address 10.231.0.2 \
    --local-port "$a_port" --remote-port "$b_port" \
    --link-id "$link_id" >"$a_log" 2>&1 &
a_pid=$!

sleep 0.25

sudo ip netns exec "$ns_b" "$peer_bin" \
    --id B --scenario initial \
    --local-address 10.231.0.2 --remote-address 10.231.0.1 \
    --local-port "$b_port" --remote-port "$a_port" \
    --link-id "$link_id" >"$b1_log" 2>&1 &
b_pid=$!
wait "$b_pid"
b_pid=""

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
    echo "namespace A did not observe peer loss" >&2
    cat "$a_log" >&2
    cat "$b1_log" >&2
    exit 1
fi

sudo ip netns exec "$ns_b" "$peer_bin" \
    --id B --scenario restart \
    --local-address 10.231.0.2 --remote-address 10.231.0.1 \
    --local-port "$b_port" --remote-port "$a_port" \
    --link-id "$link_id" >"$b2_log" 2>&1 &
b_pid=$!

wait "$b_pid"
b_pid=""
wait "$a_pid"
a_pid=""

grep -q '^ROUND 1 OK id=A ' "$a_log"
grep -q '^ROUND 1 OK id=B ' "$b1_log"
grep -q '^PEER_LOST id=A$' "$a_log"
grep -q '^PEER_RECOVERED id=A$' "$a_log"
grep -q '^ROUND 2 OK id=A ' "$a_log"
grep -q '^ROUND 2 OK id=B ' "$b2_log"
grep -q '^PASS id=A scenario=survivor$' "$a_log"
grep -q '^PASS id=B scenario=initial$' "$b1_log"
grep -q '^PASS id=B scenario=restart$' "$b2_log"

cat "$a_log"
cat "$b1_log"
cat "$b2_log"
echo "NETNS_PASS"
