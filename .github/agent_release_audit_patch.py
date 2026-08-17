from pathlib import Path
import re


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing marker in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_all(path, old, new):
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"missing marker in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new))


def replace_regex(path, pattern, replacement):
    p = Path(path)
    text = p.read_text()
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"expected one regex match in {path}, got {count}: {pattern!r}")
    p.write_text(updated)


# ---------------------------------------------------------------------------
# Installed/public release surface.
# ---------------------------------------------------------------------------
replace_once(
    "CMakeLists.txt",
    "if(SPWKIT_ENABLE_CPP)\n    install(\n        FILES include/spwkit/spwkit.hpp\n        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spwkit\n    )\nendif()\n\nconfigure_package_config_file(",
    "if(SPWKIT_ENABLE_CPP)\n    install(\n        FILES include/spwkit/spwkit.hpp\n        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spwkit\n    )\nendif()\n\n# Preserve Apache-2.0 distribution metadata in installed/package artifacts.\ninstall(\n    FILES LICENSE NOTICE\n    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/spwkit\n)\n\nconfigure_package_config_file(")

replace_once(
    "include/spwkit/spwkit.h",
    '#include "spwkit/simulator.h"\n#include "spwkit/device.h"',
    '#include "spwkit/simulator.h"\n#include "spwkit/udp.h"\n#include "spwkit/device.h"')

Path("tests/api_c_compile.c").write_text(r'''// SPDX-License-Identifier: Apache-2.0
#include "spwkit/spwkit.h"

static spw_result_t (*open_fn)(const spw_port_config_t*, spw_port_t**) = spw_port_open;
static spw_result_t (*close_fn)(spw_port_t*) = spw_port_close;
static spw_result_t (*start_fn)(spw_port_t*) = spw_port_start;
static spw_result_t (*wait_fn)(spw_port_t*, spw_ready_events_t,
                               spw_timeout_us_t, spw_ready_events_t*) = spw_port_wait;
static spw_result_t (*send_fn)(spw_port_t*, const spw_packet_t*, spw_timeout_us_t) = spw_port_send;
static spw_result_t (*receive_fn)(spw_port_t*, spw_packet_t*, spw_timeout_us_t) = spw_port_receive;
static spw_result_t (*send_tc_fn)(spw_port_t*, const spw_time_code_t*, spw_timeout_us_t) =
    spw_port_send_time_code;
static spw_result_t (*receive_tc_fn)(spw_port_t*, spw_time_code_t*, spw_timeout_us_t) =
    spw_port_receive_time_code;
static spw_result_t (*stats_fn)(spw_port_t*, spw_statistics_t*) = spw_port_get_statistics;
static spw_result_t (*workspace_fn)(const spw_port_config_t*,
                                    spw_port_workspace_requirements_t*) =
    spw_port_workspace_requirements;

int spwkit_api_c_compile_probe(void) {
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(42000u, 42001u, 42u);
    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(0u);

    return (open_fn != 0 && close_fn != 0 && start_fn != 0 && wait_fn != 0 &&
            send_fn != 0 && receive_fn != 0 && send_tc_fn != 0 &&
            receive_tc_fn != 0 && stats_fn != 0 && workspace_fn != 0 &&
            simulator.struct_size != 0u && udp.struct_size != 0u &&
            device.struct_size != 0u)
               ? 0
               : 1;
}
''')

replace_once(
    "tests/api_cpp_compile.cpp",
    "static_assert(std::is_same_v<decltype(&spw_port_send),\n                             spw_result_t (*)(spw_port_t*, const spw_packet_t*, spw_timeout_us_t)>);",
    "static_assert(std::is_same_v<decltype(&spw_port_send),\n                             spw_result_t (*)(spw_port_t*, const spw_packet_t*, spw_timeout_us_t)>);\nstatic_assert(std::is_same_v<decltype(&spw_port_wait),\n                             spw_result_t (*)(spw_port_t*, spw_ready_events_t,\n                                             spw_timeout_us_t, spw_ready_events_t*)>);\nstatic_assert(SPW_UDP_CONFIG_VERSION == 1u);\nstatic_assert(SPW_DEVICE_CONFIG_VERSION == 1u);\nstatic_assert(SPW_SIMULATOR_CONFIG_VERSION == 1u);")

