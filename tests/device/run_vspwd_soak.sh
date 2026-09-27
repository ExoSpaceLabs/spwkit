#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 /path/to/vspwd /path/to/device_public_peer /path/to/device_daemon_restart_peer" >&2
  exit 2
fi

daemon="$1"
peer="$2"
daemon_restart_peer="$3"
iterations="${SPWKIT_SOAK_ITERATIONS:-10}"

[[ "$iterations" =~ ^[0-9]+$ ]] || {
  echo "SPWKIT_SOAK_ITERATIONS must be an integer" >&2
  exit 2
}
if (( iterations < 1 || iterations > 10000 )); then
  echo "SPWKIT_SOAK_ITERATIONS must be between 1 and 10000" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for ((i = 1; i <= iterations; ++i)); do
  echo "[vspwd-soak] iteration $i/$iterations"
  # run_vspwd_pair starts a fresh daemon, disconnects/restarts one client while
  # the other survives, verifies recovery, and then terminates the daemon.
  # Repeating the complete fixture therefore covers both daemon lifecycle and
  # client restart/reconnect without maintaining a second protocol harness.
  bash "$script_dir/run_vspwd_pair.sh" "$daemon" "$peer"
  bash "$script_dir/run_vspwd_daemon_restart.sh" "$daemon" "$daemon_restart_peer"
done
