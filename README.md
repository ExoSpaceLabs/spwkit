# SpWKit

**SpaceWire Development & Integration Toolkit**

SpWKit provides one portable software-facing SpaceWire API across simulation, embedded systems, Linux drivers and future hardware-backed implementations.

```text
Application
    |
    v
libspwkit public C ABI
    |
    +--> loopback reference backend
    +--> process-local SpaceWire simulator
    +--> future /dev/spwX backend
    +--> future Ethernet virtual backend
    +--> future bare-metal / RTOS backend
    +--> future FPGA / vendor backend
```

The design goal is simple: application SpaceWire logic should not change just because the transport underneath moves from a simulator to a DMA-capable FPGA implementation.

## v0.1 status

The v0.1 software baseline currently includes:

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

## Virtual SpaceWire

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

Distributed IPC/Ethernet virtual links and `/dev/vspwX` are later roadmap work.

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

A simulator may emulate this with host memory. A future FPGA backend may map the same operations to coherent/pinned memory and descriptor rings internally. Physical addresses, AXI descriptors, Linux `dma_addr_t` and vendor handles do not appear in the portable ABI.

Scatter/gather is deferred beyond v0.1; one buffer currently represents one contiguous packet.

## Build

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_SIMULATOR=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Install:

```sh
cmake --install build --prefix /path/to/spwkit-install
```

Consumer CMake:

```cmake
find_package(SpWKit 0.1 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE SpWKit::spwkit)
```

CI builds a standalone installed-package consumer so exported package metadata cannot silently rot.

## Examples

- `examples/c_loopback.c`: hosted C API, capabilities, packets, EEP and time codes;
- `examples/cpp_no_heap.cpp`: C++ application using caller-owned workspace;
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
- [Testing strategy](docs/testing.md)
- [Architecture](docs/architecture.md)
- [ECSS scope and compliance policy](docs/compliance.md)
- [Roadmap](docs/roadmap.md)

## Licensing

SpWKit software is licensed under the Apache License 2.0. A future commercial FPGA/ASIC SpaceWire implementation may remain separate while implementing the same application-facing contract.

See [LICENSE](LICENSE), [NOTICE](NOTICE) and [CONTRIBUTING.md](CONTRIBUTING.md).
