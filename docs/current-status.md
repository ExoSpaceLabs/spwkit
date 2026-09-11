# Current project status

## Stable release: v0.7.0

`v0.7.0` is the current stable software-contract release. It builds on the v0.6 hardware-provider boundary and profiling work by making backend behavior, lifecycle/error/timeout/resource semantics, zero-copy ownership, simulator equivalence, ECSS software-scope conformance, and release-performance acceptance explicit and executable.

The v0.7 software line includes:

- process-local SpaceWire simulation;
- VSPW-TP/UDP on POSIX hosts and native Windows/Winsock;
- Linux `SPW_BACKEND_DEVICE`, VSPD and `vspwd`;
- `spwctl`, `spwmon`, and optional CUSE `/dev/vspwX` presentation;
- caller-owned/no-heap construction and optional zero-copy ownership;
- public `SPW_BACKEND_DRIVER` configuration/callback contract;
- reusable backend-contract tests for the deterministic DRIVER/reference provider (#211);
- defined threading, lifecycle, timeout, error, resource and reset-safe zero-copy ownership semantics (#212);
- backend behavioral-equivalence matrix `v1-r1` across SIMULATOR, UDP, DEVICE/VSPD and DRIVER (#214);
- DMA-capable driver buffer ownership through the existing `spw_buffer_t` API;
- deterministic host reference-driver and freestanding/no-heap evidence;
- accepted CCSDSPack `v2.0.0` interoperability baseline at commit `c2f318c330c564429bcc565a8acbff22728b2851`;
- installed-package PUS-C TC/TM transport over VSPW-TP/UDP and Linux DEVICE/VSPD paths;
- deployment-shaped two-node Docker Compose interoperability evidence;
- physical NUCLEO-H755ZI-Q Cortex-M7 DMA/cache qualification through the public driver boundary;
- scoped ECSS-E-ST-50-12C Rev.1 software conformance through the `v0.7-r2` traceability matrix;
- controlled performance-regression evidence against immutable `v0.6.1`;
- multi-architecture Debian and GHCR publication for `amd64`, `arm64`, `armhf`, and `riscv64` hosted targets.

See [v0.7.0 release notes](releases/v0.7.0.md).

## Runtime and backend contract

The v0.7 contract explicitly defines the software-visible behavior that future physical providers must preserve:

- overlapping public calls on the same `spw_port_t` are application-serialized;
- distinct handles may be used concurrently, subject to any shared native-controller/provider constraints;
- `stop`, `reset`, and `close` are lifecycle operations rather than asynchronous cancellation primitives;
- zero-copy handles may move between threads only with application synchronization and a single logical owner at a time;
- immediate timeouts perform no deliberate wait, finite timeouts budget the complete public operation, and infinite waits remain terminable by terminal link/backend state;
- canonical result values distinguish argument/state, timeout, unsupported, resource, link, buffer, packet and backend failures;
- reset creates a new zero-copy ownership epoch and stale pre-reset handles are rejected;
- the represented ECSS time-code profile is limited to six-bit counts and `control_flags == 0`.

The common backend-contract suite is reused across SIMULATOR, VSPW-TP/UDP, Linux DEVICE/VSPD and the reference DRIVER. Behavioral equivalence concerns application-visible semantics; it does not assert timing equivalence with physical SpaceWire hardware.

## ECSS SpaceWire conformance policy

SpWKit targets **ECSS-E-ST-50-12C Rev.1** within the limits of the endpoint/link software abstraction implemented by this repository.

The v0.7 release maintains a project-owned applicability/traceability matrix at `tests/compliance/ecss-e-st-50-12c-rev1.md`, revision `v0.7-r2`. Positive conformance claims apply only to the specifically enumerated rows classified **Software verified**.

The current positive software claim covers seven mapped requirements concerning:

- EOP/EEP packet termination preservation;
- zero-data packets;
- transparent packet cargo;
- endpoint packet send/receive service semantics;
- the represented time-code specialization/type;
- six-bit time-code count values;
- endpoint time-code service semantics when the capability is advertised.

The matrix separately records requirements that are:

- implemented and verified by SpWKit software;
- delegated to a concrete hardware/provider implementation;
- not applicable to the endpoint/link scope;
- applicable software capabilities not yet implemented and therefore not claimed.

Physical-layer, encoding, concrete controller/link-engine, flow-control, link-initialization/recovery, electrical and complete node time-code-engine requirements remain provider/hardware responsibilities. The private `spwkit-fpga` project owns the applicable ExoSpaceLabs FPGA/controller/PHY evidence. Full end-to-end SpaceWire conformance for a concrete system requires evidence from both layers.

Distributed interrupts, standardized node-management parameters and the SpaceWire MIB/service remain outside the v0.7 positive claim. Their pre-v1 disposition is recorded in the roadmap. Generic router implementation remains outside the core SpWKit product scope.

See [`ecss-conformance.md`](ecss-conformance.md) and the [traceability matrix](../tests/compliance/ecss-e-st-50-12c-rev1.md).

## v0.7.0 release-performance gate

The immutable comparison baseline is `v0.6.1` at `03869c0b3bc9e895e0fe61a36f24cff2ace7d527`.

The release-performance tooling performs paired baseline/candidate measurement and repeated-screen aggregation. Controlled same-runner measurements were then used to distinguish reproducible software changes from hosted counter/scheduler noise.

The final investigation established:

- duplicate generic packet-shape validation in LOOPBACK was avoidable after validation became authoritative in the public core path and was removed before release;
- controlled post-cleanup measurements show no recurring LOOPBACK regression;
- DRIVER copied paths and lifecycle measurements remain effectively flat;
- no recurring significant UDP regression was reproduced in the controlled campaign;
- Linux DEVICE measurements showed large hosted-runner variance/sign changes rather than a stable source-correlated regression;
- an apparent RX zero-copy release plateau was traced to per-call counter-floor quantization; a batched 2,000,000-pair probe isolated the actual reset-safe ownership guard cost at approximately **+2.06 invariant-counter ticks per acquire+release pair**;
- a semantics-preserving direct rewrite was slightly slower, so the required ownership protection is retained unchanged.

No unexplained reproducible software regression remains that warrants additional runtime optimization before v0.7.0. Hosted timing values remain regression/reference evidence for their named environment, not physical SpaceWire performance claims.

See [`../benchmarks/v0.7.0-performance-evidence.md`](../benchmarks/v0.7.0-performance-evidence.md) and [`../benchmarks/RELEASE_PERFORMANCE.md`](../benchmarks/RELEASE_PERFORMANCE.md).

## STM32H755 qualification

The existing physical-board evidence remains part of the provider-contract foundation:

```text
magic                   = 0x53505736
phase                   = 0x0000700d
result                  = 0x00000000
sync_to_device          = 1
sync_from_device        = 1
dma_transfers           = 2
tx_packets              = 2
rx_packets              = 2
reset_stale_invalidated = 1
RESULT: PASS
```

This validates real STM32 DMA2 execution, Cortex-M7 cache clean/invalidate ownership transitions, copied and zero-copy packet paths, and reset-time stale-buffer invalidation. It is MCU driver evidence, not physical SpaceWire electrical/PHY interoperability evidence.

## Profiling reference

The profiling infrastructure retains the v0.6/v0.7 software/provider performance evidence:

- controlled hosted DRIVER/native differential and copied-vs-zero-copy characterization;
- complete LOOPBACK, SIMULATOR, VSPW-TP/UDP and DEVICE/VSPD boundary instrumentation;
- NUCLEO-H755ZI-Q Cortex-M7 DWT measurements for copied and zero-copy DMA-provider paths;
- matched STM32H755 direct/native DMA2 differential measurements;
- lifecycle/startup measurements kept separate from steady-state TX/RX data;
- VSPW-TP stage attribution and the accepted reassembly optimization;
- POSIX UDP and Linux DEVICE/VSPD readiness-path optimization;
- campaign metadata covering host/build/counter context and explicit backend coverage states.

The canonical interpretation limits are documented in [`profiling.md`](profiling.md) and [`profiling-results.md`](profiling-results.md). Hosted x86 values are invariant TSC/counter ticks; Cortex-M7 values are DWT cycles. Neither virtual transport nor generic STM32 DMA evidence is presented as SpaceWire controller/PHY/link timing.

## CCSDSPack integration baseline

SpWKit v0.7 retains the immutable external integration reference:

- tag: `CCSDSPack v2.0.0`;
- commit: `c2f318c330c564429bcc565a8acbff22728b2851`.

CCSDSPack remains optional and external. `libspwkit` does not include or link CCSDSPack.

## Hardware boundary

The public repository defines the portable driver semantics and generic HIL acceptance criteria. It does not publish proprietary FPGA/HDL implementation details, register maps, descriptor layouts, bus/clock/reset/interrupt architecture, or electrical design.

Physical FPGA-backed SpaceWire interoperability remains a later validation layer described in [`hardware-acceptance.md`](hardware-acceptance.md). Introducing a conforming physical provider is not intended to require redesign of the application-facing `spw_port_*` logic.

## Road to v1.0

The v1.0 objective is a stable software-facing API/backend contract that external applications and future hardware providers can depend on without redesigning the application layer.

```text
v0.7.0  behavioral/backend contract + ECSS SpaceWire software scope + performance gate
v0.8.x  public API/ABI cleanup, DRIVER contract candidate, ECSS-E-ST-40 applicability/API-impact work
v0.9.x  API freeze, ECSS-E-ST-40/Q-ST-80 evidence hardening, compatibility gates, fuzz/soak, 1.0 RC
v1.0.0  stable software contract and documented standards applicability/compliance status
```

During v0.8, distributed interrupts, standardized node-management parameters and SpaceWire MIB/service are evaluated before API freeze. Routing remains outside the core endpoint/link scope unless the project deliberately changes that architecture.

See #209 and workstreams #210-#216 plus [`roadmap.md`](roadmap.md).

## Development flow after v0.7.0

`main` tracks stable releases and `develop` remains the integration branch for subsequent work. Temporary release/feature branches are deleted after integration; immutable tags and release artifacts preserve release history.
