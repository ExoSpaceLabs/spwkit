#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 /path/to/vspwd /path/to/device_daemon_restart_peer" >&2
  exit 2
fi

daemon="$1"
peer="$2"
tmpdir="$(mktemp -d)"
socket_path="$tmpdir/vspwd.sock"
ready_marker="$tmpdir/peer.ready"
lost_marker="$tmpdir/peer.lost"
daemon_log="$tmpdir/vspwd.log"
peer_log="$tmpdir/peer.log"
daemon_pid=""
peer_pid=""

cleanup() {
  set +e
  if [[ -n "$peer_pid" ]] && kill -0 "$peer_pid" 2>/dev/null; then
    kill "$peer_pid" 2>/dev/null || true
    wait "$peer_pid" 2>/dev/null || true
  fi
  if [[ -n "$daemon_pid" ]] && kill -0 "$daemon_pid" 2>/dev/null; then
    kill -TERM "$daemon_pid" 2>/dev/null || true
    wait "$daemon_pid" 2>/dev/null || true
  fi
  rm -rf "$tmpdir"
}
trap cleanup EXIT

start_daemon() {
  : >"$daemon_log"
  "$daemon" --socket "$socket_path" >>"$daemon_log" 2>&1 &
  daemon_pid=$!

  for _ in $(seq 1 100); do
    [[ -S "$socket_path" ]] && return 0
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
      cat "$daemon_log" >&2 || true
      return 1
    fi
    sleep 0.02
  done
  cat "$daemon_log" >&2 || true
  return 1
}

wait_marker() {
  local marker="$1"
  for _ in $(seq 1 250); do
    [[ -f "$marker" ]] && return 0
    if ! kill -0 "$peer_pid" 2>/dev/null; then
      cat "$peer_log" >&2 || true
      cat "$daemon_log" >&2 || true
      return 1
    fi
    sleep 0.02
  done
  cat "$peer_log" >&2 || true
  cat "$daemon_log" >&2 || true
  return 1
}

start_daemon

timeout 30s "$peer" "$socket_path" "$ready_marker" "$lost_marker" >"$peer_log" 2>&1 &
peer_pid=$!

wait_marker "$ready_marker"

kill -TERM "$daemon_pid"
if ! wait "$daemon_pid"; then
  cat "$daemon_log" >&2 || true
  exit 1
fi
daemon_pid=""

wait_marker "$lost_marker"

start_daemon

if ! wait "$peer_pid"; then
  cat "$peer_log" >&2 || true
  cat "$daemon_log" >&2 || true
  exit 1
fi
peer_pid=""

kill -TERM "$daemon_pid"
if ! wait "$daemon_pid"; then
  cat "$daemon_log" >&2 || true
  exit 1
fi
daemon_pid=""
