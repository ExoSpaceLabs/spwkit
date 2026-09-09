# Profiling goals and achieved results

This page records **why SpWKit is profiled, what the profiling campaign established, and what changed as a result**. The companion [`profiling.md`](profiling.md) remains the canonical measurement/methodology contract.

The numbers below are engineering reference snapshots for the named host or board. They are not universal performance specifications and must not be mixed across platforms without preserving the original measurement domain and boundary.

## Why we are doing this

SpWKit is intended to sit between application code and multiple communication backends: simulator, virtual transports, Linux devices, embedded drivers, FPGA/controller integrations, and eventually real SpaceWire hardware. That abstraction is useful only if its cost and behavior are understood rather than assumed.

The profiling work therefore exists to answer five practical questions:

1. **Is the SpWKit API layer itself expensive?**
   We need to separate the public abstraction cost from provider, DMA, cache, socket, fragmentation and physical-link work.
2. **When is zero-copy actually beneficial?**
   Zero-copy adds ownership, synchronization, acquire/reclaim and release bookkeeping. Its crossover must be measured instead of presumed.
3. **Where should optimization effort go?**
   A measurable bottleneck should be decomposed before production code is changed. The VSPW-TP reassembly work is the concrete example.
4. **Can future hardware backends be compared fairly?**
   The hosted and STM32 campaigns establish a repeatable reference methodology for FPGA, ASIC, USB-adapter and physical SpaceWire work.
5. **Can regressions be detected?**
   CI validates benchmark mechanics and schemas, while controlled-host and physical-board runs provide reference snapshots that future changes can compare against.

The central engineering rule remains: **measure the boundary that the number actually represents**. Host TSC ticks, Cortex-M7 cycles, FPGA cycles and physical link timing are different evidence domains.

## What we established

### 1. The basic SpWKit DRIVER abstraction is thin

Controlled i7-1355U reference:

```text
host-20260909T000756Z-f79b56ff
commit f79b56ffab499ef5e0748ec9e32045d6023eb3f9
```

Paired copied DRIVER `SpWKit - native` median deltas:

| Payload | TX | RX |
|---:|---:|---:|
| 0 B | +4 TSC ticks | -1 TSC tick |
| 1 B | +3 | -1 |
| 8 B | +3 | 0 |
| 64 B | +4 | 0 |
| 256 B | +4 | 0 |
| 1024 B | +4 | 0 |
| 4096 B | +4 | +2 |

**Conclusion:** on the controlled host, the ordinary copied-path API/backend abstraction adds about **3-4 invariant-TSC ticks on TX** and effectively **no measurable RX overhead**. Provider, transport and copy mechanics dominate well before the API layer does.

### 2. Zero-copy has platform- and lifecycle-dependent crossover points

Controlled i7-1355U copied versus zero-copy medians:

| Payload | copied TX | ZC TX preparation | copied RX visibility | ZC RX visibility |
|---:|---:|---:|---:|---:|
| 0 B | 48 | 203 | 42 | 75 |
| 64 B | 109 | 253 | 57 | 75 |
| 256 B | 246 | 386 | 60 | 75 |
| 1024 B | 813 | 931 | 87 | 75 |
| 4096 B | 3189 | 3106 | 291 | 75 |

**Observed crossover:**

- TX zero-copy first becomes beneficial at 4096 B in the tested sweep, placing the crossover between **1024 and 4096 B**.
- RX zero-copy crosses between **256 and 1024 B** and is strongly beneficial at 4096 B.

This established that “zero-copy is faster” is not a useful blanket statement. Ownership work must be included according to the actual application lifecycle.

### 3. STM32H755 physical DMA-provider behavior is characterized

Physical reference:

```text
stm32h755-20260909T121009Z-e49d70a3
commit e49d70a3d6fa34994de2c6e64e50d03a8016fe1f
```

NUCLEO-H755ZI-Q, Cortex-M7 @ 64 MHz, DWT CYCCNT, 16 warmup / 64 measured iterations. Counter floor was exactly 1 cycle and was not subtracted.

