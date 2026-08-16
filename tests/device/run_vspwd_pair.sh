#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 /path/to/vspwd /path/to/vspwd_raw_peer" >&2
  exit 2
fi

daemon="$1"
peer="$2"
tmpdir="$(mktemp -d)"
socket_path="$tmpdir/vspwd.sock"
daemon_log="$tmpdir/vspwd.log"
survivor_log="$tmpdir/survivor.log"
initial_log="$tmpdir/initial.log"
restart_log="$tmpdir/restart.log"
daemon_pid=""
survivor_pid=""

cleanup() {
  set +e
  if [[ -n "$survivor_pid" ]] && kill -0 "$survivor_pid" 2>/dev/null; then
    kill "$survivor_pid" 2>/dev/null || true
    wait "$survivor_pid" 2>/dev/null || true
  fi
  if [[ -n "$daemon_pid" ]] && kill -0 "$daemon_pid" 2>/dev/null; then
    kill -TERM "$daemon_pid" 2>/dev/null || true
    wait "$daemon_pid" 2>/dev/null || true
  fi
  rm -rf "$tmpdir"
}
trap cleanup EXIT

"$daemon" --socket "$socket_path" >"$daemon_log" 2>&1 &
daemon_pid=$!

for _ in $(seq 1 100); do
  [[ -S "$socket_path" ]] && break
  if ! kill -0 "$daemon_pid" 2>/dev/null; then
    cat "$daemon_log" >&2 || true
    exit 1
  fi
  sleep 0.02
done
[[ -S "$socket_path" ]] || { cat "$daemon_log" >&2 || true; exit 1; }

timeout 20s "$peer" "$socket_path" survivor >"$survivor_log" 2>&1 &
survivor_pid=$!
sleep 0.15

if ! timeout 15s "$peer" "$socket_path" initial >"$initial_log" 2>&1; then
  cat "$initial_log" >&2 || true
  cat "$survivor_log" >&2 || true
  cat "$daemon_log" >&2 || true
  exit 1
fi

# Give the surviving port enough time to observe ERROR_WAIT before the new
# process attaches, so restart recovery cannot hide the loss transition.
sleep 0.25

if ! timeout 15s "$peer" "$socket_path" restart >"$restart_log" 2>&1; then
  cat "$restart_log" >&2 || true
  cat "$survivor_log" >&2 || true
  cat "$daemon_log" >&2 || true
  exit 1
fi

if ! wait "$survivor_pid"; then
  cat "$survivor_log" >&2 || true
  cat "$initial_log" >&2 || true
  cat "$restart_log" >&2 || true
  cat "$daemon_log" >&2 || true
  exit 1
fi
survivor_pid=""

kill -TERM "$daemon_pid"
if ! wait "$daemon_pid"; then
  cat "$daemon_log" >&2 || true
  exit 1
fi
daemon_pid=""
