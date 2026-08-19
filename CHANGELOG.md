# Changelog

Notable user-visible changes are recorded here. SpWKit follows semantic versioning for package releases while the public C ABI remains explicitly versioned through `SPWKIT_API_VERSION_*`.

## v0.4.0 — 2026-08-19

Linux virtual-device and userspace-service release. The public C API remains authoritative; VSPD, Unix sockets, CUSE and daemon-management protocol details remain private implementation layers.

### Added

- private VSPD v1.3 backend↔daemon protocol with a fixed 40-byte network-order header, bounded 32 KiB records and 1 MiB logical DATA fragmentation/reassembly;
- Linux `SPW_BACKEND_DEVICE` selected through the normal `spw_port_*` API, including reconnect/reattach after daemon loss;
- pure-C `vspwd` userspace service with two deterministic virtual ports, lifecycle, packets, EOP/EEP, zero-length packets, time codes, statistics and restart recovery;
- backend-neutral `SPW_CAP_READINESS`, `SPW_READY_RX_PACKET`, `SPW_READY_RX_TIME_CODE` and non-consuming level-triggered `spw_port_wait()`;
- VSPD 1.1 HELLO-only non-owning management and installed pure-C `spwctl` (`list`, `show`, `stats`, `clear-stats`);
- VSPD 1.2 bounded passive subscriptions and installed pure-C `spwmon` with human and JSON Lines output;
- standalone installed-package C11 and optional C++17 Linux device consumers, including mixed C/C++ peer validation;
- standalone installed-package C++17 VSPW-TP/UDP distributed peer using only `spwkit::cpp`;
- CUSE/libfuse3 feasibility work with a private fixed-width packet-record prototype; the production presenter remains tracked separately in #78;
- VSPD 1.3 bridged-port metadata and an optional topology-owned `vspwd` endpoint backed by the existing VSPW-TP/UDP runtime;
- end-to-end device↔daemon↔VSPW-TP/UDP DATA/time-code exchange with remote peer loss and fresh-process restart recovery;
- two-container Docker Compose distributed-simulation topology using isolated network namespaces and installed-package C/C++ peers;
- reproducible Ubuntu 22.04+ Debian packages for `amd64` and `arm64`, including the shared runtime, public C/C++ integration surface, `vspwd`, `spwctl` and `spwmon`;
- multi-architecture GHCR runtime/toolbox image for `linux/amd64` and `linux/arm64`, published under `v0.4.0`, `0.4` and `latest` tags;
- release packaging of Apache-2.0 `LICENSE` and `NOTICE` metadata.

### Changed

- completed the C-first v0.3 architecture by keeping `vspwd`, the Linux device backend and all daemon tools pure C with no mandatory C++ runtime;
- extended the shared backend contract to the Linux device backend and documented distributed/service-specific queue and peer-loss timing semantics;
- kept `/dev/vspwX` CUSE presentation optional and outside `libspwkit`; no kernel module or libfuse dependency is introduced into ordinary builds;
- kept bridge transport reliability in the existing `SPW_BACKEND_UDP` implementation rather than creating a second VSPW-TP stack inside `vspwd`;
- clarified simulation boundaries: `SPW_BACKEND_SIMULATOR` is intentionally process-local, while VSPW-TP/UDP and DEVICE/VSPD provide process-isolated simulation paths;
- versioned the shared library with SONAME major `0`, while the optional C++ wrapper remains header-only and compiled by the consuming application;
- keyed precompiled Linux artifacts by userspace baseline and CPU architecture rather than GCC version because the compiled public runtime ABI is C11.

### Verification

- Linux device, daemon, management, monitoring and bridge profiles run under GCC and Clang with `CXX=/bin/false`;
- public device and daemon paths run under ASan+UBSan;
- standalone installed C and C++ device consumers exercise C↔C, C++↔C++, C↔C++ and C++↔C interoperability;
- installed VSPW-TP/UDP C and C++ peers exercise C↔C, C++↔C++, C↔C++ and C++↔C as independent processes with 8 KiB DATA, EOP/EEP, time codes, peer loss and fresh-session recovery;
- distributed isolation is validated both with Linux network namespaces/veth and with a two-container Docker Compose bridge topology; these remain software simulation evidence, not physical HIL;
- `amd64` and `arm64` DEBs are built for their target architecture, installed in clean Ubuntu 22.04 containers, smoke-tested with the installed tools and exported with SHA-256 sidecars;
- ARM package and runtime-image validation executes under QEMU rather than relying on metadata-only cross packaging;
- the multi-arch OCI runtime image is built for both `linux/amd64` and `linux/arm64` before the release can publish GHCR tags;
- the cross-platform package matrix, pure-C static/shared gates, simulator contract, freestanding portability checks and Wireshark/tshark validation remain release gates;
- CUSE feasibility is compile/API validated under GCC and Clang without claiming `/dev/cuse` runtime evidence when hosted runners do not expose it.

### Deferred beyond v0.4

