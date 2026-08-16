# Architecture

SpWKit is designed around one rule: **applications depend on SpaceWire semantics, not on a transport or hardware implementation**.

## Layer model

```text
+-------------------------------------------------------------+
| Application                                                 |
+-----------------------------+-------------------------------+
                              |
+-----------------------------v-------------------------------+
| Public SpWKit C ABI                                         |
|                                                             |
| Port | Packet | Terminator | TimeCode | LinkState | Stats    |
+-----------------------------+-------------------------------+
                              |
+-----------------------------v-------------------------------+
| Portable C11 core                                           |
|                                                             |
| Validation | state | errors | capabilities | ownership       |
+-----------------------------+-------------------------------+
                              |
                   C backend vtable/context
                              |
       +----------------------+--------------------------+
       |                      |                          |
+------v-------+      +-------v--------+        +--------v-------+
| Local        |      | Distributed   |        | Linux / future |
| simulator    |      | VSPW-TP/UDP   |        | HW/RTOS        |
+------+-------+      +-------+--------+        +--------+-------+
       |                      |                          |
 process-local link         UDP/IP              vspwd, /dev,
                                                  MMIO/DMA/vendor
```

Implemented backends currently include:

- deterministic loopback reference backend;
- process-local two-peer simulator;
- POSIX VSPW-TP/UDP distributed backend.

The next backend family is the Linux virtual-device/userspace-service layer tracked by #54. Embedded/RTOS adapters and physical FPGA/vendor implementations follow on the roadmap.

## Public API boundary

The public API represents concepts visible to software using a SpaceWire interface:

- ports;
- packets;
- EOP/EEP termination;
- link state and management;
- time codes;
- statistics and errors;
- implementation capabilities;
- optional ownership-oriented packet buffers.

The mandatory common API does not expose transport-specific concepts such as UDP sockets, Unix-domain sockets, CUSE handles, Ethernet addresses, AXI registers, DMA descriptors, physical addresses or vendor handles.

Backend-specific configuration structures may describe the selected implementation, but normal packet/link operations remain portable.

## C runtime and optional C++ use

The portability baseline is C11 end to end: the public ABI and `libspwkit` runtime are C. A project using only `spwkit::spwkit` does not enable or link C++.

`SPWKIT_ENABLE_CPP=ON` adds the header-only C++17 `spwkit::cpp` target. It provides move-only RAII/convenience syntax over the same C handles, structures and `spw_result_t` values; it contains no backend implementation.

```text
C application -----------------------------+
                                           |
C++ application -> optional spwkit::cpp ---+--> stable C ABI --> C11 core/backends
```

`find_package(SpWKit)` remains the package/config name; imported CMake target namespaces are lowercase.

See `docs/language-bindings.md` for the exact language/build contract.

## Backend model

Backends translate portable SpaceWire operations to an implementation while preserving the same application-visible contract.

Current backends:

- `SPW_BACKEND_LOOPBACK`: deterministic in-process reference backend;
- `SPW_BACKEND_SIMULATOR`: process-local equal-peer SpaceWire simulator;
- `SPW_BACKEND_UDP`: distributed VSPW-TP transport over UDP on supported POSIX hosts.

Planned backends:

- Linux virtual/device service;
- Linux physical character device;
- bare-metal MMIO/DMA;
- HardRT integration;
- FreeRTOS integration;
- RTEMS integration;
- vendor-specific hardware adapters.

A backend advertises capabilities rather than forcing every implementation to pretend it supports every optional feature.

## Local virtual SpaceWire

The process-local simulator pairs two endpoints by `link_id` and A/B pairing label:

```text
Application A                            Application B
+-------------+                          +-------------+
| libspwkit   |<====== virtual link ====>| libspwkit   |
| endpoint A  |                          | endpoint B  |
+-------------+                          +-------------+
```

A/B are deterministic pairing labels only. There is no server/client role.

The simulator preserves packet boundaries, EOP/EEP, time codes, bounded resources, link lifecycle/recovery and zero-copy ownership semantics. It is implemented in C and can be built and behaviorally tested with no C++ compiler.

## Distributed virtual SpaceWire

v0.2.0 introduced the distributed VSPW-TP/UDP backend:

```text
Host A                                      Host B
+-------------+                             +-------------+
| Application |                             | Application |
+------+------+                             +------+------+
       |                                           |
   libspwkit                                   libspwkit
       |                                           |
 SPW_BACKEND_UDP                            SPW_BACKEND_UDP
       |                                           |
       +------------ VSPW-TP / UDP ----------------+
                    real IP network
```

VSPW-TP is an internal versioned framing protocol. The implementation fragments and reassembles SpaceWire packet payloads independently of Ethernet MTU while keeping fragments invisible to applications.

Ethernet/IP timing, MTU and datagram boundaries do not become SpaceWire semantics. Transport loss/reordering and peer liveness remain transport concerns and are not silently reclassified as SpaceWire errors.

## Linux virtual-device model

The Linux-facing naming model distinguishes:

```text
/dev/vspw0    virtual SpaceWire port
/dev/spw0     physical SpaceWire port
```

v0.4 develops the virtual path first. The initial implementation is deliberately userspace-first:

```text
Application
    |
libspwkit Linux-device backend
    |
Unix/device boundary
    |
  vspwd
    |
+---+------------------+
|                      |
local virtual peer   VSPW-TP/UDP peer
```

A Unix-domain socket is retained as the unprivileged/CI fallback. CUSE is investigated as a way to expose `/dev/vspwX` without committing immediately to a kernel module. Whatever presentation is selected, the daemon/private protocol stays below `spw_port_*` and must not become a second application API.

The future physical `/dev/spwX` path should reuse the same application-facing contract while replacing the implementation beneath it.

## Embedded model

Bare-metal and RTOS targets do not require a filesystem or `/dev` abstraction. They instantiate logical ports directly.

```text
Application
    |
SpWKit core
    |
backend instance
    |
+-------------------+----------------------+
|                                          |
Virtual Ethernet                       FPGA hardware
lwIP / UDP / raw L2                    MMIO + DMA
```

The embedded core is intended to support deterministic memory ownership, caller-owned storage, polling, interrupt-driven operation and optional zero-copy paths.

The VSPW-TP codec itself has no socket/POSIX dependency so it can be reused by future lwIP-based distributed backends.

## FPGA reference path

A reference AMD SoC integration is expected to use:

```text
Application
    |
SpWKit
    |
Linux / bare-metal driver
    |
+----------------------+----------------------+
| AXI4-Lite            | AXI4-Stream / DMA    |
| control/status       | packet data          |
+----------+-----------+-----------+----------+
           |                       |
           +-----------+-----------+
                       |
                SpaceWire codec
                       |
                Data / Strobe
```

The FPGA implementation is deliberately outside the portable software contract. Open or proprietary cores can implement the same backend requirements.

## Router simulation

A future virtual router should model SpaceWire routing semantics rather than use a Linux Ethernet bridge as a substitute.

```text
node A ----+
           |
node B ----+---- virtual SpaceWire router ---- node D
           |
node C ----+
```

Routing, contention, finite buffering and path/logical addressing belong to the SpaceWire simulation layer.

## Design constraints

The portable core maintains these constraints:

- no mandatory heap allocation;
- no mandatory POSIX dependency in the common API/core contract;
- no mandatory C++ runtime, exceptions or RTTI;
- deterministic resource ownership;
- explicit error returns;
- bounded resources where implementations require them;
- shared backend contract testing;
- transport-independent packet semantics;
- stable application-facing ABI suitable for long-lived embedded integrations.