# ---------------------------------------------------------------------------
# README / release notes.
# ---------------------------------------------------------------------------
replace_regex(
    "README.md",
    r"### v0\.4\.0 — Linux virtual device — development\n.*?\n## Virtual SpaceWire",
    '''### v0.4.0 — Linux virtual device and userspace service — release candidate

The v0.4 implementation is feature-complete and is undergoing its final release audit before the tag is created. It adds a Linux virtual-device/service layer without changing the application-facing `spw_port_*` model.

The release includes:

- private VSPD v1.3 over Linux `AF_UNIX`/`SOCK_SEQPACKET` with fixed network-order encodings and bounded 1 MiB logical packets;
- pure-C `vspwd` with deterministic two-port lifecycle, packet, EOP/EEP, zero-length packet, time-code, statistics and peer restart behavior;
- public `SPW_BACKEND_DEVICE` plus backend-neutral level-triggered `spw_port_wait()` receive readiness;
- HELLO-only non-owning management through `spwctl` and passive coalesced observation through `spwmon`;
- standalone installed-package C11 and optional C++17 device consumers, including mixed C/C++ peer interoperability;
- a CUSE/libfuse3 feasibility result and private packet-record prototype while the production `/dev/vspwX` presenter remains separately tracked in #78;
- an optional topology-owned `vspwd` port bridged through the existing VSPW-TP/UDP backend, including remote loss/restart recovery;
- dedicated pure-C GCC/Clang device, tools, bridge and installed-consumer CI with `CXX=/bin/false`.

Physical FPGA/HIL verification, the production CUSE presenter and native Winsock VSPW-TP remain explicitly outside the v0.4 release boundary.

## Virtual SpaceWire''')

replace_all(
    "README.md",
    "Consumer CMake for the v0.4 development line:",
    "Consumer CMake for v0.4:")

replace_once(
    "README.md",
    "- `examples/distributed`: standalone **C-only** installed-package VSPW-TP/UDP equal peer for two processes or Linux hosts, including restart scenarios.",
    "- `examples/distributed`: standalone **C-only** installed-package VSPW-TP/UDP equal peer for two processes or Linux hosts, including restart scenarios;\n- `examples/installed_device`: standalone C11 installed-package Linux device consumer;\n- `examples/installed_device_cpp`: standalone optional C++17 wrapper device consumer.")

replace_once(
    "README.md",
    "- [`vspwd` userspace virtual-device service](docs/vspwd.md)\n- [VSPW-TP capture and Wireshark tooling](tools/wireshark/README.md)",
    "- [`vspwd` userspace virtual-device service](docs/vspwd.md)\n- [`spwmon` passive daemon observation](docs/spwmon.md)\n- [Installed-package Linux device examples](docs/installed-device-examples.md)\n- [CUSE `/dev/vspwX` feasibility](docs/cuse-feasibility.md)\n- [VSPW-TP capture and Wireshark tooling](tools/wireshark/README.md)")

