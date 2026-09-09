#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1))


# LOOPBACK: the in-memory packet queue is the provider/native carrier.
replace_once(
    "src/backends/loopback/loopback_backend.c",
    '#include "backends/loopback/loopback_backend.h"\n\n#include <stdalign.h>',
    '#include "backends/loopback/loopback_backend.h"\n#include "profiling/profile.h"\n\n#include <stdalign.h>',
)
replace_once(
    "src/backends/loopback/loopback_backend.c",
    "    spw_loopback_packet_slot_t* slot;\n    (void)timeout_us;\n\n    if (backend->state != SPW_LINK_RUN) {",
    "    spw_loopback_packet_slot_t* slot;\n    (void)timeout_us;\n\n    SPW_PROFILE_TX_BACKEND_ENTRY();\n    if (backend->state != SPW_LINK_RUN) {",
)
replace_once(
    "src/backends/loopback/loopback_backend.c",
    "    slot = &backend->packets.slots[backend->packets.tail];\n",
    "    SPW_PROFILE_TX_PROVIDER_ENTRY();\n    slot = &backend->packets.slots[backend->packets.tail];\n",
)
replace_once(
    "src/backends/loopback/loopback_backend.c",
    "    ++backend->packets.count;\n\n    ++backend->statistics.tx_packets;",
    "    ++backend->packets.count;\n    /* One logical packet is now resident in the carrier queue. */\n    SPW_PROFILE_TX_PROVIDER_BOUNDARY();\n    SPW_PROFILE_RX_PROVIDER_BOUNDARY();\n\n    ++backend->statistics.tx_packets;",
)
replace_once(
    "src/backends/loopback/loopback_backend.c",
    "    --backend->packets.count;\n\n    ++backend->statistics.rx_packets;\n    backend->statistics.rx_bytes += slot->length;\n    return SPW_OK;",
    "    --backend->packets.count;\n    SPW_PROFILE_RX_PROVIDER_RETURN();\n\n    ++backend->statistics.rx_packets;\n    backend->statistics.rx_bytes += slot->length;\n    SPW_PROFILE_RX_BACKEND_RETURN();\n    return SPW_OK;",
)

# SIMULATOR: peer queue insertion is the process-local carrier handoff/data-ready point.
replace_once(
    "src/backends/virtual/simulator_backend.c",
    '#include "core/buffer_internal.h"\n#include "platform/host_sync.h"',
    '#include "core/buffer_internal.h"\n#include "platform/host_sync.h"\n#include "profiling/profile.h"',
)
replace_once(
    "src/backends/virtual/simulator_backend.c",
    "    spw_packet_space_predicate_t predicate;\n    bool ready;\n\n    if (backend->link == NULL) {",
    "    spw_packet_space_predicate_t predicate;\n    bool ready;\n\n    SPW_PROFILE_TX_BACKEND_ENTRY();\n    if (backend->link == NULL) {",
)
replace_once(
    "src/backends/virtual/simulator_backend.c",
    "    slot = &peer->packets.slots[peer->packets.tail];\n",
    "    SPW_PROFILE_TX_PROVIDER_ENTRY();\n    slot = &peer->packets.slots[peer->packets.tail];\n",
)
replace_once(
    "src/backends/virtual/simulator_backend.c",
    "    ++peer->packets.count;\n\n    ++local->statistics.tx_packets;",
    "    ++peer->packets.count;\n    /* Enqueue completes both TX carrier handoff and peer RX data-ready. */\n    SPW_PROFILE_TX_PROVIDER_BOUNDARY();\n    SPW_PROFILE_RX_PROVIDER_BOUNDARY();\n\n    ++local->statistics.tx_packets;",
)
replace_once(
    "src/backends/virtual/simulator_backend.c",
    "    --local->packets.count;\n    ++local->statistics.rx_packets;",
    "    --local->packets.count;\n    SPW_PROFILE_RX_PROVIDER_RETURN();\n    ++local->statistics.rx_packets;",
)
replace_once(
    "src/backends/virtual/simulator_backend.c",
    "    spw_host_condition_broadcast(&backend->link->condition);\n    spw_host_mutex_unlock(&backend->link->mutex);\n    return SPW_OK;\n}\n\nstatic spw_result_t simulator_send_time_code",
    "    spw_host_condition_broadcast(&backend->link->condition);\n    spw_host_mutex_unlock(&backend->link->mutex);\n    SPW_PROFILE_RX_BACKEND_RETURN();\n    return SPW_OK;\n}\n\nstatic spw_result_t simulator_send_time_code",
)

