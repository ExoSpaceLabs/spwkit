#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 VSPWD SPWMON HOLD_PEER" >&2
  exit 2
fi

vspwd="$1"
spwmon="$2"
hold_peer="$3"
tmpdir="$(mktemp -d)"
socket="$tmpdir/vspwd.sock"
stop0="$tmpdir/stop0"
stop1="$tmpdir/stop1"
stop1b="$tmpdir/stop1b"
daemon_pid=""
p0_pid=""
p1_pid=""
mon_pid=""

cleanup() {
  set +e
  touch "$stop0" "$stop1" "$stop1b" 2>/dev/null || true
  [[ -n "$mon_pid" ]] && kill -TERM "$mon_pid" 2>/dev/null || true
  [[ -n "$mon_pid" ]] && wait "$mon_pid" 2>/dev/null || true
  [[ -n "$p0_pid" ]] && wait "$p0_pid" 2>/dev/null || true
  [[ -n "$p1_pid" ]] && wait "$p1_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && kill -TERM "$daemon_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && wait "$daemon_pid" 2>/dev/null || true
  rm -rf "$tmpdir"
}
trap cleanup EXIT

wait_socket() {
  for _ in $(seq 1 200); do
    [[ -S "$socket" ]] && return 0
    sleep 0.02
  done
  echo "timed out waiting for daemon socket" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}

wait_grep() {
  local description="$1"
  local pattern="$2"
  local file="$3"
  for _ in $(seq 1 200); do
    if [[ -f "$file" ]] && grep -q "$pattern" "$file"; then
      return 0
    fi
    sleep 0.02
  done
  echo "timed out waiting for $description" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}

wait_recovered_run() {
  for _ in $(seq 1 200); do
    if [[ -f "$tmpdir/monitor.log" ]]; then
      local count
      count="$(grep -c '"state":"RUN"' "$tmpdir/monitor.log" || true)"
      if [[ "$count" -ge 2 ]]; then
        return 0
      fi
    fi
    sleep 0.02
  done
  echo "timed out waiting for recovered monitor RUN" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}

"$vspwd" --socket "$socket" >"$tmpdir/vspwd.log" 2>&1 & daemon_pid=$!
wait_socket

"$hold_peer" "$socket" 0 "$stop0" >"$tmpdir/p0.log" 2>&1 & p0_pid=$!
"$hold_peer" "$socket" 1 "$stop1" >"$tmpdir/p1.log" 2>&1 & p1_pid=$!
wait_grep "port 0 RUN" "^RUN$" "$tmpdir/p0.log"
wait_grep "port 1 RUN" "^RUN$" "$tmpdir/p1.log"

# Bounded mode must emit the immediate subscription snapshot and exit.
"$spwmon" --socket "$socket" --port 0 --count 1 --json >"$tmpdir/once.log" 2>"$tmpdir/once.err"
grep -q '"port":0' "$tmpdir/once.log"
grep -q '"state":"RUN"' "$tmpdir/once.log"
[[ $(wc -l <"$tmpdir/once.log") -eq 1 ]]

# Continuous mode must remain non-owning while the application peer disappears
# and returns. The ERROR_WAIT snapshot also proves statistic changes are pushed:
# vspwd increments link_errors when a started port loses its peer.
"$spwmon" --socket "$socket" --port 0 --json >"$tmpdir/monitor.log" 2>"$tmpdir/monitor.err" & mon_pid=$!
wait_grep "initial monitor RUN" '"state":"RUN"' "$tmpdir/monitor.log"

touch "$stop1"
wait "$p1_pid"
p1_pid=""
wait_grep "ERROR_WAIT snapshot" '"state":"ERROR_WAIT"' "$tmpdir/monitor.log"
grep -Eq '"link_errors":[1-9][0-9]*' "$tmpdir/monitor.log"

"$hold_peer" "$socket" 1 "$stop1b" >"$tmpdir/p1b.log" 2>&1 & p1_pid=$!
wait_grep "replacement port 1 RUN" "^RUN$" "$tmpdir/p1b.log"
wait_recovered_run

# The monitor connection did not steal port 0; its application owner is alive
# for the whole observation interval.
kill -0 "$p0_pid"
kill -TERM "$mon_pid"
wait "$mon_pid"
mon_pid=""

touch "$stop0" "$stop1b"
wait "$p0_pid"
p0_pid=""
wait "$p1_pid"
p1_pid=""