| Payload | copied TX | ZC TX submit | copied RX | ZC RX acquire |
|---:|---:|---:|---:|---:|
| 0 B | 47 | 156 | 74 | 284 |
| 1 B | 251 | 300 | 210 | 201 |
| 8 B | 272 | 300 | 242 | 201 |
| 64 B | 312.5 | 320 | 285 | 208 |
| 256 B | 541.5 | 446 | 536 | 250 |

Key physical conclusions:

- already-acquired-buffer TX zero-copy submit crosses between **64 and 256 B**;
- including TX acquire (~141 cycles), there is no complete pre-submit crossover within the 256 B fixture capacity;
- RX visibility is cheaper with zero-copy from 1 B upward in this fixture;
- including RX release (~190 cycles), the full RX ownership lifecycle crosses between **64 and 256 B**;
- isolated cache clean/invalidate costs are a substantial part of the copied TX path at larger payloads.

This fixture measures a real DMA2 memory-to-memory provider. It does **not** measure SpaceWire controller, codec, PHY or cable timing.

### 4. Cortex-M7 direct/native differential confirmed the abstraction cost is small

Reference:

```text
stm32h755-native-20260909T125024Z-6804ebbb
commit 6804ebbb6529d41c4cbed5d99b729428501e70fa
```

Paired median `SpWKit - native` deltas in Cortex-M7 cycles:

| Payload | copied TX | copied RX | ZC TX submit | ZC RX acquire |
|---:|---:|---:|---:|---:|
| 0 B | +8.5 | +19 | +97.5 | +223.5 |
| 1 B | +18 | +35 | +93 | +168.5 |
| 8 B | +24 | +38 | +93 | +168.5 |
| 64 B | +20.5 | +32.5 | +93 | +166 |
| 256 B | +20.5 | +34 | +93 | +211.5 |

At 64 MHz, the copied-path SpWKit differential is roughly **0.13-0.59 microseconds**. The larger fixed software cost is concentrated in zero-copy ownership plumbing rather than ordinary copied packet dispatch.

### 5. Lifecycle costs are known and are not a performance concern

Controlled lifecycle reference, i7-1355U:

| Operation | Median TSC ticks | p95 | p99 |
|---|---:|---:|---:|
| `open_heap` | 1618 | 1657 | 2405 |
| `open_in_place` | 1496 | 1503 | 1507 |
| `start` | 51 | 56 | 58 |
| `stop` | 44 | 47 | 48 |
| `reset` | 1421 | 1425 | 1427 |
| `close_in_place` | 45 | 48 | 52 |
| `close_heap` | 85 | 90 | 95 |

Counter floor median/p95/p99 was 33/35/36 ticks.

`start`, `stop` and caller-owned close are very thin. `reset` is larger because LOOPBACK clears queue state. Heap-backed open/close show allocator effects, as expected. No lifecycle optimization was justified by these measurements.

## VSPW-TP: profiling found and removed a real bottleneck

The virtual UDP backend originally showed a sharp RX cost increase once payloads exceeded the 1200-byte fragmentation threshold. Instead of optimizing the first suspicious function, the path was decomposed into socket, polling, header, ACK, copy and reassembly stages.

### Controlled attribution baseline

Reference:

```text
udp-breakdown-20260909T163912Z-0b21a649
commit 0b21a649709694d57f3cf390b0130de242870f99
```

Host: Intel Core i7-10850H, Linux 6.8.0-136-generic, GCC 11.4, CPU 0, 2 s preconditioning, 256 warmup / 1024 measured iterations. Counter floor median/p95/p99 was 34/36/37 TSC ticks.

At 4096 B / four fragments:

| Diagnostic component | Median TSC ticks |
|---|---:|
| `reassembly_push` | **28,847** |
| `reassembly_delivery` | **36,814** |
| inactive 1 MiB-capacity bitmap reset | 7,414 |
| ACK encode/validate/send | 7,016 |
| `poll(POLLIN) + recvfrom` | 3,278 |
| two ordinary payload copies | **244** |
| header encode | 68 |
| header decode | 75 |

The measurement showed that the main 4 KiB cliff was **not memcpy**. It was the byte-granular coverage bookkeeping in fragment reassembly, with additional fixed cost from ACK and socket readiness handling.

### Optimization

PR #202 / issue #201 replaced the normal non-overlapping fragment path with:

```text
64-bit coverage-range check
        -> one payload memcpy
        -> 64-bit coverage-range mark
```

