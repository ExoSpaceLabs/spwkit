# Roadmap

SpWKit has completed its v0.1 portable-core milestone and is now implementing v0.2 distributed virtual SpaceWire. The ordering below continues to stabilize software semantics before hardware-specific details are allowed to dictate the application API.

## v0.1.0 — Portable core and local virtual link — complete

Delivered:

- public C ABI and opaque port handles;
- packet, EOP/EEP, time-code, link-state, error and capability types;
- deterministic in-process loopback backend;
- process-local two-peer simulator;
- copied packet transfer and optional zero-copy ownership API;
- caller-owned no-heap port construction;
- reusable backend contract suite;
- packet-capacity/no-truncation and link-recovery semantics;
- CMake install/export and standalone `find_package(SpWKit)` verification;
- C/C++ examples;
- Linux GCC/Clang, macOS Clang, Windows MSVC, ASan/UBSan, no-heap and simulator CI.

Physical FPGA/HIL validation is deliberately outside the v0.1 release boundary because suitable hardware is not currently available.

## v0.2.0 — Distributed virtual SpaceWire — in progress

Implemented on current `main`:

- versioned VSPW-TP v1 wire format;
- POSIX UDP backend selected through `spw_port_*`;
- packet fragmentation/reassembly independent of Ethernet MTU;
- EOP/EEP preservation across fragments;
- time-code transport;
- bounded receive/reassembly storage;
- active device-to-device UDP CI.

Remaining v0.2 work:

- ACK/retransmission and duplicate handling where transport reliability requires it;
- peer keepalive and disconnect detection;
- transport sequence/loss/reordering policy;
- configurable virtual link rate and latency;
- deterministic SpaceWire-side and transport-side fault injection;
- Linux-to-Linux distributed examples;
- Wireshark dissector or capture tooling;
- broaden the shared backend contract where distributed semantics require additional assertions.

The public SpaceWire API remains unchanged: applications do not call UDP or VSPW-TP directly.

## v0.3.0 — Linux virtual device

- `vspwd` simulator service;
- `/dev/vspwX` device model investigation/implementation;
- blocking and non-blocking packet API integration;
- `poll`/event integration;
- link statistics;
- `spwctl` management utility;
- `spwmon` monitoring utility.

## v0.4.0 — Embedded and HardRT

- bare-metal platform adapter;
- polling mode;
- interrupt-driven mode;
- user-provided packet buffers;
- Ethernet virtual backend for embedded targets;
- HardRT synchronization and task/event adapter;
- Linux <-> HardRT virtual-link example.

## v0.5.0 — Physical FPGA reference backend

Reference target: AMD SoC evaluation platform.

- AXI4-Lite control/status contract;
- AXI4-Stream packet contract;
- AXI DMA integration;
- Linux physical-device backend;
- bare-metal physical backend;
- `/dev/spw0` reference interface;
- hardware loopback and two-endpoint tests.

The repository does not require the SpaceWire RTL implementation itself to be open source. The software contract should support independent open, commercial, or vendor hardware implementations.

## v0.6.0 — Link behavioural simulation

- ECSS-oriented link state model;
- finite receive credit;
- flow-control effects;
- disconnect/error recovery;
- queue contention;
- configurable character/link timing model;
- character/link error injection.

## v0.7.0 — RTOS adapters

- FreeRTOS adapter;
- RTEMS adapter;
- deterministic integration examples;
- common backend conformance test suite across Linux, bare metal, HardRT, FreeRTOS and RTEMS where available.

## v0.8.0 — Network/router simulation

- virtual SpaceWire router;
- path addressing;
- logical addressing;
- routing tables;
- finite output buffering;
- contention and blocking;
- multi-node topology configuration.

## v0.9.0 — Upper-layer protocols

Candidate optional modules:

- SpaceWire protocol identification;
- RMAP;
- CCSDS packet transfer over SpaceWire.

These remain separate from the raw core API.

## v1.0.0 — Stable software contract

Target conditions:

- stable public C ABI;
- stable C++ API;
- local and distributed virtual backends;
- Linux and embedded reference implementations;
- physical hardware reference backend;
- documented ECSS requirement mapping for implemented software-visible scope;
- repeatable CI test matrix;
- migration/versioning policy;
- published integration documentation.

## Beyond v1.0

Potential areas include:

- additional FPGA/vendor adapters;
- richer DMA/scatter-gather extensions;
- hardware-in-the-loop gateways;
- record/replay tooling;
- PCAP/Wireshark integration;
- SpaceWire router performance simulation;
- standardized topology/configuration format;
- integration with mission simulation frameworks.
