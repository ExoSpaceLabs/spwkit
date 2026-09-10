# Current project status

## Stable release: v0.6.1

`v0.6.1` is the current stable release. It is the maintenance/performance consolidation of the v0.6 software contract and preserves the public application-facing `spw_port_*` and backend contract while incorporating the completed profiling infrastructure, accepted readiness/reassembly optimizations, and synchronized documentation.

The stable v0.6 line includes:

- process-local SpaceWire simulation;
- VSPW-TP/UDP on POSIX hosts and native Windows/Winsock;
- Linux `SPW_BACKEND_DEVICE`, VSPD and `vspwd`;
- `spwctl`, `spwmon`, and optional CUSE `/dev/vspwX` presentation;
- caller-owned/no-heap construction and optional zero-copy ownership;
- public `SPW_BACKEND_DRIVER` configuration/callback contract;
- DMA-capable driver buffer ownership through the existing `spw_buffer_t` API;
- deterministic host reference-driver and freestanding/no-heap evidence;
- accepted CCSDSPack `v2.0.0` interoperability baseline at commit `c2f318c330c564429bcc565a8acbff22728b2851`;
- installed-package PUS-C TC/TM transport over VSPW-TP/UDP and Linux DEVICE/VSPD paths;
- deployment-shaped two-node Docker Compose interoperability evidence;
- physical NUCLEO-H755ZI-Q Cortex-M7 DMA/cache qualification through the public driver boundary;
- multi-architecture Debian and GHCR publication for `amd64`, `arm64`, `armhf`, and `riscv64` hosted targets.

See [v0.6.1 release notes](releases/v0.6.1.md).

## v0.6.1 profiling and performance consolidation

v0.6.1 records the post-v0.6 profiling/performance work without intentionally redesigning the public contract.

Included work:

- hosted and physical STM32 profiling/reference campaigns;
- VSPW-TP reassembly optimization, reducing the isolated fragmented reassembly component by about **96-97%**;
- controlled 4096-byte VSPW-TP RX paired overhead reduction from **60,281 to 25,032 TSC ticks** (**58.5%**);
- POSIX UDP optimistic-ready I/O, removing unconditional poll-first work where `MSG_DONTWAIT` is available;
- Linux DEVICE/VSPD optimistic-ready record I/O, saving roughly **813-850 TSC ticks** in the hosted paired readiness microbenchmark and reducing the raw operation by about **41-51%**;
- finalized profiling host/build/counter metadata and backend coverage classification from #171/#173;
- synchronized release/evidence documentation from #208.

Hosted timing results are reference/regression evidence for the named measurement environment. They are not physical SpaceWire controller, PHY, cable, or universal performance specifications.

## STM32H755 qualification

The phase-7 physical-board run completed successfully with:

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

The profiling campaign provides reproducible software/provider performance evidence on top of the v0.6 functional boundary:

- controlled i7-1355U hosted DRIVER/native differential and copied-vs-zero-copy crossover characterization;
- complete hosted LOOPBACK, SIMULATOR, VSPW-TP/UDP and DEVICE/VSPD boundary instrumentation;
- NUCLEO-H755ZI-Q Cortex-M7 DWT measurements for copied and zero-copy DMA-provider paths;
- matched STM32H755 direct/native DMA2 differential measurements;
- controlled lifecycle/startup measurements kept separate from steady-state TX/RX data;
- controlled VSPW-TP stage attribution followed by the accepted reassembly optimization;
- POSIX UDP and Linux DEVICE/VSPD readiness-path optimization with equivalent-path comparator checks;
- campaign metadata covering host/build/counter context and explicit backend coverage states (`measured`, `unsupported-platform`, `not-built`, `not-implemented-benchmark`).

The canonical measurement contract and interpretation limits are documented in [`profiling.md`](profiling.md). The accepted statistics and measured before/after achievements are summarized in [`profiling-results.md`](profiling-results.md). Hosted x86 values are reported as invariant TSC ticks; Cortex-M7 values are DWT cycles. Neither virtual transport nor generic STM32 DMA evidence is presented as SpaceWire controller/PHY/link timing.

