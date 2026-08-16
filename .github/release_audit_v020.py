#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one occurrence, found {count}: {old!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


# Public release/API version alignment.
replace_once(
    "include/spwkit/api.h",
    "#define SPWKIT_API_VERSION_MINOR 1u",
    "#define SPWKIT_API_VERSION_MINOR 2u",
)

replace_once(
    "tests/core_types.c",
    "    assert(SPW_OK == 0);\n",
    "    assert(SPWKIT_API_VERSION_MAJOR == 0u);\n"
    "    assert(SPWKIT_API_VERSION_MINOR == 2u);\n"
    "    assert(SPWKIT_API_VERSION_PATCH == 0u);\n\n"
    "    assert(SPW_OK == 0);\n",
)

# Root release status.
replace_once(
    "README.md",
    "### Current `main`: v0.2 development\n\nDevelopment has moved to package version 0.2.0 and the distributed virtual SpaceWire backend now includes:",
    "### v0.2.0 — Distributed virtual SpaceWire\n\n`v0.2.0` is the distributed virtual SpaceWire release and includes:",
)
replace_once(
    "README.md",
    "The planned v0.2 engineering scope is complete once this platform-policy slice is merged and validated. Native Winsock UDP transport is intentionally deferred; it is not required to close the v0.2 distributed milestone.",
    "The v0.2 engineering scope is complete. Native Winsock UDP transport is intentionally deferred beyond this release and tracked separately in issue #42.",
)
replace_once(
    "README.md",
    "Consumer CMake for current v0.2 development:",
    "Consumer CMake for v0.2.0:",
)

# Roadmap release-state cleanup.
replace_once(
    "docs/roadmap.md",
    "SpWKit has completed its v0.1 portable-core milestone and the planned v0.2 distributed virtual SpaceWire engineering scope is now complete pending final validation/release tagging. The ordering below continues to stabilize software semantics before hardware-specific details are allowed to dictate the application API.",
    "SpWKit has completed the v0.1 portable-core milestone and the v0.2 distributed virtual SpaceWire milestone. The ordering below continues to stabilize software semantics before hardware-specific details are allowed to dictate the application API.",
)
replace_once(
    "docs/roadmap.md",
    "## v0.2.0 — Distributed virtual SpaceWire — implementation complete\n\nDelivered in v0.2 development:",
    "## v0.2.0 — Distributed virtual SpaceWire — complete\n\nDelivered in v0.2.0:",
)
replace_once(
    "docs/roadmap.md",
    "Release completion after this slice consists of final repository/status review and the v0.2.0 release/tag decision rather than additional planned runtime features.",
    "`v0.2.0` is the released distributed virtual SpaceWire milestone. Post-v0.2 portability work, including native Winsock UDP support, remains outside this release boundary.",
)

# Configuration release wording.
replace_once(
    "docs/configuration.md",
    "Current v0.2 development uses `SPW_BACKEND_UDP` with `spw_udp_config_t`:",
    "v0.2.0 uses `SPW_BACKEND_UDP` with `spw_udp_config_t`:",
)

