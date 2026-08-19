#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 /path/to/c_peer /path/to/cpp_peer" >&2
    exit 2
fi

c_peer="$(readlink -f "$1")"
cpp_peer="$(readlink -f "$2")"
for peer in "$c_peer" "$cpp_peer"; do
    if [[ ! -x "$peer" ]]; then
        echo "peer executable not found: $peer" >&2
        exit 2
    fi
done

run_pair() {
    local a_bin="$1"
    local b_bin="$2"
    local label="$3"
    local workdir
    local a_pid=""
    local b_pid=""
    local base_port
    local a_port
    local b_port
    local link_id

    workdir="$(mktemp -d)"
    base_port=$((49000 + ($$ % 300) * 2 + RANDOM % 20))
    a_port="$base_port"
    b_port=$((base_port + 1))
    link_id=$((0x410000 + ($$ % 65535) + RANDOM))

    cleanup_pair() {
        [[ -z "$a_pid" ]] || kill "$a_pid" 2>/dev/null || true
        [[ -z "$b_pid" ]] || kill "$b_pid" 2>/dev/null || true
        rm -rf "$workdir"
    }

    "$a_bin" \
        --id A --scenario survivor \
        --local-port "$a_port" --remote-port "$b_port" \
        --link-id "$link_id" >"$workdir/a.log" 2>&1 &
    a_pid=$!

    sleep 0.25

    "$b_bin" \
        --id B --scenario initial \
        --local-port "$b_port" --remote-port "$a_port" \
        --link-id "$link_id" >"$workdir/b-initial.log" 2>&1 &
    b_pid=$!
    if ! wait "$b_pid"; then
        cat "$workdir/a.log" "$workdir/b-initial.log" >&2
        cleanup_pair
        return 1
    fi
    b_pid=""

    local peer_lost=0
    for _ in $(seq 1 300); do
        if grep -q '^PEER_LOST id=A$' "$workdir/a.log"; then
            peer_lost=1
            break
        fi
        if ! kill -0 "$a_pid" 2>/dev/null; then
            break
        fi
        sleep 0.05
    done

    if [[ "$peer_lost" -ne 1 ]]; then
        echo "$label: A did not observe peer loss" >&2
        cat "$workdir/a.log" "$workdir/b-initial.log" >&2
        cleanup_pair
        return 1
    fi

    "$b_bin" \
        --id B --scenario restart \
        --local-port "$b_port" --remote-port "$a_port" \
        --link-id "$link_id" >"$workdir/b-restart.log" 2>&1 &
    b_pid=$!
    if ! wait "$b_pid"; then
        cat "$workdir/a.log" "$workdir/b-initial.log" "$workdir/b-restart.log" >&2
        cleanup_pair
        return 1
    fi
    b_pid=""

    if ! wait "$a_pid"; then
        cat "$workdir/a.log" "$workdir/b-initial.log" "$workdir/b-restart.log" >&2
        cleanup_pair
        return 1
    fi
    a_pid=""

    grep -q '^ROUND 1 OK id=A ' "$workdir/a.log"
    grep -q '^ROUND 1 OK id=B ' "$workdir/b-initial.log"
    grep -q '^PEER_RECOVERED id=A$' "$workdir/a.log"
    grep -q '^ROUND 2 OK id=A ' "$workdir/a.log"
    grep -q '^ROUND 2 OK id=B ' "$workdir/b-restart.log"
    grep -q '^PASS id=A scenario=survivor$' "$workdir/a.log"
    grep -q '^PASS id=B scenario=initial$' "$workdir/b-initial.log"
    grep -q '^PASS id=B scenario=restart$' "$workdir/b-restart.log"

    cat "$workdir/a.log" "$workdir/b-initial.log" "$workdir/b-restart.log"
    echo "CROSS_LANGUAGE_PASS $label"
    cleanup_pair
}

run_pair "$c_peer" "$c_peer" "C/C"
run_pair "$cpp_peer" "$cpp_peer" "C++/C++"
run_pair "$c_peer" "$cpp_peer" "C/C++"
run_pair "$cpp_peer" "$c_peer" "C++/C"

echo "CROSS_LANGUAGE_MATRIX_PASS"
