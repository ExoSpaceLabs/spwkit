#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 /path/to/vspwd /path/to/backend_contract_device" >&2
  exit 2
fi

daemon="$1"
contract="$2"
tmpdir="$(mktemp -d)"
socket_path="$tmpdir/vspwd.sock"
daemon_log="$tmpdir/vspwd.log"
contract_log="$tmpdir/contract.log"
daemon_pid=""

cleanup() {
  set +e
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

if [[ ! -S "$socket_path" ]]; then
  cat "$daemon_log" >&2 || true
  exit 1
fi

if ! timeout 45s "$contract" "$socket_path" >"$contract_log" 2>&1; then
  cat "$contract_log" >&2 || true
  cat "$daemon_log" >&2 || true
  exit 1
fi

kill -TERM "$daemon_pid"
if ! wait "$daemon_pid"; then
  cat "$daemon_log" >&2 || true
  exit 1
fi
daemon_pid=""