# Public API document: current v0.2 release, not a half-finished development snapshot.
replace_once(
    "docs/api.md",
    "This document defines the v0.1 application-facing contract and notes the v0.2 distributed backend now present on current `main`.",
    "This document defines the v0.2.0 application-facing contract while preserving the v0.1 portable-core baseline.",
)
replace_once(
    "docs/api.md",
    "For v0.1, the process-local simulator is the primary runtime reference backend.",
    "The process-local simulator introduced in v0.1 remains the primary local runtime reference backend.",
)
replace_once(
    "docs/api.md",
    "Current `main` contains the first v0.2 distributed backend:",
    "v0.2.0 adds the distributed VSPW-TP/UDP backend:",
)
replace_once(
    "docs/api.md",
    "The initial backend implements:",
    "The v0.2.0 backend implements:",
)
replace_once(
    "docs/api.md",
    "Broader shared-contract coverage, stronger multi-process/network-namespace examples, capture tooling and final platform-scope decisions remain v0.2 work.",
    "The UDP backend runs the reusable shared public contract, process and Linux network-namespace integration, deterministic timing/fault scenarios, and VSPW-TP capture/Wireshark validation. The hosted runtime is supported on POSIX hosts according to `docs/platform-support.md`; native Winsock transport is deferred beyond v0.2.0.",
)
replace_once(
    "docs/api.md",
    "For v0.1, the shared suite runs through `libspwkit` against loopback and the local simulator backend.\n\nThe v0.2 UDP backend currently adds codec and end-to-end D2D integration coverage. As distributed semantics mature, reusable contract coverage should be expanded so the same behavioural assertions execute across local, distributed, embedded, `/dev/spwX`, and future HIL backends where capability profiles permit.",
    "The shared suite runs through `libspwkit` against loopback, the local simulator and the v0.2 UDP backend. Distributed-specific extensions cover peer loss/restart, while D2D tests cover framing, fragmentation/reordering, reliability, timing, deterministic faults and process/network isolation. Future embedded, `/dev/spwX`, and HIL backends should reuse the same capability-driven contract where applicable.",
)

# Internal backend contract: reflect completed v0.2 transport semantics.
replace_once(
    "docs/backend-contract.md",
    "Current `main` contains the initial v0.2 VSPW-TP/UDP backend. It implements the same internal contract while translating copied packet/time-code operations into versioned VSPW-TP datagrams.",
    "v0.2.0 includes the VSPW-TP/UDP backend. It implements the same internal contract while translating copied packet/time-code operations into versioned VSPW-TP datagrams.",
)
replace_once(
    "docs/backend-contract.md",
    "ACK/retransmission, peer keepalive/disconnect detection, explicit loss/reordering handling, latency/rate and deterministic fault injection remain v0.2 work.",
    "The v0.2.0 backend additionally provides logical-message ACK/retransmission, duplicate suppression, session/KEEPALIVE peer liveness and restart recovery, bounded arbitrary-order fragment reassembly, deterministic virtual rate/latency, deterministic transport fault injection, explicit SpaceWire-side EEP injection, and separate fault-domain diagnostics.",
)
replace_once(
    "docs/backend-contract.md",
    "The shared contract suite exercises loopback and the local simulator. The distributed UDP backend additionally has transport codec and end-to-end D2D integration coverage and should continue adopting reusable contract cases as v0.2 semantics mature.",
    "The shared contract suite exercises loopback, the local simulator and the distributed UDP backend. Distributed extensions verify peer loss/restart through public APIs, while codec/D2D tests verify fragmentation, arbitrary ordering, retry/deduplication, timing, deterministic faults, installed-package process isolation and Linux network-namespace operation.",
)

# Getting-started guide: release package and current distributed behavior.
replace_once(
    "docs/getting-started.md",
    "The released v0.1 baseline contains loopback and the process-local simulator. Current `main` has moved into v0.2 development and additionally contains the first VSPW-TP/UDP distributed backend.",
    "The v0.2.0 release contains the v0.1 portable-core baseline plus the VSPW-TP/UDP distributed backend, reliability/liveness, deterministic timing and fault injection, distributed contract coverage, process/network isolation examples, and capture tooling.",
)
replace_once(
    "docs/getting-started.md",
    "find_package(SpWKit 0.1 CONFIG REQUIRED)",
    "find_package(SpWKit 0.2 CONFIG REQUIRED)",
)
replace_once(
    "docs/getting-started.md",
    "## Distributed UDP peers on current main\n\nThe v0.2 development backend uses the same public port API. Only backend configuration changes:",
    "## Distributed UDP peers in v0.2.0\n\nThe v0.2.0 backend uses the same public port API. Only backend configuration changes:",
)
replace_once(
    "docs/getting-started.md",
    "ACK/retransmission, peer keepalive/disconnect detection, configurable virtual latency/rate and deterministic fault injection remain v0.2 work.",
    "The v0.2.0 backend includes logical-message ACK/retransmission, duplicate suppression, peer session/keepalive/disconnect detection and restart recovery, configurable virtual latency/rate, deterministic transport fault injection, and explicit SpaceWire-side EEP injection. Linux is the primary fully exercised distributed host and macOS is a supported POSIX host; Windows retains the public API/package but returns `SPW_ERR_UNSUPPORTED` for the UDP runtime in v0.2.0.",
)
replace_once(
    "docs/getting-started.md",
    "- `examples/installed`: standalone `find_package(SpWKit)` consumer used by CI.\n\nThe repository's device-to-device test provides the current executable UDP example/verification path until a dedicated user-facing distributed example is added during v0.2.",
    "- `examples/installed`: standalone `find_package(SpWKit)` consumer used by CI;\n- `examples/distributed`: installed-package equal-peer UDP application used for two-process and Linux network-namespace scenarios.\n\nThe D2D workflow builds the distributed example against the installed package, exercises full-duplex >MTU packets and time codes, then verifies peer loss and new-session restart recovery.",
)