The original byte-granular path remains for duplicates, partial overlaps and conflicts. Active-packet reset clears only coverage words relevant to that packet; conservative inactive reset still clears the full coverage storage.

Merged result:

```text
7a6f072fecea1fb851330d189bce6b06dd9a6807
```

### Controlled component result after optimization

Reference:

```text
udp-breakdown-20260909T180527Z-eb383077
tested head eb3830772dcebde4e7f6b5dd5e9b9ae6e413ee84
```

Same i7-10850H host and measurement configuration. Counter floor median/p95/p99 was 33/35/36 ticks.

| Payload | Stage | Before | After | Reduction |
|---:|---|---:|---:|---:|
| 1201 B | reassembly push | 13,078 | 321 | **97.5%** |
| 1201 B | complete reassembly/delivery | 15,749.5 | 466 | **97.0%** |
| 2400 B | reassembly push | 16,550 | 525 | **96.8%** |
| 2400 B | complete reassembly/delivery | 24,156 | 755 | **96.9%** |
| 4096 B | reassembly push | 28,847 | 900 | **96.9%** |
| 4096 B | complete reassembly/delivery | 36,814 | 1,302 | **96.5%** |

### Production VSPW-TP RX result

The unchanged production-vs-native UDP comparison was then rerun before and after the optimization on the same i7-10850H host.

Paired median `SpWKit - native` RX overhead:

| Payload | Before | After | Reduction |
|---:|---:|---:|---:|
| 64 B | 12,478 | 12,111 | 2.9% |
| 1024 B | 12,991 | 12,434 | 4.3% |
| 1200 B | 12,996 | 12,630 | 2.8% |
| **1201 B** | **34,376.5** | **16,524** | **51.9%** |
| **2400 B** | **42,245** | **17,170** | **59.4%** |
| **4096 B** | **60,281** | **25,032** | **58.5%** |

At 4096 B, absolute SpWKit RX statistics changed from:

| Statistic | Before | After |
|---|---:|---:|
| median | 62,733 | 27,542.5 |
| p95 | 77,339 | 37,512 |
| p99 | 97,277 | 47,631 |

The native 4096 B medians remained close, 2452 before versus 2510.5 after. The production calibration floor differed between the two runs (117 versus 33 ticks), but the authoritative comparison is paired native/SpWKit within each run. In addition, the controlled component runs had near-identical calibration floors (34 versus 33 ticks) and independently showed a ~97% reassembly reduction. The production improvement is therefore accepted as real rather than a calibration artifact.

**Achievement:** the pathological fragmentation/reassembly cost was removed without changing VSPW-TP wire semantics. Production 4 KiB RX overhead fell by about **58.5%**, while unfragmented payloads remained essentially unchanged.

## What remains deliberately outside these results

These measurements still do not claim:

- cycle-accurate SpaceWire simulation;
- FPGA/IP-core latency;
- SpaceWire controller or codec timing;
- Data-Strobe PHY/electrical timing;
- cable/link end-to-end latency and jitter;
- router forwarding latency;
- formal ECSS performance qualification.

Those belong to future FPGA/controller and physical HIL campaigns. The software profiling work gives those future measurements a clean baseline: when hardware is introduced, we can distinguish hardware cost from an already-characterized software stack instead of measuring one opaque end-to-end number.

## Overall result

The profiling campaign achieved more than a set of benchmark tables:

- it demonstrated that the **basic SpWKit abstraction is thin** on both controlled x86 and Cortex-M7;
- it established **measured zero-copy crossover behavior** rather than assuming zero-copy always wins;
- it separated cache/DMA/provider work from API abstraction cost on real STM32 hardware;
- it characterized lifecycle operations and found no meaningful lifecycle bottleneck;
- it identified the dominant VSPW-TP fragmentation cost quantitatively;
- it produced a production optimization that reduced **4 KiB VSPW-TP RX overhead by ~58.5%** and the reassembly component by **~96-97%**;
- it left behind reproducible scripts, schemas, CI mechanics and controlled reference snapshots for future FPGA/ASIC/physical SpaceWire development.

That is the purpose of the profiling infrastructure: **make performance decisions from measured boundaries, preserve comparable evidence, and ensure future hardware integration starts from a software stack whose own costs are already understood.**