# Replace the incomplete v0.4 changelog development stub with the actual release boundary.
replace_regex(
    "CHANGELOG.md",
    r"## v0\.4\.0 — unreleased\n.*?\n## v0\.3\.0",
    '''## v0.4.0 — unreleased

Linux virtual-device and userspace-service release candidate. The public C API remains authoritative; VSPD, Unix sockets, CUSE and daemon-management protocol details remain private implementation layers.

### Added

- private VSPD v1.3 backend↔daemon protocol with a fixed 40-byte network-order header, bounded 32 KiB records and 1 MiB logical DATA fragmentation/reassembly;
- Linux `SPW_BACKEND_DEVICE` selected through the normal `spw_port_*` API, including reconnect/reattach after daemon loss;
- pure-C `vspwd` userspace service with two deterministic virtual ports, lifecycle, packets, EOP/EEP, zero-length packets, time codes, statistics and restart recovery;
- backend-neutral `SPW_CAP_READINESS`, `SPW_READY_RX_PACKET`, `SPW_READY_RX_TIME_CODE` and non-consuming level-triggered `spw_port_wait()`;
- VSPD 1.1 HELLO-only non-owning management and installed pure-C `spwctl` (`list`, `show`, `stats`, `clear-stats`);
- VSPD 1.2 bounded passive subscriptions and installed pure-C `spwmon` with human and JSON Lines output;
- standalone installed-package C11 and optional C++17 Linux device consumers, including mixed C/C++ peer validation;
- CUSE/libfuse3 feasibility work with a private fixed-width packet-record prototype; the production presenter remains tracked separately in #78;
- VSPD 1.3 bridged-port metadata and an optional topology-owned `vspwd` endpoint backed by the existing VSPW-TP/UDP runtime;
- end-to-end device↔daemon↔VSPW-TP/UDP DATA/time-code exchange with remote peer loss and fresh-process restart recovery;
- release packaging of Apache-2.0 `LICENSE` and `NOTICE` metadata.

### Changed

- completed the C-first v0.3 architecture by keeping `vspwd`, the Linux device backend and all daemon tools pure C with no mandatory C++ runtime;
- extended the shared backend contract to the Linux device backend and documented distributed/service-specific queue and peer-loss timing semantics;
- kept `/dev/vspwX` CUSE presentation optional and outside `libspwkit`; no kernel module or libfuse dependency is introduced into ordinary builds;
- kept bridge transport reliability in the existing `SPW_BACKEND_UDP` implementation rather than creating a second VSPW-TP stack inside `vspwd`.

### Verification

- Linux device, daemon, management, monitoring and bridge profiles run under GCC and Clang with `CXX=/bin/false`;
- public device and daemon paths run under ASan+UBSan;
- standalone installed C and C++ device consumers exercise C↔C, C++↔C++, C↔C++ and C++↔C interoperability;
- the existing cross-platform package matrix, pure-C static/shared gates, simulator contract, VSPW-TP D2D/network-namespace tests, freestanding portability checks and Wireshark/tshark validation remain release gates;
- CUSE feasibility is compile/API validated under GCC and Clang without claiming `/dev/cuse` runtime evidence when hosted runners do not expose it.

### Deferred beyond v0.4

- production event-driven CUSE `/dev/vspwX` presenter (#78);
- native Windows/Winsock VSPW-TP runtime (#42);
- physical FPGA/HIL backend and electrical interoperability evidence;
- generic SpaceWire routing/topology management and router simulation.

No `v0.4.0` tag is implied until the release audit is complete.

## v0.3.0''')

# ---------------------------------------------------------------------------
# User/integration documentation.
# ---------------------------------------------------------------------------
replace_once(
    "docs/getting-started.md",
    "The current v0.3 engineering line contains the v0.1 portable core, v0.2 VSPW-TP/UDP distributed backend, and the C11 runtime conversion. The next milestone is the Linux virtual-device/userspace-service layer tracked by #54.",
    "The v0.4 release candidate contains the v0.1 portable core, v0.2 VSPW-TP/UDP distributed backend, v0.3 C11 runtime conversion, and the Linux virtual-device/userspace-service layer tracked by #54. Production CUSE `/dev/vspwX`, native Winsock UDP and physical HIL remain separately tracked beyond this release boundary.")