# Type documentation now includes the public v0.2 additions.
replace_once(
    "docs/types.md",
    "This document defines the v0.1 software-visible value types used by `libspwkit`.",
    "This document defines the v0.2.0 software-visible value types used by `libspwkit`, including the v0.1 portable-core baseline and additive v0.2 fault diagnostics.",
)
replace_once(
    "docs/types.md",
    "The v0.1 common results are:",
    "The common result set is:",
)
replace_once(
    "docs/types.md",
    "The counters are diagnostic and verification-facing. Additional hardware-specific counters belong in backend extension APIs rather than the portable structure.",
    "The counters are diagnostic and verification-facing. Additional hardware-specific counters belong in backend extension APIs rather than the portable structure.\n\nv0.2.0 additionally defines `spw_fault_statistics_t` for backends advertising `SPW_CAP_FAULT_INJECTION`. It separates VSPW-TP transport drops, duplicates, reorders and delays from explicit SpaceWire-visible EEP injections so carrier faults are not misreported as SpaceWire errors.",
)

# Backend directory status.
replace_once(
    "src/backends/ethernet/README.md",
    "Remaining v0.2 work includes ACK/retransmission, peer keepalive/disconnect detection, loss/reordering policy, configurable virtual rate/latency and deterministic fault injection.",
    "v0.2.0 also includes logical-message ACK/retransmission, duplicate suppression, session/KEEPALIVE liveness and restart recovery, bounded arbitrary-order reassembly, configurable virtual rate/latency, deterministic transport faults, explicit SpaceWire EEP injection, shared contract coverage, process/network-namespace integration and capture tooling.",
)

# Architecture/release wording.
replace_once(
    "docs/architecture.md",
    "Current `main` also contains the first v0.2 distributed backend:",
    "v0.2.0 includes the distributed VSPW-TP/UDP backend:",
)

# Protocol wording at the release boundary.
replace_once(
    "docs/vspw-tp.md",
    "The earlier 32-byte VSPW-TP draft existed only on unreleased v0.2 development history. The session-aware 40-byte format is the v1 format intended for the v0.2 release.",
    "The earlier 32-byte VSPW-TP draft existed only on unreleased v0.2 development history. The session-aware 40-byte format is the VSPW-TP v1 format used by the v0.2.0 release.",
)

# Release audit guards: these phrases indicate a stale pre-release snapshot.
stale = [
    "Current `main`: v0.2 development",
    "Current v0.2 development",
    "pending final validation/release tagging",
    "remain v0.2 work",
    "remains v0.2 work",
    "intended for the v0.2 release",
]
for path in [
    "README.md",
    "docs/api.md",
    "docs/architecture.md",
    "docs/backend-contract.md",
    "docs/configuration.md",
    "docs/getting-started.md",
    "docs/roadmap.md",
    "docs/types.md",
    "docs/vspw-tp.md",
    "src/backends/ethernet/README.md",
]:
    text = Path(path).read_text(encoding="utf-8")
    for phrase in stale:
        if phrase in text:
            raise SystemExit(f"{path}: stale release phrase remains: {phrase}")

print("V020_RELEASE_AUDIT_PATCH_PASS")
