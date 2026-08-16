# SpWKit

**SpaceWire Development & Integration Toolkit**

SpWKit provides one portable software-facing SpaceWire API across simulation, distributed virtual links, embedded systems, Linux drivers and future hardware-backed implementations.

```text
Application
    |
    v
libspwkit public C ABI
    |
    +--> loopback reference backend
    +--> process-local SpaceWire simulator
    +--> VSPW-TP / UDP distributed backend
    +--> future /dev/spwX backend
    +--> future bare-metal / RTOS backend
    +--> future FPGA / vendor backend
```

The design goal is simple: application SpaceWire logic should not change just because the implementation underneath moves from a simulator to UDP transport or a DMA-capable FPGA implementation.

## Release status

### v0.1.0 baseline

`v0.1.0` is tagged at the completed portable-core boundary. It includes:

- stable-in-v0.1 public C ABI and opaque port handles;
- packet send/receive with EOP and EEP preservation;
- explicit link state and lifecycle control;
- time codes;
- capabilities and statistics;
- deterministic loopback backend;
- process-local two-peer virtual SpaceWire simulator;
- reusable backend contract tests;
- caller-owned no-heap port construction;
- optional zero-copy ownership semantics;
- CMake install/export and `find_package(SpWKit)` support;
- C and C++ public examples;
- cross-platform CI, sanitizers, no-heap verification and simulator CI.

Physical FPGA/HIL verification is deliberately deferred until suitable hardware is available. The same backend contract suite is intended to be reused when a physical backend exists.

### Current `main`: v0.2 development

Development has moved to package version 0.2.0 and the distributed virtual SpaceWire backend now includes:

- versioned VSPW-TP v1 framing with a fixed 40-byte header carrying a 64-bit sender session ID;
- POSIX IPv4 UDP backend selected through the normal `spw_port_*` API;
- bounded fragmentation plus arbitrary-order reassembly with exact duplicate/overlap handling;
- EOP/EEP and time-code transport;
- deterministic 1 MiB backend packet/reassembly/reliable-TX bound;
- default 1200-byte UDP fragment payload;
- logical-message ACK and bounded complete-message retransmission;
- duplicate logical-message suppression;
- per-frame session identity, keepalive/peer timeout and restart recovery;
- configured source address/port validation;
- deterministic configurable SpaceWire-side virtual link rate and fixed latency;
- deterministic seeded transport drop/duplicate/reorder/delay injection;
- explicit SpaceWire-side EEP injection with transport/SpaceWire fault-domain counters;
- active device-to-device CI exercising real UDP transfer, recovery, timing and fault behavior.

Stronger multi-process/container examples, broader distributed contract coverage and capture/Wireshark tooling remain v0.2 work.

## Virtual SpaceWire

### Process-local simulator

Two simulator ports with the same `link_id` and opposite A/B endpoint identifiers form equal SpaceWire peers:

```text
Application A                               Application B
     |                                           |
 libspwkit                                   libspwkit
 endpoint A <========== virtual link =========> endpoint B
                         link_id
```

A/B are pairing labels, not server/client roles.

The process-local simulator supports bidirectional/full-duplex packets, EOP/EEP, time codes, bounded queues, immediate/finite/infinite waits, link start/stop/reset, disconnect/recovery, statistics and zero-copy ownership emulation.

### Distributed UDP backend

Two applications can communicate over VSPW-TP/UDP while retaining the same public SpaceWire API:

```text
Application A                         Application B
     |                                    |
 libspwkit                            libspwkit
     |                                    |
 SPW_BACKEND_UDP                    SPW_BACKEND_UDP
     |                                    |
     +--------- VSPW-TP / UDP ------------+
```

UDP is an internal transport. Packet boundaries, EOP/EEP and time codes remain SpaceWire-facing semantics and are not replaced by datagram boundaries.

Reliability is logical-message based. The backend retains at most one unacknowledged DATA/TIME_CODE event, retries it cooperatively after the configured ACK timeout, suppresses duplicate logical delivery, and uses transport keepalives/session IDs for peer liveness and restart recovery. No mandatory background thread is required.