replace_all("docs/getting-started.md", "find_package(SpWKit 0.3 CONFIG REQUIRED)", "find_package(SpWKit 0.4 CONFIG REQUIRED)")
replace_once(
    "docs/getting-started.md",
    "`examples/distributed` is deliberately a **C-only installed-package consumer**. The D2D workflow builds it with `CXX=/bin/false`, then runs independent-process and Linux network-namespace restart scenarios.\n\n## Errors and timeouts",
    '''`examples/distributed` is deliberately a **C-only installed-package consumer**. The D2D workflow builds it with `CXX=/bin/false`, then runs independent-process and Linux network-namespace restart scenarios.

## Linux virtual-device service

Build the public Linux device backend and daemon without enabling C++:

```sh
CC=gcc CXX=/bin/false cmake -S . -B build-device \\
  -DSPWKIT_BUILD_CPP_TESTS=OFF \\
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \\
  -DSPWKIT_BUILD_DEVICE=ON \\
  -DSPWKIT_BUILD_VSPWD=ON
cmake --build build-device --parallel
```

Run `vspwd`, then open one daemon port through the same public API:

```c
spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(0u);
spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
config.backend_config = &device;
config.backend_config_size = sizeof(device);
```

`spw_port_wait()` provides non-consuming packet/time-code readiness when the device backend advertises `SPW_CAP_READINESS`. `spwctl` and `spwmon` are optional installed daemon tools when `SPWKIT_BUILD_TOOLS=ON`; they never become an alternate application data API.

Standalone installed-package device consumers live under `examples/installed_device` and `examples/installed_device_cpp`.

## Errors and timeouts''')
replace_once(
    "docs/getting-started.md",
    "- `examples/distributed`: C-only installed VSPW-TP/UDP equal-peer process used by D2D CI.",
    "- `examples/distributed`: C-only installed VSPW-TP/UDP equal-peer process used by D2D CI;\n- `examples/c_device_peer.c` / `examples/cpp_device_peer.cpp`: in-tree Linux device peers;\n- `examples/installed_device` / `examples/installed_device_cpp`: standalone installed-package Linux device consumers.")

replace_all("docs/language-bindings.md", "find_package(SpWKit 0.3 CONFIG REQUIRED)", "find_package(SpWKit 0.4 CONFIG REQUIRED)")

replace_regex(
    "docs/platform-support.md",
    r"## Current hosted support matrix\n.*?\n## C and C\+\+ language support",
    '''## Current hosted support matrix

| Host | C11 core / loopback | Local simulator | VSPW-TP/UDP runtime | Linux device / `vspwd` | Current validation level |
|---|---|---|---|---|---|
| Linux | supported | supported | **supported** | **supported** | full GCC/Clang host CI, pure-C static/shared consumers, simulator, D2D, VSPD/device/service/tools/bridge integration |
| macOS | supported | supported | **supported** | not implemented | host CI and UDP shared contract |
| Windows | supported | supported | **not implemented yet** | not implemented | MSVC build/test/install consumers; unsupported backend selection is verified |
| other CMake `UNIX` hosts | build path enabled | build path enabled where applicable | best effort / not release-validated | not release-validated | no release-support claim without dedicated evidence |

Linux is the primary distributed and virtual-device host. macOS is a supported second POSIX UDP host. Native Windows/Winsock UDP remains #42; the production Linux CUSE presenter remains #78. Neither is required for the v0.4 package boundary.

## C and C++ language support''')
replace_all("docs/platform-support.md", "find_package(SpWKit 0.3 CONFIG REQUIRED)", "find_package(SpWKit 0.4 CONFIG REQUIRED)")
replace_once(
    "docs/platform-support.md",
    "SpWKit_SIMULATOR_RUNTIME_SUPPORTED\nSpWKit_CPP_WRAPPER_AVAILABLE",
    "SpWKit_SIMULATOR_RUNTIME_SUPPORTED\nSpWKit_DEVICE_RUNTIME_SUPPORTED\nSpWKit_DEVICE_RUNTIME_SCOPE\nSpWKit_CPP_WRAPPER_AVAILABLE")
