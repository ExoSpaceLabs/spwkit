#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec "$ROOT_DIR/benchmarks/run_profile_campaign.sh" \
  --controlled-cpu auto \
  --precondition-seconds 2 \
  --governor performance \
  --settle-seconds 0 \
  "$@"