The optional virtual timing model adds deterministic SpaceWire-side serialization and fixed latency to logical DATA/TIME_CODE events without treating incidental host UDP delay as simulated link timing. ACKs, keepalives and retransmissions remain transport mechanics and do not reapply the logical SpaceWire delay.

The UDP backend also supports fixed-size seeded fault rules for transport drop, duplicate, adjacent reorder and delay. These operate on VSPW-TP carrier datagrams and remain distinct from explicit SpaceWire-side EEP injection. Ordinary transport loss or reordering never synthesizes EEP. `spw_port_get_fault_statistics()` exposes separate counters for the two fault domains.

`/dev/vspwX` remains later roadmap work.

## Portable memory model

Heap allocation is optional.

Hosted applications may use:

```c
spw_port_open(&config, &port);
```

Bare-metal/RTOS-oriented code can use caller-owned storage:

```c
spw_port_workspace_requirements_t req;
spw_port_workspace_requirements(&config, &req);
spw_port_open_in_place(&config, workspace, workspace_size, &port);
```

`SPWKIT_ENABLE_HEAP=OFF` disables heap-backed open while preserving the in-place path.

## No-throw policy

The `spwkit` library target is built with C++ exceptions and RTTI disabled on supported toolchains. Recoverable failures are reported through `spw_result_t`; library code does not depend on exception handling.

This is a project-wide portability property, not just a special no-heap CI configuration.

## Zero-copy / future DMA

The optional zero-copy API models ownership rather than hardware descriptors:

```text
TX: acquire -> fill -> submit -> backend owns -> reclaim -> reuse/release
RX: backend receives -> acquire -> inspect -> release
```

The local simulator emulates this with host memory. A future FPGA backend may map the same operations to coherent/pinned memory and descriptor rings internally. Physical addresses, AXI descriptors, Linux `dma_addr_t` and vendor handles do not appear in the portable ABI.

Scatter/gather is deferred beyond v0.1; one buffer currently represents one contiguous packet.

## Build

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_SIMULATOR=ON \
  -DSPWKIT_BUILD_UDP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Install:

```sh
cmake --install build --prefix /path/to/spwkit-install
```

Consumer CMake for current v0.2 development:

```cmake
find_package(SpWKit 0.2 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE SpWKit::spwkit)
```

Consumers pinned to the `v0.1.0` tag should request `SpWKit 0.1` instead.

CI builds a standalone installed-package consumer so exported package metadata cannot silently rot.

## Examples

- `examples/c_loopback.c`: hosted C API, capabilities, packets, EEP and time codes;
- `examples/cpp_no_heap.cpp`: C++ application using caller-owned workspace;
- `examples/c_simulator_zero_copy.c`: paired simulator endpoints using zero-copy ownership;
- `examples/installed`: standalone installed-package consumer.

All examples use public headers only and require no physical hardware.

## Standards scope

The primary design reference is **ECSS-E-ST-50-12C Rev.1, SpaceWire - Links, nodes, routers and networks (15 May 2019)**. Related ECSS SpaceWire protocol standards include protocol identification, RMAP and CCSDS packet transfer.

SpWKit uses these standards as design references. The project does **not** claim formal ECSS conformance or certification until implemented behavior is backed by explicit requirements traceability and verification evidence.

## Documentation

- [Getting started](docs/getting-started.md)
- [Public API contract](docs/api.md)
- [Core public types](docs/types.md)
- [Port/backend configuration](docs/configuration.md)
- [Memory ownership](docs/memory.md)
- [Zero-copy buffers](docs/buffers.md)
- [No-throw and portability contract](docs/portability.md)
- [Backend contract](docs/backend-contract.md)
- [Local virtual simulator](docs/simulator.md)
- [Distributed VSPW-TP transport](docs/vspw-tp.md)
- [Testing strategy](docs/testing.md)
- [Architecture](docs/architecture.md)
- [ECSS scope and compliance policy](docs/compliance.md)
- [Roadmap](docs/roadmap.md)

## Licensing

SpWKit software is licensed under the Apache License 2.0. A future commercial FPGA/ASIC SpaceWire implementation may remain separate while implementing the same application-facing contract.

See [LICENSE](LICENSE), [NOTICE](NOTICE) and [CONTRIBUTING.md](CONTRIBUTING.md).
