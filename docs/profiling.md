# Profiling methodology and reference results

SpWKit profiling measures software boundaries without silently turning host, MCU, FPGA and physical-link timing into one number. This document is the canonical contract for profiling configuration, result interpretation and accepted reference snapshots.

The primary rule is simple: **measure and name the boundary that the number actually represents**. A software dispatch result is not a DMA transfer result, a DMA-provider result is not SpaceWire controller latency, and a virtual transport is not a timing-equivalent SpaceWire link.

## Evidence domains

| Domain | Primary unit | What it can establish | What it does not establish |
|---|---|---|---|
| Hosted software | architectural counter ticks | public API, backend, provider and transport software cost | MCU/controller/PHY timing |
| Cortex-M software/provider | DWT CYCCNT cycles | embedded API/provider/cache/DMA preparation and completion boundaries | SpaceWire controller/codec/PHY/link timing |
| FPGA/IP core | FPGA clock cycles | controller/core pipeline latency | host/MCU software or cable/link latency |
| Physical HIL | elapsed time, throughput, jitter and error behavior | complete controller/PHY/cable/link behavior | attribution to one software layer without separate probes |

On x86, the profiling counter is invariant `RDTSCP`/TSC. Values are **TSC ticks**, not dynamic core clock cycles. On Cortex-M7, DWT CYCCNT values are core cycles. Do not relabel either unit for convenience.

## Probe contract

Profiling is compiled with `SPWKIT_ENABLE_PROFILING=ON` and exactly one start/end probe pair selected by `SPWKIT_PROFILE_START` and `SPWKIT_PROFILE_END`. All unselected probes compile to no-ops.

The production probe vocabulary is defined in `src/profiling/profile.h`. The copied packet path uses:

- `TX_API_ENTRY`: application enters `spw_port_send()`;
- `TX_BACKEND_ENTRY`: selected backend begins TX handling;
- `TX_PROVIDER_ENTRY`: backend hands the operation to provider/native work;
- `TX_PROVIDER_BOUNDARY`: provider-specific native submission boundary;
- `RX_PROVIDER_BOUNDARY`: provider/native completion or data-ready boundary;
- `RX_PROVIDER_RETURN`: provider returns toward the backend;
- `RX_BACKEND_RETURN`: backend returns toward the public API;
- `RX_API_RETURN`: packet becomes visible to the application.

Zero-copy adds explicit TX acquire/submit/reclaim/release and RX acquire/release probes, including sync entry/return and provider-owned submission/data-ready markers.

For `SPW_BACKEND_DRIVER`, the provider/native boundary is provider-owned. Calling a driver callback does not by itself prove DMA/MMIO submission or completion. The provider places the corresponding boundary marker at the native event it owns.

### Backend boundary mapping

The hosted backends use one logical packet boundary even when their carrier fragments the packet:

- **LOOPBACK:** packet committed to / available from the in-memory queue;
- **SIMULATOR:** packet committed to / available from the peer queue;
- **VSPW-TP/UDP:** complete logical packet handed to UDP and logical packet available after receive/reassembly;
- **DEVICE/VSPD:** logical DATA_TX handoff over AF_UNIX and logical DATA_RX availability after reassembly;
- **DRIVER:** provider-defined native submission/completion markers.

The 4096-byte UDP and DEVICE/VSPD probe-smoke cases deliberately exercise fragmentation while requiring exactly one completed profiling sample (`sequence == 1`).

## Measurement ranges

A result must state its range, not just its operation name.

### Copied TX

The main abstraction range is:

```text
TX_API_ENTRY -> TX_PROVIDER_BOUNDARY
```

This measures application-side public API work through native/provider submission. It does **not** include subsequent hardware transfer or completion unless the provider itself is synchronous and that behavior is explicitly part of the tested boundary.

### Copied RX

The main visibility range is:

```text
RX_PROVIDER_BOUNDARY -> RX_API_RETURN
```

This measures native/provider data-ready to application visibility. Fixture-specific synchronous behavior must be called out where it contributes to that interval.

### Zero-copy TX

Acquire, submit, reclaim and release are separate ownership operations. The same-boundary copied-vs-zero-copy submission comparison is:

```text
copied:    TX_API_ENTRY -> TX_PROVIDER_BOUNDARY
zero-copy: TX_ZC_SUBMIT_API_ENTRY -> TX_ZC_SUBMIT_PROVIDER_BOUNDARY
```

