#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 VSPWD SPWCTL HOLD_PEER" >&2
  exit 2
fi

vspwd=$1
spwctl=$2
peer=$3
tmpdir=$(mktemp -d)
socket="$tmpdir/vspwd.sock"
stop_file="$tmpdir/stop"
daemon_pid=""
peer0_pid=""
peer1_pid=""

cleanup() {
  set +e
  touch "$stop_file" 2>/dev/null || true
  [[ -n "$peer0_pid" ]] && kill "$peer0_pid" 2>/dev/null || true
  [[ -n "$peer1_pid" ]] && kill "$peer1_pid" 2>/dev/null || true
  [[ -n "$peer0_pid" ]] && wait "$peer0_pid" 2>/dev/null || true
  [[ -n "$peer1_pid" ]] && wait "$peer1_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && kill -TERM "$daemon_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && wait "$daemon_pid" 2>/dev/null || true
  rm -rf "$tmpdir"
}
trap cleanup EXIT

"$vspwd" --socket "$socket" >"$tmpdir/vspwd.log" 2>&1 &
daemon_pid=$!
for _ in $(seq 1 100); do
  [[ -S "$socket" ]] && break
  sleep 0.02
done
[[ -S "$socket" ]] || { cat "$tmpdir/vspwd.log"; exit 1; }

"$spwctl" --socket "$socket" list >"$tmpdir/list-empty.txt"
grep -Eq '^0 no no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-empty.txt"
grep -Eq '^1 no no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-empty.txt"

if "$spwctl" --socket "$socket" show 99 >"$tmpdir/invalid.out" 2>"$tmpdir/invalid.err"; then
  echo "invalid management port unexpectedly succeeded" >&2
  exit 1
fi
grep -q 'invalid argument' "$tmpdir/invalid.err"

"$peer" "$socket" 0 "$stop_file" >"$tmpdir/peer0.log" 2>&1 &
peer0_pid=$!
"$peer" "$socket" 1 "$stop_file" >"$tmpdir/peer1.log" 2>&1 &
peer1_pid=$!
for _ in $(seq 1 250); do
  if grep -q '^RUN$' "$tmpdir/peer0.log" 2>/dev/null &&
     grep -q '^RUN$' "$tmpdir/peer1.log" 2>/dev/null; then
    break
  fi
  kill -0 "$peer0_pid" 2>/dev/null || { cat "$tmpdir/peer0.log"; exit 1; }
  kill -0 "$peer1_pid" 2>/dev/null || { cat "$tmpdir/peer1.log"; exit 1; }
  sleep 0.02
done
grep -q '^RUN$' "$tmpdir/peer0.log"
grep -q '^RUN$' "$tmpdir/peer1.log"

"$spwctl" --socket "$socket" list >"$tmpdir/list-run.txt"
grep -Eq '^0 no yes yes no RUN [0-9]+/2 [0-9]+/8$' "$tmpdir/list-run.txt"
grep -Eq '^1 no yes yes no RUN [0-9]+/2 [0-9]+/8$' "$tmpdir/list-run.txt"

"$spwctl" --socket "$socket" show 0 >"$tmpdir/show.txt"
grep -q '^attached: yes$' "$tmpdir/show.txt"
grep -q '^started: yes$' "$tmpdir/show.txt"
grep -q '^state: RUN$' "$tmpdir/show.txt"

"$spwctl" --socket "$socket" stats 0 >"$tmpdir/stats.txt"
grep -q '^tx_packets: ' "$tmpdir/stats.txt"
grep -q '^dropped_packets: ' "$tmpdir/stats.txt"
"$spwctl" --socket "$socket" clear-stats 0 >"$tmpdir/clear.txt"
grep -q '^cleared statistics for port 0$' "$tmpdir/clear.txt"
"$spwctl" --socket "$socket" stats 0 >"$tmpdir/stats-cleared.txt"
grep -q '^tx_packets: 0$' "$tmpdir/stats-cleared.txt"
grep -q '^link_errors: 0$' "$tmpdir/stats-cleared.txt"

# Management operations must not steal or alter application ownership/lifecycle.
"$spwctl" --socket "$socket" show 0 >"$tmpdir/show-after.txt"
grep -q '^attached: yes$' "$tmpdir/show-after.txt"
grep -q '^started: yes$' "$tmpdir/show-after.txt"
grep -q '^state: RUN$' "$tmpdir/show-after.txt"

touch "$stop_file"
wait "$peer0_pid"
peer0_pid=""
wait "$peer1_pid"
peer1_pid=""

"$spwctl" --socket "$socket" list >"$tmpdir/list-detached.txt"
grep -Eq '^0 no no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-detached.txt"
grep -Eq '^1 no no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-detached.txt"
