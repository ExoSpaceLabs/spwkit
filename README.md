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
    +--> Linux virtual-device backend -> VSPD -> vspwd
    +--> future bare-metal / RTOS backend
    +--> future FPGA / vendor backend
```

The design goal is simple: application SpaceWire logic should not change just because the implementation underneath moves from a simulator to UDP transport, a Linux device, or a DMA-capable FPGA implementation.

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
- explicit v0.2 platform policy and installed package UDP runtime metadata.

Native Winsock UDP transport remains tracked separately in issue #42.

### v0.3.0 — C11 runtime and optional C++ wrapper

`v0.3.0` is the C-first runtime release. `spwkit::spwkit` is a C11 runtime and builds without a C++ compiler/linker/runtime. `SPWKIT_ENABLE_CPP=ON` optionally installs a header-only C++17 `spwkit::cpp` convenience target above the same C API. Static and shared builds use the standard `BUILD_SHARED_LIBS` switch.

The release adds pure-C behavioral CI, C-only static/shared installed consumers, a genuinely C-only distributed example, a real freestanding/no-heap portability build, lowercase imported targets, and repository hygiene checks for stale pre-C11 integration patterns.

The C API is authoritative. C++ does not provide a second implementation or different SpaceWire semantics.

### v0.4.0 — Linux virtual device and userspace service — release candidate

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

The process-local simulator supports bidirectional/full-duplex packets, EOP/EEP, time codes, bounded queues, immediate/finite/infinite waits, link start/stop/reset, disconnect/recovery, statistics and zero-copy ownership emulation. The simulator itself is implemented in C and is exercised by a pure-C behavioral test/profile with `CXX=/bin/false`.

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

The distributed backend runs the reusable public backend contract and is also exercised as genuinely separate applications. The D2D gate installs SpWKit, builds the **C-only** `examples/distributed` through `find_package(SpWKit)` with no C++ compiler, launches two peer processes, verifies peer loss/restart, and repeats the same 8 KiB EOP/EEP plus time-code exchange in two Linux network namespaces connected by a 1500-byte-MTU veth link. The applications never call VSPW-TP or socket-private APIs.

Wire inspection is available separately under `tools/wireshark`: the Lua dissector recognizes VSPW-TP on configurable UDP ports, decodes v1 header/control fields, and is validated against a deterministic generated PCAP through tshark. This tooling is not linked into `libspwkit` and adds no runtime dependency.

### Linux virtual-device service

The v0.4 device path uses the pure-C public backend and the private VSPD control/data protocol:

```text
C application -----------------------------+
                                           |
C++ application -> optional spwkit::Port --+
                                           v
                                      spw_port_*
                                           |
                                  SPW_BACKEND_DEVICE
                                           |
                                          VSPD
                                           |
                                         vspwd
                                           |
                               virtual port 0 <-> virtual port 1
```

The daemon is opt-in with `SPWKIT_BUILD_VSPWD=ON` and remains separate from the portable library core. The hosted client backend is controlled independently by `SPWKIT_BUILD_DEVICE` and is Linux-only in the current v0.4 implementation.

The public `spw_device_config_t` contains only a daemon `port_id` and bounded endpoint path. Unix file descriptors, `sockaddr_un`, VSPD frames and daemon-private state remain implementation details.

The device path preserves logical packet boundaries, EOP/EEP, zero-length packets, time codes, receive-too-small retry, statistics, peer loss and fresh-process restart recovery. It also advertises `SPW_CAP_READINESS`: `spw_port_wait()` can wait for packet/time-code receive readiness without consuming the pending event and without exposing a file descriptor or Linux polling type. CI runs the full path using pure C with `CXX=/bin/false`, plus the optional C++ wrapper over the exact same backend.

See `docs/vspw-device-protocol.md` for the VSPD wire contract and `docs/vspwd.md` for daemon/backend behavior and CI evidence.

### Hosted platform scope

The UDP runtime is currently **POSIX-only**. Linux is the primary fully exercised distributed platform; macOS is supported as a second POSIX host through the host/shared UDP contract matrix. Windows remains supported for the portable C core API, simulator and installed package, but the UDP runtime backend is not implemented there yet.

Windows still installs `spwkit/udp.h` and exposes `SPW_BACKEND_UDP`/`spw_udp_config_t`. Selecting that backend with a structurally valid configuration returns `SPW_ERR_UNSUPPORTED`, rather than changing the public API by platform. The generated CMake package exports `SpWKit_UDP_RUNTIME_SUPPORTED` so hosted consumers can gate runtime-specific examples while still handling `SPW_ERR_UNSUPPORTED` at the API boundary.

The Linux device headers and `SPW_BACKEND_DEVICE` are likewise source-visible independently of runtime support. Installed packages expose `SpWKit_DEVICE_RUNTIME_SUPPORTED`; the current implementation scope is Linux.

Native Winsock support is deferred behind the same UDP backend contract. See `docs/platform-support.md` and issue #42.

The Linux virtual-device/userspace-service milestone is tracked by #54.

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

Pure-C runtime/tests/examples:

```sh
CC=gcc CXX=/bin/false cmake -S . -B build-c \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_CPP_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_SIMULATOR=ON \
  -DSPWKIT_BUILD_UDP=ON \
  -DSPWKIT_ENABLE_CPP=OFF
