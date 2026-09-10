# Roadmap

The roadmap is organized around evidence boundaries rather than aspirational backend names. Stable tags remain immutable; `develop` is the integration branch for subsequent work.

## Released milestones

### v0.1

Established the portable SpaceWire-facing C API, deterministic loopback/process-local simulation, bounded resource behavior, EOP/EEP, time codes, caller-owned construction and zero-copy ownership semantics.

### v0.2

Added distributed VSPW-TP/UDP, fragmentation/reassembly, session/liveness, retry/deduplication, virtual timing, deterministic transport/SpaceWire fault injection and capture tooling.

### v0.3

Expanded package/consumer and portability evidence while keeping the application API transport-neutral and moving the runtime to an authoritative C11 implementation.

### v0.4

Delivered the Linux virtual-device/service boundary:

- `SPW_BACKEND_DEVICE`;
- VSPD;
- `vspwd`;
- `spwctl`;
- `spwmon`;
- installed C/C++ device consumers;
- VSPW-TP bridge integration.

### v0.5

Completed hosted-platform parity and embedded/RTOS integration evidence:

- native Windows/Winsock VSPW-TP/UDP;
- production CUSE `/dev/vspwX` presentation;
- HardRT POSIX execution integration;
- Cortex-M7/HardRT compile-link integration;
- multi-architecture Debian publication (`amd64`, `arm64`, `armhf`, `riscv64`);
- multi-platform GHCR publication;
- v0.5.1 maintenance parity for the optional C++17 wrapper.

### v0.6.0

Completed the public hardware-driver integration boundary without publishing proprietary hardware implementation details.

```mermaid
flowchart LR
    VIRT[Stable virtual backends] --> DRIVER[Portable driver contract]
    DRIVER --> DMA[DMA/zero-copy mapping]
    DMA --> REF[Reference-driver evidence]
    REF --> STM[STM32H755 physical DMA/cache evidence]
    REF --> FPGA[Public future-FPGA provider boundary]
    VIRT --> CCSDS[CCSDSPack v2.0.0 integration]
    STM --> REL[v0.6.0]
    FPGA --> REL
    CCSDS --> REL
```

Delivered evidence includes:

- public `SPW_BACKEND_DRIVER` configuration/callback contract;
- driver ABI v2 DMA/zero-copy ownership mapping;
- bounded wrapper slots and no-heap compatibility;
- deterministic host reference driver;
- driver/DMA contract and cache-hook tests;
- immutable CCSDSPack `v2.0.0` baseline at `c2f318c330c564429bcc565a8acbff22728b2851`;
- installed-package CCSDSPack PUS-C interoperability over UDP and Linux DEVICE/VSPD;
- two-node Docker Compose CCSDSPack-over-VSPW-TP/UDP exchange;
- proprietary-safe future FPGA/provider boundary documentation and generic HIL acceptance criteria;
- physical NUCLEO-H755ZI-Q validation with real DMA2, Cortex-M7 cache synchronization and reset-time stale-buffer invalidation.

The STM32 qualification is real MCU DMA/cache/zero-copy evidence. It is not SpaceWire electrical or PHY interoperability evidence.

### v0.6.1

Consolidated the v0.6 contract as a patch release without an intentional public API/ABI break:

- finalized profiling backend discovery plus host/build/counter metadata;
- retained the accepted VSPW-TP reassembly optimization, including the controlled 4096-byte RX paired-overhead reduction from 60,281 to 25,032 invariant-TSC ticks;
- reduced avoidable readiness overhead in POSIX UDP and Linux DEVICE/VSPD while preserving timeout/error semantics;
- synchronized package, testing, profiling and hardware-evidence documentation;
- published immutable `v0.6.1` source, multi-architecture Debian packages and the multi-architecture GHCR image through the release-policy workflow.

## v0.7.0 release candidate

The behavioral/backend-contract engineering planned for v0.7 is implemented on `develop`:

- the deterministic DRIVER/reference provider executes the reusable public backend contract (#211);
- threading, lifecycle, error, timeout, resource and zero-copy reset/ownership semantics are defined and enforced (#212);
- simulator-to-backend behavioral equivalence is versioned and executable across SIMULATOR, VSPW-TP/UDP, Linux DEVICE/VSPD and DRIVER (#214).

`v0.7.0` is not considered complete merely because those functional changes pass CI. Publication is gated by #224.

The release gate is now implemented as evidence/tooling on PR #225:

1. ECSS-E-ST-50-12C Rev.1 has a project-owned `v0.7-r2` applicability/traceability matrix with specifically enumerated positive software claims and explicit delegated/future/not-applicable dispositions;
2. concrete controller/PHY/electrical and complete node time-code-engine requirements are delegated explicitly to the provider, with `spwkit-fpga` responsible for its applicable hardware-side evidence;
3. release-performance tooling compares immutable `v0.6.1` with the candidate using matched public-operation/native-relative metrics and separate lifecycle evidence;
4. GitHub-hosted CI repeats the paired screen three times and aggregates only recurring positive threshold crossings as targets for investigation;
5. final acceptance still requires repeated controlled-host measurements for the release candidate, investigation of reproducible regressions, and optimization of avoidable overhead before the tag.

A performance regression is accepted only when it is a necessary correctness/semantic tradeoff, quantified and documented. Known avoidable release-candidate cleanup is not intentionally deferred to `v0.7.1` simply because a patch number is available.

See [`ecss-conformance.md`](ecss-conformance.md) for the software/provider conformance boundary and [`../benchmarks/RELEASE_PERFORMANCE.md`](../benchmarks/RELEASE_PERFORMANCE.md) for the performance gate.

## Post-v0.7 directions

Later work may include:

- a real FPGA/vendor SpaceWire driver implementation below the existing public driver contract;
- physical SpaceWire HIL and electrical interoperability evidence;
- physical host adapters, including a future USB-to-SpaceWire hardware path;
- additional RTOS/platform adapters;
- upper-layer protocols such as RMAP kept modular above the link API;
- broader requirements/compliance traceability;
- longer-term ASIC-backed physical interface work where justified by cost and qualification needs.

SpWKit remains focused on endpoint/link communication rather than becoming a router implementation. Networks containing SpaceWire routers can still be reached through routing information handled above or below the link boundary, but generic router implementation is not a core SpWKit deliverable.

No later milestone is considered delivered merely because an interface placeholder or documentation concept exists.
