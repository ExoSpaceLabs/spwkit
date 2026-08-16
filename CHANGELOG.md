# Changelog

Notable user-visible changes are recorded here. SpWKit follows semantic versioning for package releases while the public C ABI remains explicitly versioned through `SPWKIT_API_VERSION_*`.

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