## CCSDSPack integration baseline

SpWKit v0.6 accepts the immutable external integration reference:

- tag: `CCSDSPack v2.0.0`;
- commit: `c2f318c330c564429bcc565a8acbff22728b2851`.

CCSDSPack remains optional and external. `libspwkit` does not include or link CCSDSPack.

## Hardware boundary

The public repository defines the portable driver semantics and generic HIL acceptance criteria. It does not publish proprietary FPGA/HDL implementation details, register maps, descriptor layouts, bus/clock/reset/interrupt architecture, or electrical design.

Physical FPGA-backed SpaceWire interoperability remains a later validation layer described in [`hardware-acceptance.md`](hardware-acceptance.md).

## v0.7 development status

The planned v0.7 behavioral/backend-contract engineering scope is now implemented on `develop`:

- the deterministic DRIVER/reference provider executes the reusable public backend contract (#211);
- the stable threading, lifecycle, error, timeout, resource and zero-copy ownership semantics are defined and executable (#212);
- the simulator-to-backend behavioral-equivalence matrix and aggregate proof are implemented across SIMULATOR, UDP, DEVICE/VSPD and DRIVER (#214).

This does **not** yet make `v0.7.0` a released version. Final publication is gated by #224.

## ECSS SpaceWire conformance policy

SpWKit targets **ECSS-E-ST-50-12C Rev.1** within the limits of the software/runtime abstraction implemented by this repository.

The project should make positive, evidence-backed conformance claims for applicable software requirements instead of using a blanket disclaimer. A requirement-level applicability/traceability matrix distinguishes:

- software requirements implemented and verified by SpWKit;
- requirements delegated to a concrete hardware/provider implementation;
- requirements that are not applicable to the SpWKit software scope;
- applicable requirements that are not yet implemented and therefore are not claimed.

The private `spwkit-fpga` project is responsible for the FPGA/controller/PHY/electrical conformance evidence that belongs below the public DRIVER boundary. Full end-to-end SpaceWire conformance for a concrete system requires the applicable evidence from both layers.

See [`ecss-conformance.md`](ecss-conformance.md) and #224.

## v0.7.0 release quality gate

Before `v0.7.0` is tagged, the release candidate must satisfy both the software-scope ECSS traceability gate and a performance-regression gate against immutable `v0.6.1`.

Any v0.7 change that can affect a hot path or lifecycle cost must be measured using the existing controlled profiling infrastructure. Reproducible regressions outside normal measurement noise are investigated; avoidable overhead is optimized before release. A regression may be accepted only when it is a necessary correctness/semantic tradeoff, is quantified, and is documented.

Known avoidable release-candidate work is not intentionally deferred to `v0.7.1`. Patch releases remain available for defects discovered after publication, not as a substitute for completing the release candidate.

## Road to v1.0

The v1.0 objective is a stable software-facing API/backend contract that external applications and future hardware providers can depend on without redesigning the application layer. FPGA RTL, USB adapters, ASICs, router implementation and optional upper layers do not block the generic software release boundary. Software-scope ECSS conformance remains an explicit requirement; physical/provider conformance is evidenced by the corresponding provider implementation.

The planned progression is evidence-driven:

```text
v0.6.1  profiling/performance consolidation and documentation sync
v0.7.x  behavioral/backend contract hardening and simulator equivalence
v0.8.x  public API/ABI cleanup and DRIVER contract candidate
v0.9.x  API freeze, compatibility gates, fuzz/soak and 1.0 RC
v1.0.0  stable software contract
```

See #209 and its tracked workstreams #210-#216 plus the v0.7 release gate #224 for the pre-v1 contract-hardening plan.

## Development flow after v0.6.1

`main` tracks stable releases and `develop` remains the integration branch for subsequent work. No post-v0.6.1 feature is considered delivered until it has its own implementation and evidence boundary.