# UDP/VSPW-TP: use one logical-packet marker, not one marker per fragment/datagram.
replace_once(
    "src/backends/ethernet/udp_backend.c",
    '#include "backends/ethernet/vspw_tp.h"\n\n#include <spwkit/udp.h>',
    '#include "backends/ethernet/vspw_tp.h"\n#include "profiling/profile.h"\n\n#include <spwkit/udp.h>',
)
replace_once(
    "src/backends/ethernet/udp_backend.c",
    "    spw_result_t result;\n    spw_terminator_t effective_terminator;\n\n    if ((packet->length != 0u && packet->data == NULL) ||",
    "    spw_result_t result;\n    spw_terminator_t effective_terminator;\n\n    SPW_PROFILE_TX_BACKEND_ENTRY();\n    if ((packet->length != 0u && packet->data == NULL) ||",
)
replace_once(
    "src/backends/ethernet/udp_backend.c",
    "    (void)send_keepalive(backend, SPW_TIMEOUT_IMMEDIATE);\n    result = transmit_pending(backend, deadline_remaining(&deadline));\n    if (result != SPW_OK) {\n        clear_pending_tx(backend);\n        return result;\n    }\n\n    ++backend->statistics.tx_packets;",
    "    (void)send_keepalive(backend, SPW_TIMEOUT_IMMEDIATE);\n    SPW_PROFILE_TX_PROVIDER_ENTRY();\n    result = transmit_pending(backend, deadline_remaining(&deadline));\n    if (result != SPW_OK) {\n        clear_pending_tx(backend);\n        return result;\n    }\n    /* All fragments of this logical packet have been handed to UDP. */\n    SPW_PROFILE_TX_PROVIDER_BOUNDARY();\n\n    ++backend->statistics.tx_packets;",
)
replace_once(
    "src/backends/ethernet/udp_backend.c",
    "        backend->pending_packet_terminator = terminator;\n        backend->pending_packet_valid = true;\n        if (ack_required) {",
    "        backend->pending_packet_terminator = terminator;\n        backend->pending_packet_valid = true;\n        SPW_PROFILE_RX_PROVIDER_BOUNDARY();\n        if (ack_required) {",
)
replace_once(
    "src/backends/ethernet/udp_backend.c",
    "    backend->pending_packet_terminator =\n        (backend->reassembly.terminator_flags & SPW_VSPW_TP_FLAG_EEP) != 0u\n            ? SPW_TERMINATOR_EEP\n            : SPW_TERMINATOR_EOP;\n    backend->pending_packet_valid = true;\n    {",
    "    backend->pending_packet_terminator =\n        (backend->reassembly.terminator_flags & SPW_VSPW_TP_FLAG_EEP) != 0u\n            ? SPW_TERMINATOR_EEP\n            : SPW_TERMINATOR_EOP;\n    backend->pending_packet_valid = true;\n    SPW_PROFILE_RX_PROVIDER_BOUNDARY();\n    {",
)
replace_once(
    "src/backends/ethernet/udp_backend.c",
    "    backend->pending_packet_valid = false;\n    ++backend->statistics.rx_packets;\n    backend->statistics.rx_bytes += packet->length;\n    return SPW_OK;",
    "    backend->pending_packet_valid = false;\n    SPW_PROFILE_RX_PROVIDER_RETURN();\n    ++backend->statistics.rx_packets;\n    backend->statistics.rx_bytes += packet->length;\n    SPW_PROFILE_RX_BACKEND_RETURN();\n    return SPW_OK;",
)