That comparison assumes the zero-copy TX buffer is already acquired. If acquire is a per-packet cost in the application, add it explicitly rather than claiming the submit-only crossover applies to the complete lifecycle.

### Zero-copy RX

The visibility comparison is:

```text
copied:    RX_PROVIDER_BOUNDARY -> RX_API_RETURN
zero-copy: RX_ZC_ACQUIRE_PROVIDER_BOUNDARY -> RX_ZC_ACQUIRE_API_RETURN
```

Release remains a separate ownership cost. For a complete receive lifecycle comparison, include acquire plus release.

## Reproducibility rules

Reference campaigns follow these rules:

1. **Release configuration.** Benchmark claims come from optimized Release builds, not debugger-oriented builds.
2. **Clean rebuild per compiled probe pair.** Probe selection is compile-time configuration and must not reuse a stale object tree.
3. **Serial execution.** Cases run one at a time unless concurrency is the subject of the measurement.
4. **Warmup before measured samples.** Published snapshots state warmup and measured iteration counts.
5. **Counter-floor calibration per case/configuration.** Back-to-back counter reads are recorded as a diagnostic floor. The floor is **never blindly subtracted** from operation results.
6. **Raw samples remain authoritative.** JSON/JSONL artifacts keep exact values. Human summaries may round tick/cycle values for readability.
7. **Platform metadata is mandatory.** Record architecture, counter kind/frequency where known, compiler/version, optimization mode, payload, build commit and relevant backend/provider configuration.
8. **Controlled-host affinity.** Reference host campaigns pin the complete child-process tree to one logical CPU and precondition the CPU. Governor/driver state is recorded rather than assumed.
9. **Fixed sample storage.** Profiling fixtures avoid allocator noise in steady-state packet measurements. Heap effects are measured only when intentionally profiling heap-backed lifecycle APIs.
10. **Cache policy is part of the fixture.** Cache clean/invalidate behavior must match between compared paths and may also be measured separately as a diagnostic contribution.

GitHub-hosted numbers validate benchmark mechanics, schemas and regressions. They are not authoritative performance claims because runner placement, CPU policy and host generation are uncontrolled.

## Direct/native comparison rules

A `SpWKit - native` differential is valid only when both sides are matched:

- same provider/controller implementation;
- same buffers and alignment;
- same payload;
- same cache clean/invalidate operations;
- same submission/completion semantics;
- same build/optimization mode;
- alternating native-first / SpWKit-first measured order;
- paired samples from the same firmware/process where practical.

The authoritative differential is the paired signed value:

```text
SpWKit - native
```

Absolute native and SpWKit statistics are still reported, but subtracting unrelated medians from separate campaigns is not a substitute for a paired differential.

## Submission, transfer and completion

Do not use the generic word "latency" when the interval is ambiguous.

- **submission latency:** API/preparation to provider/native submission;
- **transfer latency:** hardware/carrier work after submission;
- **completion latency:** native completion/data-ready to software visibility;
- **end-to-end latency:** complete sender-to-receiver path under a named topology.

The profiling work in this repository primarily characterizes software submission/visibility boundaries. Physical SpaceWire end-to-end latency remains a separate HIL activity.

## Copied versus zero-copy interpretation

Zero-copy is an ownership model, not an automatic performance guarantee. It replaces copying with buffer acquisition, synchronization, submission, completion/reclaim and release work. A crossover therefore depends on:

- payload size;
- provider implementation;
- cache architecture/policy;
- buffer ownership lifetime;
- whether acquire/release are amortized across application work;
- whether the comparison is submit-only, visibility-only or a full ownership lifecycle.

Crossover sizes are platform-specific. Do not copy a crossover from x86 to Cortex-M, or from the STM32 DMA2 memory-to-memory fixture to a future SpaceWire controller.

## Accepted reference snapshots

These snapshots are engineering baselines, not universal performance specifications.

### Controlled i7-1355U hosted reference

Reference run:

```text
host-20260909T000756Z-f79b56ff
commit f79b56ffab499ef5e0748ec9e32045d6023eb3f9
```

