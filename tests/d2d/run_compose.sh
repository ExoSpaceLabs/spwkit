#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compose_file="$repo_root/tests/d2d/compose.yml"
project="spwkit-d2d-${GITHUB_RUN_ID:-local}-$$"
image="spwkit-d2d-peer:${GITHUB_RUN_ID:-local}-$$"

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
docker build -f tests/d2d/docker/Dockerfile -t "$image" .
export SPWKIT_D2D_IMAGE="$image"

run_combo() {
    local a_impl="$1"
    local b_impl="$2"
    local label="$3"
    local index="$4"
    local a_id
    local b_id
    local a_status
    local b_status
    local a_log
    local b_log

    export SPWKIT_PEER_A_IMPL="$a_impl"
    export SPWKIT_PEER_B_IMPL="$b_impl"
    export SPWKIT_LINK_ID=$((0x520000 + index))

    docker compose -p "$project" -f "$compose_file" up -d --force-recreate
    a_id="$(docker compose -p "$project" -f "$compose_file" ps -q peer-a)"
    b_id="$(docker compose -p "$project" -f "$compose_file" ps -q peer-b)"
    if [[ -z "$a_id" || -z "$b_id" ]]; then
        docker compose -p "$project" -f "$compose_file" logs --no-color >&2 || true
        return 1
    fi

    if ! a_status="$(timeout 45s docker wait "$a_id")"; then
        echo "$label: peer A did not exit" >&2
        docker compose -p "$project" -f "$compose_file" logs --no-color >&2 || true
        return 1
    fi
    if ! b_status="$(timeout 45s docker wait "$b_id")"; then
        echo "$label: peer B did not exit" >&2
        docker compose -p "$project" -f "$compose_file" logs --no-color >&2 || true
        return 1
    fi

    a_log="$(docker logs "$a_id" 2>&1)"
    b_log="$(docker logs "$b_id" 2>&1)"
    printf '%s\n' "$a_log"
    printf '%s\n' "$b_log"

    if [[ "$a_status" != "0" || "$b_status" != "0" ]]; then
        echo "$label: container exit codes A=$a_status B=$b_status" >&2
        return 1
    fi

    grep -q '^ROUND 1 OK id=A ' <<<"$a_log"
    grep -q '^PEER_LOST id=A$' <<<"$a_log"
    grep -q '^PEER_RECOVERED id=A$' <<<"$a_log"
    grep -q '^ROUND 2 OK id=A ' <<<"$a_log"
    grep -q '^PASS id=A scenario=survivor$' <<<"$a_log"

    grep -q '^ROUND 1 OK id=B ' <<<"$b_log"
    grep -q '^PASS id=B scenario=initial$' <<<"$b_log"
    grep -q '^ROUND 2 OK id=B ' <<<"$b_log"
    grep -q '^PASS id=B scenario=restart$' <<<"$b_log"

    echo "COMPOSE_D2D_PASS $label"
    docker compose -p "$project" -f "$compose_file" rm -sf >/dev/null
}

run_combo c c "C/C" 1
run_combo cpp cpp "C++/C++" 2
run_combo c cpp "C/C++" 3
run_combo cpp c "C++/C" 4

echo "COMPOSE_D2D_MATRIX_PASS"

# The deployment-shaped CCSDS example belongs to the same network-isolation
# gate: two containers must exchange and validate actual PUS-C packets over the
# VSPW-TP/UDP Ethernet carrier. The example installs its CCSDSPack dependency
# independently and verifies the configured ref/SHA pair before either peer is
# built. The gate fails unless both peers emit their exact PASS records.
bash "$repo_root/integrations/ccsdspack_v2/run_compose.sh"