# DEVICE/VSPD: final logical DATA_TX fragment is the AF_UNIX handoff; final DATA_RX
# fragment is the logical data-ready boundary.
replace_once(
    "src/backends/device/device_backend.c",
    '#include "backends/device/vspw_device_protocol.h"\n\n#include <spwkit/device.h>',
    '#include "backends/device/vspw_device_protocol.h"\n#include "profiling/profile.h"\n\n#include <spwkit/device.h>',
)
replace_once(
    "src/backends/device/device_backend.c",
    "            context->rx_active = false;\n            context->rx_ready = true;\n        }",
    "            context->rx_active = false;\n            context->rx_ready = true;\n            SPW_PROFILE_RX_PROVIDER_BOUNDARY();\n        }",
)
replace_once(
    "src/backends/device/device_backend.c",
    "    uint32_t offset = 0u;\n    spw_result_t result = ensure_connected(context, deadline);",
    "    uint32_t offset = 0u;\n    spw_result_t result;\n\n    SPW_PROFILE_TX_BACKEND_ENTRY();\n    result = ensure_connected(context, deadline);",
)
replace_once(
    "src/backends/device/device_backend.c",
    "    request_id = context->next_request_id++;\n    message_id = context->next_message_id++;\n\n    do {",
    "    request_id = context->next_request_id++;\n    message_id = context->next_message_id++;\n\n    SPW_PROFILE_TX_PROVIDER_ENTRY();\n    do {",
)
replace_once(
    "src/backends/device/device_backend.c",
    "        result = send_record(context, frame, VSPD_HEADER_SIZE + chunk, deadline);\n        if (result != SPW_OK) {\n            return result;\n        }\n        offset += chunk;\n        if (final_fragment) {",
    "        result = send_record(context, frame, VSPD_HEADER_SIZE + chunk, deadline);\n        if (result != SPW_OK) {\n            return result;\n        }\n        offset += chunk;\n        if (final_fragment) {\n            /* Complete logical DATA_TX request has reached AF_UNIX. */\n            SPW_PROFILE_TX_PROVIDER_BOUNDARY();",
)
replace_once(
    "src/backends/device/device_backend.c",
    "    clear_rx(context);\n    return SPW_OK;\n}\n\nstatic spw_result_t device_send_time_code",
    "    clear_rx(context);\n    SPW_PROFILE_RX_PROVIDER_RETURN();\n    SPW_PROFILE_RX_BACKEND_RETURN();\n    return SPW_OK;\n}\n\nstatic spw_result_t device_send_time_code",
)

# Permanent probe-smoke mode for the existing benchmark fixtures. This validates
# real paths while leaving normal benchmark output/semantics unchanged.
replace_once(
    "benchmarks/profile_host_backend.c",
    '            "[--warmup N] [--iterations N] [--payload N]\\n",',
    '            "[--warmup N] [--iterations N] [--payload N] [--probe-smoke]\\n",',
)
replace_once(
    "benchmarks/profile_host_backend.c",
    "    int have_direction = 0;\n    size_t warmup_iterations = 256u;",
    "    int have_direction = 0;\n    int probe_smoke = 0;\n    size_t warmup_iterations = 256u;",
)
replace_once(
    "benchmarks/profile_host_backend.c",
    "        } else {\n            print_usage(argv[0]);\n            return 2;\n        }\n    }\n\n    if (!have_backend || !have_direction) {",
    "        } else if (strcmp(argv[i], \"--probe-smoke\") == 0) {\n            probe_smoke = 1;\n        } else {\n            print_usage(argv[0]);\n            return 2;\n        }\n    }\n\n    if (!have_backend || !have_direction) {",
)
replace_once(
    "benchmarks/profile_host_backend.c",
    "    spw_profile_prepare();\n    for (i = 0u; i < warmup_iterations; ++i) {",
    "    spw_profile_prepare();\n    if (probe_smoke) {\n        uint64_t ignored = 0u;\n        const volatile spw_profile_sample_t* sample;\n        spw_profile_reset();\n        if (!run_sample(&fixture, direction, payload_size, &ignored)) {\n            fprintf(stderr, \"probe smoke operation failed\\n\");\n            return 1;\n        }\n        sample = spw_profile_last_sample();\n        if (sample == NULL || sample->sequence != 1u) {\n            fprintf(stderr, \"probe smoke expected sequence=1, got %u\\n\",\n                    sample == NULL ? 0u : sample->sequence);\n            return 1;\n        }\n        printf(\"PROBE_SMOKE PASS backend=%s direction=%s sequence=%u delta=%llu\\n\",\n               backend_name(backend), direction_name(direction), sample->sequence,\n               (unsigned long long)sample->delta);\n        return fixture_close(&fixture) ? 0 : 1;\n    }\n    for (i = 0u; i < warmup_iterations; ++i) {",
)