Configuration: Release, clean rebuild per case, logical CPU 0, 2 s preconditioning, serial execution, 256 warmup / 1024 measured iterations, payloads 0/1/8/64/256/1024/4096 B. `intel_pstate` remained in `powersave` because the governor entry was not writable; all standalone calibration medians nevertheless stabilized at 33-34 invariant-TSC ticks.

#### DRIVER copied native differential

| Payload | TX `SpWKit - native` | RX `SpWKit - native` |
|---:|---:|---:|
| 0 B | +4 ticks | -1 tick |
| 1 B | +3 | -1 |
| 8 B | +3 | 0 |
| 64 B | +4 | 0 |
| 256 B | +4 | 0 |
| 1024 B | +4 | 0 |
| 4096 B | +4 | +2 |

The basic DRIVER abstraction adds roughly 3-4 invariant-TSC ticks on TX and effectively no measurable RX overhead on this host. Provider/transport/copy work dominates long before the ordinary API dispatch layer does.

#### Controlled copied versus zero-copy crossover

| Payload | copied TX | ZC TX preparation | copied RX visibility | ZC RX visibility |
|---:|---:|---:|---:|---:|
| 0 B | 48 | 203 | 42 | 75 |
| 64 B | 109 | 253 | 57 | 75 |
| 256 B | 246 | 386 | 60 | 75 |
| 1024 B | 813 | 931 | 87 | 75 |
| 4096 B | 3189 | 3106 | 291 | 75 |

Values are median invariant-TSC ticks. TX first becomes beneficial at 4096 B in the tested sweep, placing the controlled-host TX crossover between 1024 and 4096 B. RX crosses between 256 and 1024 B and is strongly beneficial at 4096 B.

VSPW-TP/UDP and DEVICE/VSPD results are intentionally interpreted as transport/provider behavior, not basic API abstraction cost. VSPW-TP has a clear 4096-byte fragmentation/reassembly step change. The virtual transports provide functional and protocol-level validation, not cycle-accurate or timing-equivalent SpaceWire emulation.

### STM32H755 physical DMA-provider reference

Reference run:

```text
stm32h755-20260909T121009Z-e49d70a3
commit e49d70a3d6fa34994de2c6e64e50d03a8016fe1f
```

Configuration: NUCLEO-H755ZI-Q Cortex-M7, DWT CYCCNT @ 64 MHz, Release, clean serial builds, 16 warmup / 64 measured iterations, payloads 0/1/8/64/256 B. Counter floor was exactly 1 cycle in every case and was not subtracted.

#### Physical copied / zero-copy boundaries

| Payload | copied TX | ZC TX submit | copied RX | ZC RX acquire |
|---:|---:|---:|---:|---:|
| 0 B | 47 | 156 | 74 | 284 |
| 1 B | 251 | 300 | 210 | 201 |
| 8 B | 272 | 300 | 242 | 201 |
| 64 B | 312.5 | 320 | 285 | 208 |
| 256 B | 541.5 | 446 | 536 | 250 |

Values are median Cortex-M7 cycles.

When the TX provider-owned buffer is already acquired, submit-only zero-copy crosses between 64 and 256 B and is 17.6% lower at 256 B. TX acquire itself is about 141 cycles, so there is no complete acquire+submit TX crossover within the fixture's 256-byte capacity.

RX visibility is lower with zero-copy from 1 B upward in this fixture. RX release is about 190 cycles; when release is included, the full ownership lifecycle crosses between 64 and 256 B and is 440 vs 536 cycles at 256 B.

Isolated cache clean/invalidate medians are 60/38 cycles at 1 B, 58/38 at 8 B, 72/47 at 64 B and 156/101 at 256 B. At 256 B, the 257-cycle combined diagnostic is comparable to a large fraction of the 541.5-cycle copied-TX path. These values are separate measurements and must not be mechanically subtracted from the packet-path result.

This fixture uses STM32 DMA2 memory-to-memory as the provider. It proves real MCU DMA/cache/provider behavior through the SpWKit driver boundary. It is **not** SpaceWire controller, codec, PHY, cable or link timing.

### STM32H755 direct/native differential

Reference run:

```text
stm32h755-native-20260909T125024Z-6804ebbb
commit 6804ebbb6529d41c4cbed5d99b729428501e70fa
```

Both paths use the same driver/provider, DMA2 stream, D2 SRAM buffers and cache helpers, with native-first / SpWKit-first ordering alternated every measured iteration.

