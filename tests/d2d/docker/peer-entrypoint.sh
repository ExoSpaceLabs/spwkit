#!/usr/bin/env bash
set -euo pipefail

: "${SPWKIT_IMPL:?SPWKIT_IMPL must be c or cpp}"
: "${SPWKIT_ID:?SPWKIT_ID must be A or B}"
: "${SPWKIT_LOCAL_ADDRESS:?SPWKIT_LOCAL_ADDRESS is required}"
: "${SPWKIT_REMOTE_ADDRESS:?SPWKIT_REMOTE_ADDRESS is required}"
: "${SPWKIT_LOCAL_PORT:?SPWKIT_LOCAL_PORT is required}"
: "${SPWKIT_REMOTE_PORT:?SPWKIT_REMOTE_PORT is required}"
: "${SPWKIT_LINK_ID:?SPWKIT_LINK_ID is required}"

case "$SPWKIT_IMPL" in
    c) peer=/usr/local/bin/spwkit_udp_peer_c ;;
    cpp) peer=/usr/local/bin/spwkit_udp_peer_cpp ;;
    *)
        echo "invalid SPWKIT_IMPL: $SPWKIT_IMPL" >&2
        exit 2
        ;;
esac

case "$SPWKIT_ID" in
    A|B) ;;
    *)
        echo "invalid SPWKIT_ID: $SPWKIT_ID" >&2
        exit 2
        ;;
esac

common=(
    --id "$SPWKIT_ID"
    --local-address "$SPWKIT_LOCAL_ADDRESS"
    --remote-address "$SPWKIT_REMOTE_ADDRESS"
    --local-port "$SPWKIT_LOCAL_PORT"
    --remote-port "$SPWKIT_REMOTE_PORT"
    --link-id "$SPWKIT_LINK_ID"
)

if [[ "$SPWKIT_ID" == "A" ]]; then
    exec "$peer" "${common[@]}" --scenario survivor
fi

"$peer" "${common[@]}" --scenario initial
# The remote liveness timeout is 500 ms. Keep the container alive while the
# process is absent long enough for peer A to observe ERROR_WAIT, then create a
# genuinely fresh peer process with a new VSPW-TP session identity.
sleep 1
exec "$peer" "${common[@]}" --scenario restart
