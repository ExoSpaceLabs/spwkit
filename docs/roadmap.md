# Roadmap

SpWKit has completed five public development releases. The current `develop` branch targets v0.6.0 and is focused on the software boundary required before a real FPGA/HDL SpaceWire implementation can be integrated honestly.

## v0.1.0 - Portable core and local virtual link - released

Delivered:

- public C ABI and opaque port handles;
- packet, EOP/EEP, time-code, link-state, error and capability types;
- deterministic loopback backend;
- process-local two-peer simulator;
- copied packet transfer and optional zero-copy ownership API;
- caller-owned no-heap port construction;
- reusable backend contract suite;
- CMake install/export and standalone consumers;
- Linux GCC/Clang, macOS Clang, Windows MSVC, sanitizers and no-heap CI.

## v0.2.0 - Distributed virtual SpaceWire - released

Delivered:

- VSPW-TP v1 session-aware wire framing;
- POSIX UDP backend through `spw_port_*`;
- bounded fragmentation/reassembly;
- EOP/EEP and time-code preservation;
- ACK/retry, duplicate suppression and restart recovery;
- virtual timing and deterministic fault injection;
- independent-process and network-namespace D2D integration;
- VSPW-TP Wireshark/tshark tooling.

## v0.3.0 - C11 runtime and optional C++ wrapper - released

Delivered:

- authoritative C11 backend vtable/context runtime;
- C11 port dispatch and opaque-buffer representation;
- C implementations of loopback, simulator, VSPW-TP and UDP paths;
- pure-C/no-C++ runtime builds and installed consumers;
- optional header-only C++17 convenience layer;
- standard static/shared selection;
- explicit no-heap/freestanding profile.

## v0.4.0 - Linux virtual device and userspace service - released

Delivered:

- VSPD network-order private protocol;
- pure-C `vspwd` virtual-device service;
- public Linux `SPW_BACKEND_DEVICE`;
- packet/time-code/link/statistics/restart behavior;
- backend-neutral receive readiness;
- `spwctl` management and `spwmon` observation;
- installed C/C++ device consumers;
- VSPW-TP bridge and remote restart recovery;
- Debian/GHCR release artifacts for `amd64` and `arm64`.

## v0.5.0 - Hosted parity and embedded integration - released

Delivered:

- production Linux CUSE `/dev/vspwX` presentation while keeping libfuse outside `libspwkit`;
- native Windows/Winsock implementation of the existing VSPW-TP UDP backend;
- shared Windows transport contract and independent-process peer restart validation;
- hosted DEB expansion to `amd64`, `arm64`, `armhf`, and `riscv64`;
- one four-platform GHCR runtime image;
- HardRT POSIX integration under GCC/Clang;
- Cortex-M7 `arm-none-eabi`/Thumb/soft-float/no-heap compile/link evidence;
- consolidated CI, tagged Release and manual HIL lifecycle workflows.

## v0.6.0 - Portable hardware-driver integration - current

Tracked by #108.

The purpose of v0.6 is to complete everything that can be defined and validated in software before a real FPGA SpaceWire implementation is required.

Planned/delivered work:

- pinned CCSDSPack `v2.0.0` PUS-C interoperability through installed SpWKit packages;
- current-documentation reconciliation after the v0.5 release;
- public portable driver backend/configuration contract behind the normal `spw_port_*` API;
- lifecycle, copied packet, EOP/EEP, time-code, readiness, capabilities and statistics delegation to platform/vendor drivers;
- DMA-capable TX/RX buffers mapped onto the existing SpWKit zero-copy ownership API;
- deterministic in-memory reference driver run through the reusable backend contract in CI;
- no-heap and RTOS/bare-metal-friendly driver integration evidence;
- explicit future FPGA boundary covering register map, DMA descriptors, interrupts, coherency, clock/reset domains and HIL requirements.

v0.6 deliberately does **not** implement a real SpaceWire HDL/IP core and does not claim electrical interoperability. See `docs/v0.6-scope.md`.

## After v0.6 - FPGA/HDL implementation boundary

The next hardware phase will need its own specification before implementation. Expected design topics include:

- SpaceWire link/IP core scope and ECSS behavior;
- AXI4-Lite or equivalent control/status register map;
- packet/DMA descriptor format and ownership;
- address width, alignment and scatter/gather policy;
- interrupt/event model;
- cache maintenance and coherent/non-coherent DMA behavior;
- clock/reset-domain ownership;
- time-code path and link/error counters;
- Linux and bare-metal driver bindings;
- physical loopback, two-endpoint and electrical/HIL validation.

The software driver contract should allow open, commercial, or vendor HDL implementations rather than coupling SpWKit to one RTL core.

## Later software directions

The exact release numbering should be assigned when each scope becomes active rather than preserving obsolete calendar guesses. Candidate directions are:

- deeper ECSS-oriented link behavioral simulation, finite credits and contention;
- broader RTOS adapters such as FreeRTOS, RTEMS and Zephyr;
- multi-port router/topology simulation with path/logical addressing;
- RMAP and other upper-protocol integration;
- stable v1.0 compatibility policy once the software and hardware backend contracts have enough real implementation evidence.

## v1.0 target

A v1.0 declaration should require evidence, not merely accumulated features:

- stable documented public C ABI and compatibility policy;
- backend capability and ownership contracts frozen enough for independent implementations;
- simulator, hosted and embedded/hardware-backed reference paths;
- release-level conformance/contract evidence;
- a documented policy for future backends and protocol modules.