replace_regex(
    "docs/platform-support.md",
    r"## Linux virtual-device direction\n.*?\n## Why Winsock remains separate",
    '''## Linux virtual-device support

v0.4 adds the Linux `SPW_BACKEND_DEVICE` runtime and pure-C `vspwd` service beneath the same public `spw_port_*` API. The installed package reports `SpWKit_DEVICE_RUNTIME_SUPPORTED` and `SpWKit_DEVICE_RUNTIME_SCOPE=Linux` so applications can gate hosted examples without exposing Unix socket types.

The unprivileged reference path uses private VSPD over `AF_UNIX`/`SOCK_SEQPACKET`. `spwctl` and `spwmon` are optional installed service tools. `vspwd` can also reserve one of its two reference ports as a VSPW-TP/UDP bridge endpoint while the opposite port remains a normal public device endpoint.

CUSE feasibility has been validated separately with libfuse3 and a private packet-record prototype. The production event-driven `/dev/vspwX` presenter remains #78 and is not claimed as part of v0.4.

## Why Winsock remains separate''')
replace_once(
    "docs/platform-support.md",
    "- Linux is the primary fully exercised distributed and upcoming virtual-device platform;",
    "- Linux is the primary fully exercised distributed and virtual-device/service platform;")

replace_once(
    "docs/roadmap.md",
    "SpWKit has completed the v0.1 portable-core, v0.2 distributed virtual SpaceWire, and v0.3 C-first runtime releases. The v0.4 line builds Linux virtual-device/service integration on that C substrate.",
    "SpWKit has completed the v0.1 portable-core, v0.2 distributed virtual SpaceWire, and v0.3 C-first runtime releases. The v0.4 feature set is complete and in release audit for the Linux virtual-device/service boundary.")
replace_regex(
    "docs/roadmap.md",
    r"## v0\.4\.0 — Linux virtual device and userspace service — active\n.*?\n## v0\.5\.0",
    '''## v0.4.0 — Linux virtual device and userspace service — release audit

Tracked by #54. Functional implementation is complete; #81 is the final release-hardening/audit gate before tagging.

Delivered:

- private VSPD v1.3 with fixed-width network-order framing and bounded 1 MiB logical packets;
- pure-C `vspwd` two-port userspace service over Linux `AF_UNIX`/`SOCK_SEQPACKET`;
- public Linux `SPW_BACKEND_DEVICE` through the normal `spw_port_*` API;
- full packet/EOP/EEP/zero-length/time-code/link/statistics/restart behavior;
- backend-neutral level-triggered `spw_port_wait()` receive readiness;
- shared backend-contract coverage for the device path;
- non-owning `spwctl` management and passive `spwmon` observation;
- standalone installed-package C and optional C++ device consumers with mixed-language peer tests;
- CUSE/libfuse3 feasibility and a private packet-record prototype, with production presenter deferred to #78;
- topology-owned `vspwd` VSPW-TP/UDP bridge with remote loss/restart recovery;
- dedicated pure-C GCC/Clang device/service/tools/bridge CI, sanitizers and installed-consumer gates.

Explicitly deferred beyond v0.4: production CUSE presenter (#78), native Winsock UDP (#42), physical FPGA/HIL, and generic router/topology simulation.

## v0.5.0''')

replace_once(
    "docs/architecture.md",
    "The remainder of v0.4 is the OS-visible `/dev/vspwX` presentation and management/tooling layer. Embedded/RTOS adapters and physical FPGA/vendor implementations follow on the roadmap.",
    "The v0.4 virtual-device/service feature set is complete: the device backend, `vspwd`, readiness, management/monitoring, installed consumers and VSPW-TP/UDP bridge are implemented. A production `/dev/vspwX` CUSE presenter remains an optional post-v0.4 layer tracked by #78. Embedded/RTOS adapters and physical FPGA/vendor implementations follow on the roadmap.")
replace_once(
    "docs/architecture.md",
    "The current userspace socket is both the implementation transport and unprivileged CI boundary. CUSE remains a candidate for presenting `/dev/vspwX` without committing immediately to a kernel module. That future presentation must remain below `spw_port_*`; `/dev/vspwX` is not a second application API.",
    "The current userspace socket is both the implementation transport and unprivileged CI boundary. CUSE/libfuse3 feasibility has been validated with a packet-record prototype; the production event-driven presenter is tracked in #78. Any future `/dev/vspwX` presentation remains below `spw_port_*` and does not become a second application API.")