- production event-driven CUSE `/dev/vspwX` presenter (#78);
- native Windows/Winsock VSPW-TP runtime (#42);
- physical FPGA/HIL backend and electrical interoperability evidence;
- additional hosted precompiled architectures beyond `amd64` and `arm64` until target-specific CI evidence is added;
- generic SpaceWire routing/topology management and router simulation.

The v0.4.0 software release boundary is finalized by the dated release-preparation commit; the `v0.4.0` tag is accepted only after the tag-triggered exact-ref release matrix passes. Binary artifacts are published only after that full matrix, including the binary-package gate, succeeds on the exact tag commit.

## v0.3.0 — 2026-08-16

C-first runtime and packaging architecture. The public C API remains authoritative while the implementation no longer requires a C++ toolchain.

### Changed

- converted port dispatch, workspace ownership and backend polymorphism to a C11 vtable/context model;
- converted loopback, process-local simulator, zero-copy simulator path, VSPW-TP codec, fragment reassembly, virtual timing, deterministic fault logic and POSIX UDP backend to C11;
- preserved the released VSPW-TP v1 wire format and v0.2 session/reliability semantics through the implementation-language conversion;
- exported installed/runtime targets as lowercase `spwkit::spwkit` and optional `spwkit::cpp`, while retaining `find_package(SpWKit)` as the package lookup name;
- separated pure-C tests/examples from optional C++ development fixtures so CTest can execute meaningful behavior with `CXX=/bin/false`;
- made the installed distributed VSPW-TP example a genuine C-only project instead of forcing a C++ linker;
- replaced the previous placeholder embedded workflow with a real freestanding/no-heap portability build.

### Added

- optional header-only C++17 `spwkit::Port` RAII/convenience wrapper controlled by `SPWKIT_ENABLE_CPP`, with no alternate backend implementation;
- independent `SPWKIT_BUILD_CPP_TESTS` and `SPWKIT_BUILD_CPP_EXAMPLES` switches;
- pure-C two-peer simulator behavioral coverage for EOP/EEP and time codes;
- pure-C no-heap caller-owned workspace behavior and workspace-reuse coverage;
- C++ wrapper loopback example;
- Linux C-only static and shared installed-package validation;
- repository hygiene checks rejecting stale pre-C11 target names, forced C++ linker workarounds, obsolete package requests and removed runtime source paths;
- explicit freestanding C/no-heap compile evidence separate from future ARM/HardRT target-HIL claims.

### Portability contract

- `spwkit::spwkit` configures/builds with no C++ compiler, linker or runtime;
- the complete simulator + UDP runtime is exercised in a pure-C profile;
- static archives are checked for accidental C++ ABI/runtime references;
- the optional C++ wrapper remains exception-free and delegates exclusively to the public C API;
- hosted simulator thread primitives and POSIX socket details remain private implementation dependencies.

The dedicated release audit verified package/API version alignment and the full hosted, pure-C, simulator, D2D and freestanding portability gates before the `v0.3.0` tag boundary.

## v0.2.0 — 2026-08-16

Distributed virtual SpaceWire over the existing portable application API.

### Added

- VSPW-TP v1 distributed transport with the released 40-byte network-order header and 64-bit sender session identity;
- POSIX IPv4 UDP backend selected through the normal `spw_port_*` API;
- bounded fragmentation and arbitrary-order reassembly for logical packets up to the backend's 1 MiB limit;
- reliable DATA and TIME_CODE delivery using session-bound logical-message ACKs, bounded retransmission and duplicate suppression;
- KEEPALIVE/session peer discovery, timeout detection and restart recovery;
- deterministic virtual SpaceWire rate/latency modelling separate from incidental host-network timing;
- deterministic seeded transport drop/duplicate/reorder/delay injection;
- explicit SpaceWire-side EEP injection with transport/SpaceWire fault-domain counters;
- reusable shared public backend contract plus distributed peer-loss/restart contract;
- installed-package equal-peer example for independent processes/hosts;
- active D2D CI exercising localhost processes and two Linux network namespaces in addition to transport/recovery/timing/fault tests;
- VSPW-TP Wireshark/tshark capture tooling with deterministic real-dissector validation;
- explicit v0.2 platform policy and installed package UDP runtime metadata.

### Platform scope

- Linux is the primary fully exercised distributed runtime platform;
- macOS is supported as a POSIX UDP host through the host/shared-contract CI;
- Windows retains the portable API/package and public UDP configuration surface, but the v0.2 UDP runtime is not implemented and returns `SPW_ERR_UNSUPPORTED`;
- native Winsock transport is deferred beyond v0.2.0 and tracked separately.

### Release hardening

- package version and public API version are aligned at `0.2.0`;
- Release-mode test targets explicitly keep `assert()` active so test operations and assertions cannot disappear under `NDEBUG`;
- the release audit runs the full Release suite with active assertions and stress-validates simulator edge behavior;
- stale pre-release documentation was reconciled with the completed v0.2 implementation.

## v0.1.0

Portable core and process-local virtual SpaceWire baseline:

- public C ABI and opaque port handles;
- packet send/receive with EOP/EEP preservation;
- explicit link state and lifecycle control;
- time codes;
- capabilities and statistics;
- deterministic loopback backend;
- process-local two-peer virtual SpaceWire simulator;
- reusable backend contract tests;
- caller-owned no-heap port construction;
- optional zero-copy ownership semantics;
- CMake install/export and standalone `find_package(SpWKit)` consumption.
