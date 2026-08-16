# Changelog

Notable user-visible changes are recorded here. SpWKit follows semantic versioning for package releases while the public C ABI remains explicitly versioned through `SPWKIT_API_VERSION_*`.

## v0.4.0 — unreleased

Linux virtual-device and userspace-service development line.

### Added

- private VSPD v1 backend↔daemon protocol, distinct from VSPW-TP, with a fixed 40-byte network-order header;
- request/response correlation plus asynchronous DATA/TIME_CODE/link-state events;
- bounded DATA fragmentation with 32 KiB local IPC records and a 1 MiB logical-packet ceiling;
- explicit EOP/EEP, link-state, capability, statistics and fixed status encodings without native structure layout on wire;
- pure-C golden/malformed protocol vectors;
- Linux `AF_UNIX`/`SOCK_SEQPACKET` integration coverage for record preservation, non-blocking receive, `poll()` readiness and disconnect;
- dedicated pure-C Virtual device CI on GCC/Clang.

### Direction

- `vspwd` and the Linux device backend will use VSPD beneath the existing `spw_port_*` API;
- Unix-domain `SOCK_SEQPACKET` is the unprivileged reference transport;
- CUSE `/dev/vspwX` remains a presentation-layer investigation rather than a prerequisite for CI/development;
- VSPD codec logic remains portable C and participates in the freestanding/no-heap portability gate.

No `v0.4.0` release tag is implied by this development section.

## v0.3.0 — 2026-08-16

C-first runtime and packaging architecture. The public C API remains authoritative while the implementation no longer requires a C++ toolchain.

### Changed

- converted port dispatch, workspace ownership and backend polymorphism to a C11 vtable/context model;
- converted loopback, process-local simulator, zero-copy simulator path, VSPW-TP codec, fragment reassembly, virtual timing, deterministic fault logic and POSIX UDP backend to C11;
- preserved the released VSPW-TP v1 wire format and v0.2 session/reliability semantics through the implementation-language conversion;
- exported installed/runtime targets as lowercase `spwkit::spwkit` and optional `spwkit::cpp`, while retaining `find_package(SpWKit)` as the package lookup name;
- separated pure-C tests/examples from optional C++ development fixtures so CTest can execute meaningful behavior with `CXX=/bin/false`;
- made the installed distributed VSPW-TP example a genuine C-only project instead of forcing a C++ linker;
- replaced the previous placeholder embedded workflow with a real freestanding C/no-heap portability build.

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
- deterministic transport drop/duplicate/reorder/delay injection and explicit SpaceWire-side EEP injection;
- backend-neutral fault-domain statistics;
- reusable shared public backend contract coverage for the UDP backend and distributed peer-loss/restart extensions;
- installed-package equal-peer distributed example with two-process and Linux network-namespace integration;
- VSPW-TP Wireshark Lua dissector plus deterministic PCAP/tshark validation;
- installed-package metadata describing whether the current build contains the UDP runtime.

### Platform scope

- Linux is the primary fully exercised distributed runtime platform;
- macOS is supported as a POSIX UDP host through host/shared-contract CI;
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
- copied packet I/O with EOP/EEP preservation and no-truncation receive semantics;
- link lifecycle/state, time codes, capabilities and statistics;
- deterministic loopback backend;
- process-local equal-peer simulator;
- caller-owned/no-heap port construction;
- optional zero-copy ownership API;
- reusable backend contract tests;
- CMake install/export and standalone `find_package(SpWKit)` consumption.