replace_once(
    "docs/architecture.md",
    "The future physical `/dev/spwX` path should reuse the same application-facing contract while replacing the implementation beneath it.",
    '''`vspwd` may also reserve one of the two reference ports as a topology-owned VSPW-TP/UDP bridge endpoint:

```text
local application -> SPW_BACKEND_DEVICE -> VSPD -> vspwd
                                                local port <-> bridged port
                                                                 |
                                                            SPW_BACKEND_UDP
                                                                 |
                                                            remote peer
```

The bridge reuses the existing UDP backend and keeps VSPW-TP reliability/transport logic out of the daemon itself.

The future physical `/dev/spwX` path should reuse the same application-facing contract while replacing the implementation beneath it.''')

# ---------------------------------------------------------------------------
# Verification documentation.
# ---------------------------------------------------------------------------
replace_all("docs/testing.md", "find_package(SpWKit 0.3 CONFIG REQUIRED)", "find_package(SpWKit 0.4 CONFIG REQUIRED)")
replace_once(
    "docs/testing.md",
    "The D2D workflow runs the real VSPW-TP/UDP integration tests and `backend_contract_udp`. It installs SpWKit, then builds `examples/distributed` as a **separate C-only** `find_package(SpWKit 0.3)` consumer with `CXX=/bin/false`.",
    "The D2D workflow runs the real VSPW-TP/UDP integration tests and `backend_contract_udp`. It installs SpWKit, then builds `examples/distributed` as a **separate C-only** `find_package(SpWKit 0.4)` consumer with `CXX=/bin/false`.")
replace_once(
    "docs/testing.md",
    "### HIL\n\nThe HIL workflow remains explicit/manual and must fail if someone claims hardware is ready without the required harness. No hosted workflow result is presented as physical SpaceWire evidence.\n\n## Test labels",
    '''### Virtual device

The Virtual device workflow separates the portable daemon/protocol profile from the hosted public backend. GCC and Clang pure-C jobs cover VSPD codec/seqpacket behavior, `vspwd` process lifecycle, public `SPW_BACKEND_DEVICE`, the shared backend contract, readiness, peer loss/restart and ASan+UBSan. A separate pure-C bridge job exercises device↔`vspwd`↔VSPW-TP/UDP exchange and remote restart.

### Tools

The Tools workflow builds `vspwd`, `spwctl` and `spwmon` with GCC and Clang under `CXX=/bin/false`, runs live management/monitor integration, then installs and smoke-tests all three CLIs.

### Installed device examples

The Installed device examples workflow installs an actual device-enabled package, builds standalone C and optional C++ consumers against exported targets only, and executes C↔C, C++↔C++, C↔C++ and C++↔C process pairs through the installed daemon.

### CUSE feasibility

CUSE feasibility runs inside the C-only workflow under GCC and Clang. It validates the private record codec and linked libfuse3 API without claiming full `/dev/cuse` runtime evidence on hosted runners that do not expose the device. Production CUSE is tracked by #78 and is not a v0.4 release gate.

### HIL

The HIL workflow remains explicit/manual and must fail if someone claims hardware is ready without the required harness. No hosted workflow result is presented as physical SpaceWire evidence, and HIL is not a v0.4 release blocker while hardware is unavailable.

## Test labels''')
replace_once(
    "docs/testing.md",
    "Future device/physical/standards work may additionally use `device`, `embedded`, `hil`, and `compliance` as evidence categories.",
    "Additional active labels include `device`, `protocol`, `process`, `restart`, `tools`, `management`, `monitor` and `bridge`. Future physical/standards work may add `hil` and `compliance` evidence when real targets exist.")
