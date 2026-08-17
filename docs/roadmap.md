# Roadmap

SpWKit has completed the v0.1 portable-core, v0.2 distributed virtual SpaceWire, and v0.3 C-first runtime releases. The v0.4 feature set is complete and in release audit for the Linux virtual-device/service boundary.

## v0.1.0 — Portable core and local virtual link — released

Delivered:

- public C ABI and opaque port handles;
- packet, EOP/EEP, time-code, link-state, error and capability types;
- deterministic loopback backend;
- process-local two-peer simulator;
- copied packet transfer and optional zero-copy ownership API;
- caller-owned no-heap port construction;
- reusable backend contract suite;
- packet-capacity/no-truncation and link-recovery semantics;
- CMake install/export and standalone `find_package(SpWKit)` verification;
- C/C++ examples;
- Linux GCC/Clang, macOS Clang, Windows MSVC, ASan/UBSan, no-heap and simulator CI.

`v0.1.0` is tagged at the completed portable-core boundary.

## v0.2.0 — Distributed virtual SpaceWire — released

Delivered:

- VSPW-TP v1 with the fixed 40-byte session-aware wire header;
- POSIX IPv4 UDP backend through `spw_port_*`;
- bounded arbitrary-order fragmentation/reassembly;
- EOP/EEP and time-code preservation;
- logical-message ACK/retry and duplicate suppression;
- session/KEEPALIVE peer liveness and restart recovery;
- 1 MiB backend packet/reassembly/reliable-TX bound;
- deterministic virtual SpaceWire rate/latency;
- deterministic transport fault injection and explicit SpaceWire EEP injection;
- shared UDP backend contract and peer-loss/restart contract;
- independent-process and Linux network-namespace D2D integration;
- VSPW-TP Wireshark/tshark tooling;
- explicit POSIX UDP platform policy and installed package metadata.

`v0.2.0` is tagged at the audited distributed-runtime boundary. Native Winsock UDP remains tracked separately in #42.

## v0.3.0 — C11 runtime and optional C++ wrapper — released

Objective: make the implementation match the public portability contract before adding Linux-device, RTOS and hardware-specific layers.

Delivered:

- C11 backend vtable/context substrate replacing internal C++ inheritance;
- C11 port dispatch, workspace ownership and opaque buffer representation;
- C11 loopback backend;
- C11 process-local simulator with private POSIX/Windows hosted synchronization;
- C11 simulator zero-copy ownership path;
- C11 VSPW-TP codec preserving the released v1 wire format;
- C11 bounded fragment reassembly;
- C11 virtual timing and deterministic fault engines;
- C11 POSIX UDP distributed backend preserving v0.2 sessions/retry/liveness semantics;
- complete simulator + UDP runtime build with `CXX=/bin/false` and no C++ ABI/runtime symbols;
- pure-C behavioral tests/examples separated from optional C++ fixtures;
- C-only installed package consumer using `project(... LANGUAGES C)`;
- optional header-only C++17 convenience layer exported as `spwkit::cpp`;
- authoritative C runtime target exported as `spwkit::spwkit`;
- wrapper build switch `SPWKIT_ENABLE_CPP`, independent from runtime/backend selection;
- standard `BUILD_SHARED_LIBS` static/shared selection;
- Linux C-only static and shared installed-package validation;
- documented C versus C++ integration contract and embedded/hosted build profiles.

The C API remains authoritative. The C++ wrapper has no backend implementation and preserves `spw_result_t` error handling.

`v0.3.0` is tagged at the audited C-first runtime boundary.

## v0.4.0 — Linux virtual device and userspace service — release audit

Tracked by #54. Functional implementation is complete; #81 is the final release-hardening/audit gate before tagging.

Delivered:

- private VSPD v1.3 with fixed-width network-order framing and bounded 1 MiB logical packets;
- pure-C `vspwd` two-port userspace service over Linux `AF_UNIX`/`SOCK_SEQPACKET`;
- public Linux `SPW_BACKEND_DEVICE` through the normal `spw_port_*` API;
- full packet/EOP/EEP/zero-length/time-code/link/statistics/restart behavior;
- backend-neutral level-triggered `spw_port_wait()` receive readiness;
- shared backend-contract coverage for the device path;
- non-owning `spwctl` management and passive `spwmon` observation;
- standalone installed-package C and optional C++ device consumers with mixed-language peer tests;
- CUSE/libfuse3 feasibility and a private packet-record prototype, with production presenter deferred to #78;
- topology-owned `vspwd` VSPW-TP/UDP bridge with remote loss/restart recovery;
- dedicated pure-C GCC/Clang device/service/tools/bridge CI, sanitizers and installed-consumer gates.

Explicitly deferred beyond v0.4: production CUSE presenter (#78), native Winsock UDP (#42), physical FPGA/HIL, and generic router/topology simulation.

## v0.5.0 — Embedded and HardRT

- bare-metal platform adapter;
- polling mode;
- interrupt-driven mode;
- user-provided packet buffers;
- Ethernet virtual backend for embedded targets;
- HardRT synchronization and task/event adapter;
- Linux <-> HardRT virtual-link example.

## v0.6.0 — Physical FPGA reference backend

Reference target: AMD SoC evaluation platform.

- AXI4-Lite control/status contract;
- AXI4-Stream packet contract;
- AXI DMA integration;
- Linux physical-device backend;
- bare-metal physical backend;
- `/dev/spw0` reference interface;
- hardware loopback and two-endpoint tests.

The repository does not require the SpaceWire RTL implementation itself to be open source. The software contract should support independent open, commercial, or vendor hardware implementations.

## v0.7.0 — Link behavioural simulation

- ECSS-oriented link state model;
- finite receive credit;
- flow-control effects;
- disconnect/error recovery;
- queue contention;
- configurable character/link timing model;
- character/link error injection.

## v0.8.0 — RTOS adapters

- FreeRTOS adapter;
- RTEMS adapter;
- Zephyr investigation;
- shared embedded contract fixtures;
- embedded Ethernet virtual-link examples.

## v0.9.0 — Router simulation

- multi-port router model;
- logical addressing;
- path addressing;
- routing tables;
- port isolation and fault injection;
- multi-node topology tests.

## v0.10.0 — Upper protocols

- protocol-ID integration hooks;
- RMAP module;
- CCSDS packet-transfer helpers;
- interoperability examples with external CCSDS/PUS stacks.

## v1.0.0 — Stable public contract

- stable public C ABI;
- documented backend capability model;
- Linux, simulator and embedded reference backends;
- release-level conformance/contract evidence;
- compatibility policy for future backends and protocol modules.
