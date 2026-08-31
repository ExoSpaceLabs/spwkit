#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compose_file="$repo_root/integrations/ccsdspack_v2/compose.yml"
project="spwkit-ccsds-${GITHUB_RUN_ID:-local}-$$"
image="spwkit-ccsds-v2:${GITHUB_RUN_ID:-local}-$$"

if ! docker compose version >/dev/null 2>&1; then
    echo "docker compose v2 is required" >&2
    exit 2
fi

cleanup() {
    docker compose -p "$project" -f "$compose_file" down -v --remove-orphans >/dev/null 2>&1 || true
    docker image rm "$image" >/dev/null 2>&1 || true
}
trap cleanup EXIT

cd "$repo_root"
export SPWKIT_CCSDS_IMAGE="$image"

docker compose -p "$project" -f "$compose_file" build peer-a
docker compose -p "$project" -f "$compose_file" up -d --no-build

a_id="$(docker compose -p "$project" -f "$compose_file" ps -q peer-a)"
b_id="$(docker compose -p "$project" -f "$compose_file" ps -q peer-b)"
if [[ -z "$a_id" || -z "$b_id" ]]; then
    docker compose -p "$project" -f "$compose_file" logs --no-color >&2 || true
    exit 1
fi

if ! a_status="$(timeout 60s docker wait "$a_id")"; then
    echo "CCSDS peer A did not exit" >&2
    docker compose -p "$project" -f "$compose_file" logs --no-color >&2 || true
    exit 1
fi
if ! b_status="$(timeout 60s docker wait "$b_id")"; then
    echo "CCSDS peer B did not exit" >&2
    docker compose -p "$project" -f "$compose_file" logs --no-color >&2 || true
    exit 1
fi

a_log="$(docker logs "$a_id" 2>&1)"
b_log="$(docker logs "$b_id" 2>&1)"
printf '%s\n' "$a_log"
printf '%s\n' "$b_log"

if [[ "$a_status" != "0" || "$b_status" != "0" ]]; then
    echo "CCSDS Compose exit codes A=$a_status B=$b_status" >&2
    exit 1
fi

grep -q '^PASS backend=udp id=A tx=PUS:revC:TC rx=PUS:revC:TM .* terminator=EOP$' <<<"$a_log"
grep -q '^PASS backend=udp id=B tx=PUS:revC:TM rx=PUS:revC:TC .* terminator=EOP$' <<<"$b_log"

echo "CCSDS_ETHERNET_COMPOSE_PASS"