replace_regex(
    "docs/testing.md",
    r"## v0\.4 device verification direction\n.*?\n## ECSS evidence",
    '''## v0.4 device verification

The v0.4 Linux virtual-device/service boundary has executable evidence for:

- VSPD golden/malformed protocol vectors and Linux seqpacket behavior;
- raw daemon process lifecycle, edge cases and restart;
- public C device applications and the optional C++ wrapper through the same backend;
- backend-neutral non-consuming packet/time-code readiness;
- shared contract packet, EOP/EEP, zero-length, no-truncation, timeout, statistics and recovery behavior;
- `spwctl` non-owning management and `spwmon` bounded passive observation;
- installed-package C/C++ device consumers and mixed-language process pairs;
- VSPW-TP/UDP bridging with DATA/time codes, remote peer loss and fresh-process recovery under GCC and Clang;
- ASan+UBSan on the public device/daemon path;
- CUSE record/libfuse3 feasibility without a false hosted-kernel claim.

The broader release matrix additionally retains simulator, VSPW-TP D2D/network-namespace, static/shared package, freestanding, cross-platform host and Wireshark/tshark evidence.

Release tags must execute the release-critical CI workflows; physical HIL remains manual and outside the v0.4 claim.

## ECSS evidence''')

# ---------------------------------------------------------------------------
# VSPD/vspwd/tool docs: correct accumulated protocol-history drift.
# ---------------------------------------------------------------------------
replace_regex(
    "docs/vspwd.md",
    r"## Not in this slice\n.*?\n\n## VSPW-TP/UDP bridge",
    '''## Deliberately outside the v0.4 core release

The completed v0.4 device/service boundary still does **not** claim:

- the production event-driven `/dev/vspwX` CUSE presenter tracked by #78;
- generic router/topology configuration or multi-hop SpaceWire routing;
- external administrative START/STOP/RESET override of application-owned ports;
- physical SpaceWire hardware, FPGA/DMA drivers or electrical interoperability evidence.

`spwctl`, `spwmon` and the single topology-owned VSPW-TP/UDP bridge are part of v0.4 and remain private service layers beneath the public application API.

## VSPW-TP/UDP bridge''')

replace_once(
    "docs/vspw-device-protocol.md",
    "The v1.2 codec currently requires an exact 1.2 match. Version-range negotiation can be introduced in a later protocol revision rather than inferred from native package versions.",
    "The v1.3 codec currently requires an exact 1.3 match. Version-range negotiation can be introduced in a later protocol revision rather than inferred from native package versions.")
replace_once(
    "docs/vspw-device-protocol.md",
    "VSPD 1.3 extends the same HELLO-only plane with passive subscriptions used by `spwmon`; lifecycle overrides and topology mutation remain deliberately absent.",
    "VSPD 1.2 extended the same HELLO-only plane with passive subscriptions used by `spwmon`; VSPD 1.3 adds bridged-port metadata. Lifecycle overrides and topology mutation remain deliberately absent.")
replace_once(
    "docs/vspw-device-protocol.md",
    "Flags report attached, started, reset-latched and ever-attached state.",
    "Flags report attached, started, reset-latched, ever-attached and topology-owned bridged state.")
replace_once(
    "docs/vspw-device-protocol.md",
    "VSPD 1.3 adds `SUBSCRIBE_PORT` and `UNSUBSCRIBE_PORT` on the same HELLO-only management connection. A successful subscription queues an immediate `PORT_SNAPSHOT_EVENT`, then the daemon emits another snapshot whenever observable metadata changes.",
    "VSPD 1.2 added `SUBSCRIBE_PORT` and `UNSUBSCRIBE_PORT` on the same HELLO-only management connection. A successful subscription queues an immediate `PORT_SNAPSHOT_EVENT`, then the daemon emits another snapshot whenever observable metadata changes. VSPD 1.3 keeps that subscription wire shape and adds the `BRIDGED` flag to the existing port-info flags field.")
replace_once(
    "docs/spwmon.md",
    "- attached, started, reset-latched and ever-attached flags;",
    "- attached, started, reset-latched, ever-attached and bridged flags;")

print("v0.4 release-audit source/docs patch applied")
