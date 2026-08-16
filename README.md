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

### v0.2.0 — Distributed virtual SpaceWire

`v0.2.0` is the distributed virtual SpaceWire release and includes:

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
- reusable shared public backend contract plus distributed peer-loss/restart contract;
- installed-package equal-peer example for independent processes/hosts;
- active D2D CI exercising localhost processes and two Linux network namespaces in addition to transport/recovery/timing/fault tests;
- VSPW-TP Wireshark/tshark capture tooling with deterministic real-dissector validation;
- explicit v0.2 platform policy and installed-package UDP runtime metadata.

The v0.2 engineering scope is complete. Native Winsock UDP transport is intentionally deferred beyond this release and tracked separately in issue #42.

### v0.3.0 development line — C11 runtime

The v0.3 engineering line converts the runtime implementation itself to C11 before Linux-device, RTOS and hardware backends expand the surface. `SpWKit::spwkit` now builds without a C++ compiler/runtime; `SPWKIT_ENABLE_CPP=ON` optionally installs a header-only C++17 `SpWKit::cpp` convenience target above the same C API. Static and shared builds use the standard `BUILD_SHARED_LIBS` switch. No `v0.3.0` tag is implied by the development version.

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

The distributed backend runs the reusable public backend contract and is also exercised as genuinely separate applications. The D2D gate installs SpWKit, builds `examples/distributed` through `find_package(SpWKit)`, launches two peer processes, verifies peer loss/restart, and repeats the same 8 KiB EOP/EEP plus time-code exchange in two Linux network namespaces connected by a 1500-byte-MTU veth link. The applications never call VSPW-TP or socket-private APIs.

Wire inspection is available separately under `tools/wireshark`: the Lua dissector recognizes VSPW-TP on configurable UDP ports, decodes v1 header/control fields, and is validated against a deterministic generated PCAP through tshark. This tooling is not linked into `libspwkit` and adds no runtime dependency.

### v0.2 hosted platform scope

The v0.2 UDP runtime is **POSIX-only**. Linux is the primary fully exercised distributed platform; macOS is supported as a second POSIX host through the host/shared UDP contract matrix. Windows remains supported for the portable core API, simulator and installed package, but the UDP runtime backend is not implemented there in v0.2.

Windows still installs `spwkit/udp.h` and exposes `SPW_BACKEND_UDP`/`spw_udp_config_t`. Selecting that backend with a structurally valid configuration returns `SPW_ERR_UNSUPPORTED`, rather than changing the public API by platform. The generated CMake package exports `SpWKit_UDP_RUNTIME_SUPPORTED` so hosted consumers can gate runtime-specific examples while still handling `SPW_ERR_UNSUPPORTED` at the API boundary.

Native Winsock support is deferred to a later portability slice behind the same backend contract. See `docs/platform-support.md` for the exact validation matrix and rationale.

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

## Language and error policy

`libspwkit` is implemented in C11 and reports recoverable failures through `spw_result_t`. It does not require a C++ compiler, linker or runtime. The optional C++17 wrapper is exception-free, remains move/RAII convenience only, and delegates to the same C API.

See [C and C++ integration](docs/language-bindings.md) for C-only, wrapper-enabled, static/shared and embedded build profiles.

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
  -DSPWKIT_BUILD_UDP=ON \
  -DSPWKIT_ENABLE_CPP=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Install:

```sh
cmake --install build --prefix /path/to/spwkit-install
```

Consumer CMake for the v0.3 development line:

```cmake
find_package(SpWKit 0.3 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE SpWKit::spwkit)

if(SpWKit_UDP_RUNTIME_SUPPORTED)
    # This installed library contains the hosted VSPW-TP/UDP backend.
endif()
```

Consumers pinned to the `v0.1.0` tag should request `SpWKit 0.1` instead.

CI builds standalone installed-package consumers so exported package metadata cannot silently rot.

## Examples

- `examples/c_loopback.c`: hosted C API, capabilities, packets, EEP and time codes;
- `examples/cpp_no_heap.cpp`: C++ application using caller-owned workspace;
- `examples/c_simulator_zero_copy.c`: paired simulator endpoints using zero-copy ownership;
- `examples/installed`: standalone **C-only** installed-package consumer that verifies package/runtime metadata without enabling C++;
- `examples/installed_cpp`: optional C++17 wrapper consumer built against `SpWKit::cpp`;
- `examples/distributed`: standalone installed-package VSPW-TP/UDP equal peer for two processes or Linux hosts, including restart scenarios.

All examples use public headers only and require no physical SpaceWire hardware.

## Standards scope

The primary design reference is **ECSS-E-ST-50-12C Rev.1, SpaceWire - Links, nodes, routers and networks (15 May 2019)**. Related ECSS SpaceWire protocol standards include protocol identification, RMAP and CCSDS packet transfer.

SpWKit uses these standards as design references. The project does **not** claim formal ECSS conformance or certification until implemented behavior is backed by explicit requirements traceability and verification evidence.

## Documentation

- [Getting started](docs/getting-started.md)
- [Public API contract](docs/api.md)
- [Core public types](docs/types.md)
- [Port/backend configuration](docs/configuration.md)
- [v0.2 platform support](docs/platform-support.md)
- [Memory ownership](docs/memory.md)
- [Zero-copy buffers](docs/buffers.md)
- [C and C++ integration](docs/language-bindings.md)
- [Portability contract](docs/portability.md)
- [Backend contract](docs/backend-contract.md)
- [Local virtual simulator](docs/simulator.md)
- [Distributed VSPW-TP transport](docs/vspw-tp.md)
- [VSPW-TP capture and Wireshark tooling](tools/wireshark/README.md)
- [Testing strategy](docs/testing.md)
- [Architecture](docs/architecture.md)
- [ECSS scope and compliance policy](docs/compliance.md)
- [Roadmap](docs/roadmap.md)

## Licensing

SpWKit software is licensed under the Apache License 2.0. A future commercial FPGA/ASIC SpaceWire implementation may remain separate while implementing the same application-facing contract.

See [LICENSE](LICENSE), [NOTICE](NOTICE) and [CONTRIBUTING.md](CONTRIBUTING.md).