for path, prefix in [
    ("benchmarks/profile_udp_comparison.c", "UDP"),
    ("benchmarks/profile_device_comparison.c", "DEVICE"),
]:
    text = Path(path).read_text()
    if path.endswith("profile_udp_comparison.c"):
        text = text.replace(
            '            "Usage: %s --direction tx|rx [--warmup N] [--iterations N] [--payload N]\\n",',
            '            "Usage: %s --direction tx|rx [--warmup N] [--iterations N] [--payload N] [--probe-smoke]\\n",',
            1,
        )
        text = text.replace(
            "    int have_direction = 0;\n    size_t warmup_iterations = 64u;",
            "    int have_direction = 0;\n    int probe_smoke = 0;\n    size_t warmup_iterations = 64u;",
            1,
        )
    else:
        text = text.replace(
            '            "Usage: %s --socket PATH --direction tx|rx [--warmup N] [--iterations N] [--payload N]\\n",',
            '            "Usage: %s --socket PATH --direction tx|rx [--warmup N] [--iterations N] [--payload N] [--probe-smoke]\\n",',
            1,
        )
        text = text.replace(
            "    int have_direction = 0;\n    size_t warmup_iterations = 64u;",
            "    int have_direction = 0;\n    int probe_smoke = 0;\n    size_t warmup_iterations = 64u;",
            1,
        )
    parse_tail = "        } else {\n            usage(argv[0]);\n            return 2;\n        }\n    }"
    if text.count(parse_tail) != 1:
        raise SystemExit(f"{path}: parse-tail match count {text.count(parse_tail)}")
    text = text.replace(
        parse_tail,
        "        } else if (strcmp(argv[i], \"--probe-smoke\") == 0) {\n"
        "            probe_smoke = 1;\n"
        "        } else {\n"
        "            usage(argv[0]);\n"
        "            return 2;\n"
        "        }\n"
        "    }",
        1,
    )
    if path.endswith("profile_udp_comparison.c"):
        anchor = "    spw_profile_prepare();\n\n    for (i = 0u; i < warmup_iterations; ++i) {"
        smoke = "    spw_profile_prepare();\n    if (probe_smoke) {\n        uint64_t ignored = 0u;\n        const volatile spw_profile_sample_t* sample;\n        spw_profile_reset();\n        if (!(direction == UDP_DIRECTION_TX\n                  ? sample_spw_tx(&fixture, payload_size, &ignored)\n                  : sample_spw_rx(&fixture, payload_size, &ignored))) {\n            fprintf(stderr, \"UDP probe smoke operation failed\\n\");\n            fixture_close(&fixture);\n            return 1;\n        }\n        sample = spw_profile_last_sample();\n        if (sample == NULL || sample->sequence != 1u) {\n            fprintf(stderr, \"UDP probe smoke expected sequence=1, got %u\\n\",\n                    sample == NULL ? 0u : sample->sequence);\n            fixture_close(&fixture);\n            return 1;\n        }\n        printf(\"PROBE_SMOKE PASS backend=udp direction=%s sequence=%u delta=%llu\\n\",\n               direction == UDP_DIRECTION_TX ? \"tx\" : \"rx\", sample->sequence,\n               (unsigned long long)sample->delta);\n        fixture_close(&fixture);\n        return 0;\n    }\n\n    for (i = 0u; i < warmup_iterations; ++i) {"
    else:
        anchor = "    spw_profile_prepare();\n\n    for (i = 0u; i < warmup_iterations; ++i) {"
        smoke = "    spw_profile_prepare();\n    if (probe_smoke) {\n        uint64_t ignored = 0u;\n        const volatile spw_profile_sample_t* sample;\n        spw_profile_reset();\n        if (!(direction == DEVICE_DIRECTION_TX\n                  ? sample_spw_tx(&fixture, payload_size, &ignored)\n                  : sample_spw_rx(&fixture, payload_size, &ignored))) {\n            fprintf(stderr, \"DEVICE probe smoke operation failed\\n\");\n            fixture_close(&fixture);\n            return 1;\n        }\n        sample = spw_profile_last_sample();\n        if (sample == NULL || sample->sequence != 1u) {\n            fprintf(stderr, \"DEVICE probe smoke expected sequence=1, got %u\\n\",\n                    sample == NULL ? 0u : sample->sequence);\n            fixture_close(&fixture);\n            return 1;\n        }\n        printf(\"PROBE_SMOKE PASS backend=device direction=%s sequence=%u delta=%llu\\n\",\n               direction == DEVICE_DIRECTION_TX ? \"tx\" : \"rx\", sample->sequence,\n               (unsigned long long)sample->delta);\n        fixture_close(&fixture);\n        return 0;\n    }\n\n    for (i = 0u; i < warmup_iterations; ++i) {"
    if text.count(anchor) != 1:
        raise SystemExit(f"{path}: profile-prepare anchor count {text.count(anchor)}")
    Path(path).write_text(text.replace(anchor, smoke, 1))

# Add permanent real-path validation jobs to the profiling workflow.
workflow = Path(".github/workflows/profiling.yml")
text = workflow.read_text()
anchor = "  counter-metadata:\n    name: Counter metadata override\n"
if text.count(anchor) != 1:
    raise SystemExit("profiling workflow counter-metadata anchor missing")
