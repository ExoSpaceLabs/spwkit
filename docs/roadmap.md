# Roadmap

SpWKit has completed the v0.1 portable-core milestone and the v0.2 distributed virtual SpaceWire milestone. The ordering below continues to stabilize software semantics before hardware-specific details are allowed to dictate the application API.

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

`v0.1.0` is tagged at the completed portable-core boundary. Physical FPGA/HIL validation is deliberately outside that release because suitable hardware is not currently available.

## v0.2.0 — Distributed virtual SpaceWire — complete

Delivered in v0.2.0:

- versioned VSPW-TP v1 wire format with a 40-byte header carrying the sender session ID;
- POSIX IPv4 UDP backend selected through `spw_port_*`;
- packet fragmentation/reassembly independent of Ethernet MTU;
- bounded arbitrary-order fragment reassembly with duplicate/overlap validation;
- EOP/EEP preservation across fragments;
- time-code transport;
- bounded 1 MiB receive/reassembly and reliable-TX storage;
- logical-message ACK semantics for DATA/TIME_CODE;
- bounded complete-message retransmission after ACK timeout;
- duplicate logical-message suppression and ACK replay;
- 64-bit sender session identity on every frame plus KEEPALIVE-driven session transitions;
- configured peer address/port validation;
- peer timeout mapping to public link state/errors;
- peer restart/session recovery;
- deterministic SpaceWire-side virtual link rate/latency timing for DATA and TIME_CODE;
- deterministic seeded VSPW-TP transport drop/duplicate/reorder/delay injection;
- explicit SpaceWire-side EEP injection with separate fault-domain diagnostics;
- reusable public backend contract running against the UDP backend;
- reusable distributed peer-loss/restart contract using public operations only;
- standalone installed-package equal-peer UDP example for independent Linux processes/hosts;
- active two-process restart integration using only public APIs;
- active two-network-namespace integration across a 1500-byte-MTU veth link;
- active device-to-device UDP CI including public contract, process/network isolation, retry/dedup/recovery, timing and fault coverage;
- VSPW-TP v1 Wireshark Lua dissector with heuristic and Decode As support;
- deterministic generated-PCAP/tshark validation covering DATA fragments, KEEPALIVE, ACK, TIME_CODE and invalid-version handling;
- documented tcpdump/pcap capture and display-filter workflow;
- explicit hosted platform policy: Linux primary, macOS supported POSIX host, Windows UDP runtime deferred;
- installed-package metadata reporting whether the specific build contains the UDP runtime;
- deterministic `SPW_ERR_UNSUPPORTED` UDP selection on builds without the hosted implementation;
- development package version advanced to 0.2.0.

The v0.2 public SpaceWire API remains backend-neutral: applications do not call UDP, VSPW-TP, POSIX or Winsock APIs directly. Capture tooling remains development-only and adds no runtime dependency to `libspwkit`.

Native Winsock transport is intentionally deferred beyond v0.2 and can later reuse the same codec, reliability, timing, fault, capture and backend-contract work without changing application-facing semantics.

`v0.2.0` is the released distributed virtual SpaceWire milestone. Post-v0.2 portability work, including native Winsock UDP support, remains outside this release boundary.

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
- Zephyr investigation;
- shared embedded contract fixtures;
- embedded Ethernet virtual-link examples.

## v0.8.0 — Router simulation

- multi-port router model;
- logical addressing;
- path addressing;
- routing tables;
- port isolation and fault injection;
- multi-node topology tests.

## v0.9.0 — Upper protocols

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