Paired median `SpWKit - native` deltas in Cortex-M7 cycles:

| Payload | copied TX | copied RX | ZC TX submit | ZC RX acquire |
|---:|---:|---:|---:|---:|
| 0 B | +8.5 | +19 | +97.5 | +223.5 |
| 1 B | +18 | +35 | +93 | +168.5 |
| 8 B | +24 | +38 | +93 | +168.5 |
| 64 B | +20.5 | +32.5 | +93 | +166 |
| 256 B | +20.5 | +34 | +93 | +211.5 |

At 64 MHz, the ordinary copied-path SpWKit abstraction delta is roughly 0.13-0.59 us. The larger fixed software cost is concentrated in zero-copy ownership plumbing: TX submit is about 93 cycles (~1.45 us) and RX acquire is roughly 166-224 cycles (~2.6-3.5 us). Large zero-copy percentages should not be overinterpreted when the direct callback denominator is only a few dozen cycles.

### Controlled lifecycle reference

Reference implementation merged from PR #196 after the controlled i7-1355U dataset at head `c4e646085a6af40539774d2f7a253c1a8acc3156`.

Configuration: LOOPBACK, logical CPU 0, 2 s preconditioning, GCC 11.4, invariant x86 TSC, 256 warmup / 1024 measured iterations. Counter floor median/p95/p99 was 33/35/36 ticks and was not subtracted.

| Operation | Median | p95 | p99 |
|---|---:|---:|---:|
| `open_heap` | 1618 | 1657 | 2405 |
| `open_in_place` | 1496 | 1503 | 1507 |
| `start` | 51 | 56 | 58 |
| `stop` | 44 | 47 | 48 |
| `reset` | 1421 | 1425 | 1427 |
| `close_in_place` | 45 | 48 | 52 |
| `close_heap` | 85 | 90 | 95 |

In-place construction is highly deterministic. Heap-backed open adds only about 122 median ticks over in-place construction but has allocator/system tails. `start`, `stop` and caller-owned close are thin dispatch/state transitions close to the counter floor. LOOPBACK reset is larger because it clears packet and time-code queues. Lifecycle results are secondary startup/recovery metrics, not steady-state communication latency.

## Benchmark entry points

Hosted profiling infrastructure lives under `benchmarks/`.

Key entry points include:

- `benchmarks/run_profile_campaign.sh` - complete hosted matrix;
- `benchmarks/run_controlled_profile_campaign.sh` - controlled hosted reference wrapper;
- `benchmarks/summarize_profile_campaign.py` - hosted campaign summary;
- `benchmarks/run_lifecycle_profile.sh` - lifecycle reference and archive generation;
- `benchmarks/summarize_lifecycle_profile.py` - lifecycle summary.

STM32H755 physical profiling uses:

- `scripts/stm32h755_profile_campaign.sh`;
- `scripts/stm32h755_native_compare_campaign.sh`;
- `scripts/summarize_stm32h755_profile.py`;
- `scripts/extract_stm32h755_profile.py`;
- `integrations/stm32h755_dma/`.

The CI workflows validate mechanics and schemas. Controlled-host and physical-board runs provide the accepted performance evidence.

## Publishing new snapshots

A new snapshot should include:

- exact git commit;
- result/archive identifier;
- hardware/host identity at the level needed to reproduce the run;
- compiler and optimization mode;
- counter kind and frequency where applicable;
- payload set;
- warmup and measured iteration counts;
- CPU affinity/governor/cache/provider details that affect interpretation;
- calibration floor;
- raw JSON/JSONL or debugger-extracted evidence;
- boundary definition and signed comparison convention;
- explicit limitations.

Do not replace a controlled or physical snapshot with a GitHub-hosted run merely because the latter is newer. Freshness does not make an uncontrolled machine a better reference.

## What these results do not claim

The current profiling evidence does not establish:

- cycle-accurate SpaceWire simulation;
- FPGA/IP-core latency;
- SpaceWire codec or PHY timing;
- Data-Strobe electrical timing;
- cable/link end-to-end latency or jitter;
- router forwarding latency;
- formal ECSS performance qualification.

Those require the corresponding FPGA or physical HIL layer. When that hardware exists, report its clock-cycle and real-time metrics separately rather than folding them into the software tables above.