job = r'''  hosted-backend-boundaries:
    name: Backend boundaries / ${{ matrix.backend }} / ${{ matrix.direction }}
    runs-on: ubuntu-24.04
    strategy:
      fail-fast: false
      matrix:
        include:
          - backend: loopback
            direction: tx
            start: SPW_PROFILE_ID_TX_API_ENTRY
            end: SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
          - backend: loopback
            direction: rx
            start: SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
            end: SPW_PROFILE_ID_RX_API_RETURN
          - backend: simulator
            direction: tx
            start: SPW_PROFILE_ID_TX_API_ENTRY
            end: SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
          - backend: simulator
            direction: rx
            start: SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
            end: SPW_PROFILE_ID_RX_API_RETURN
          - backend: udp
            direction: tx
            start: SPW_PROFILE_ID_TX_API_ENTRY
            end: SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
          - backend: udp
            direction: rx
            start: SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
            end: SPW_PROFILE_ID_RX_API_RETURN
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Configure selected backend boundary
        shell: bash
        run: |
          set -euo pipefail
          cmake -S benchmarks -B build-boundary \
            -DCMAKE_BUILD_TYPE=Release \
            -DSPWKIT_BENCHMARK_PROFILE_START=${{ matrix.start }} \
            -DSPWKIT_BENCHMARK_PROFILE_END=${{ matrix.end }} \
            -DSPWKIT_BENCHMARK_BUILD_SIMULATOR=ON \
            -DSPWKIT_BENCHMARK_BUILD_UDP=ON

      - name: Exercise real backend path
        shell: bash
        run: |
          set -euo pipefail
          if [[ "${{ matrix.backend }}" == udp ]]; then
            cmake --build build-boundary --target spwkit_profile_udp_comparison --parallel 2
            # 4096 B deliberately exercises fragmentation while requiring one
            # logical provider-boundary sample for the API operation.
            timeout 30s build-boundary/spwkit_profile_udp_comparison \
              --direction "${{ matrix.direction }}" --payload 4096 --probe-smoke
          else
            cmake --build build-boundary --target spwkit_profile_host_backend --parallel 2
            build-boundary/spwkit_profile_host_backend \
              --backend "${{ matrix.backend }}" \
              --direction "${{ matrix.direction }}" \
              --payload 64 --probe-smoke
          fi

  device-backend-boundaries:
    name: Backend boundaries / device / ${{ matrix.direction }}
    runs-on: ubuntu-24.04
    strategy:
      fail-fast: false
      matrix:
        include:
          - direction: tx
            start: SPW_PROFILE_ID_TX_API_ENTRY
            end: SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
          - direction: rx
            start: SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY
            end: SPW_PROFILE_ID_RX_API_RETURN
    steps:
      - name: Checkout
        uses: actions/checkout@v6

      - name: Configure DEVICE boundary
        shell: bash
        run: |
          set -euo pipefail
          cmake -S benchmarks -B build-device-boundary \
            -DCMAKE_BUILD_TYPE=Release \
            -DSPWKIT_BENCHMARK_PROFILE_START=${{ matrix.start }} \
            -DSPWKIT_BENCHMARK_PROFILE_END=${{ matrix.end }} \
            -DSPWKIT_BENCHMARK_BUILD_DEVICE=ON
          cmake --build build-device-boundary \
            --target vspwd spwkit_profile_device_comparison --parallel 2

      - name: Exercise real DEVICE/VSPD path
        shell: bash
        run: |
          set -euo pipefail
          tmpdir="$(mktemp -d)"
          socket="$tmpdir/vspwd.sock"
          log="$tmpdir/vspwd.log"
          daemon="build-device-boundary/spwkit-runtime/vspwd"
          cleanup() {
            set +e
            [[ -n "${pid:-}" ]] && kill -TERM "$pid" 2>/dev/null || true
            [[ -n "${pid:-}" ]] && wait "$pid" 2>/dev/null || true
            rm -rf "$tmpdir"
          }
          trap cleanup EXIT
          "$daemon" --socket "$socket" >"$log" 2>&1 &
          pid=$!
          for _ in $(seq 1 100); do
            [[ -S "$socket" ]] && break
            kill -0 "$pid" 2>/dev/null || { cat "$log" >&2; exit 1; }
            sleep 0.02
          done
          [[ -S "$socket" ]] || { cat "$log" >&2; exit 1; }
          # 4096 B validates that VSPD fragmentation still produces one logical
          # boundary sample rather than one sample per AF_UNIX record.
          timeout 30s build-device-boundary/spwkit_profile_device_comparison \
            --socket "$socket" --direction "${{ matrix.direction }}" \
            --payload 4096 --probe-smoke

'''
workflow.write_text(text.replace(anchor, job + anchor, 1))

print("#136 backend boundary patch applied")