cmake --build build-c --parallel
ctest --test-dir build-c --output-on-failure
```

C plus optional C++ wrapper/development tests:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_CPP_TESTS=ON \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_CPP_EXAMPLES=ON \
  -DSPWKIT_BUILD_SIMULATOR=ON \
  -DSPWKIT_BUILD_UDP=ON \
  -DSPWKIT_ENABLE_CPP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Linux public virtual-device profile:

```sh
CC=gcc CXX=/bin/false cmake -S . -B build-device \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_CPP_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_DEVICE=ON \
  -DSPWKIT_BUILD_VSPWD=ON \
  -DSPWKIT_ENABLE_CPP=OFF
cmake --build build-device --parallel
ctest --test-dir build-device -L device --output-on-failure
```

Install:

```sh
cmake --install build --prefix /path/to/spwkit-install
```

Consumer CMake for v0.4:

```cmake
find_package(SpWKit 0.4 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE spwkit::spwkit)

if(SpWKit_UDP_RUNTIME_SUPPORTED)
    # This installed library contains the hosted VSPW-TP/UDP backend.
endif()

if(SpWKit_DEVICE_RUNTIME_SUPPORTED)
    # This installed library contains the Linux VSPD/vspwd client backend.
endif()
```

Consumers pinned to the `v0.3.0` release should request `SpWKit 0.3` instead.

Optional C++ consumers link `spwkit::cpp` when the package was built with `SPWKIT_ENABLE_CPP=ON`.

CI builds standalone installed-package consumers so exported package metadata cannot silently rot.

## Examples

- `examples/c_loopback.c`: hosted C API, capabilities, packets, EEP and time codes;
- `examples/c_simulator_zero_copy.c`: paired pure-C simulator endpoints using zero-copy ownership;
- `examples/c_device_peer.c`: two-process pure-C `SPW_BACKEND_DEVICE` exchange through `vspwd`;
- `examples/cpp_no_heap.cpp`: C++ source using the authoritative C API with caller-owned workspace;
- `examples/cpp_wrapper_loopback.cpp`: optional `spwkit::Port` RAII wrapper over the same loopback runtime;
- `examples/cpp_device_peer.cpp`: optional `spwkit::Port` device peer using the same `SPW_BACKEND_DEVICE` runtime;
- `examples/installed`: standalone **C-only** installed-package consumer that verifies package/runtime metadata without enabling C++;
- `examples/installed_cpp`: optional C++17 wrapper consumer built against `spwkit::cpp`;
- `examples/distributed`: standalone **C-only** installed-package VSPW-TP/UDP equal peer for two processes or Linux hosts, including restart scenarios;
- `examples/installed_device`: standalone C11 installed-package Linux device consumer;
- `examples/installed_device_cpp`: standalone optional C++17 wrapper device consumer.

The device C and C++ examples are executed as real peer processes against `vspwd` in the Virtual device workflow. Raw VSPD clients remain test infrastructure and are not public examples.

All public examples use public headers only and require no physical SpaceWire hardware.

## Standards scope

The primary design reference is **ECSS-E-ST-50-12C Rev.1, SpaceWire - Links, nodes, routers and networks (15 May 2019)**. Related ECSS SpaceWire protocol standards include protocol identification, RMAP and CCSDS packet transfer.

SpWKit uses these standards as design references. The project does **not** claim formal ECSS conformance or certification until implemented behavior is backed by explicit requirements traceability and verification evidence.

## Documentation

- [Getting started](docs/getting-started.md)
- [Public API contract](docs/api.md)
- [Core public types](docs/types.md)
- [Port/backend configuration](docs/configuration.md)
- [Hosted platform support](docs/platform-support.md)
- [Memory ownership](docs/memory.md)
- [Zero-copy buffers](docs/buffers.md)
- [C and C++ integration](docs/language-bindings.md)
- [Portability contract](docs/portability.md)
- [Backend contract](docs/backend-contract.md)
- [Local virtual simulator](docs/simulator.md)
- [Distributed VSPW-TP transport](docs/vspw-tp.md)
- [Linux VSPD device protocol](docs/vspw-device-protocol.md)
- [`vspwd` userspace virtual-device service](docs/vspwd.md)
- [`spwmon` passive daemon observation](docs/spwmon.md)
- [Installed-package Linux device examples](docs/installed-device-examples.md)
- [CUSE `/dev/vspwX` feasibility](docs/cuse-feasibility.md)
- [VSPW-TP capture and Wireshark tooling](tools/wireshark/README.md)
- [Testing strategy](docs/testing.md)
- [Architecture](docs/architecture.md)
- [ECSS scope and compliance policy](docs/compliance.md)
- [Roadmap](docs/roadmap.md)

## Licensing

SpWKit software is licensed under the Apache License 2.0. A future commercial FPGA/ASIC SpaceWire implementation may remain separate while implementing the same application-facing contract.

See [LICENSE](LICENSE), [NOTICE](NOTICE) and [CONTRIBUTING.md](CONTRIBUTING.md).
