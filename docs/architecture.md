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
| Portable core                                               |
|                                                             |
| Validation | state | errors | capabilities | ownership       |
+-----------------------------+-------------------------------+
                              |
                 backend interface / HAL
                              |
       +----------------------+--------------------------+
       |                      |                          |
+------v-------+      +-------v--------+        +--------v-------+
| Local        |      | Distributed   |        | Future         |
| simulator    |      | VSPW-TP/UDP   |        | HW/RTOS        |
+------+-------+      +-------+--------+        +--------+-------+
       |                      |                          |
 process-local link         UDP/IP                 /dev, MMIO,
                                                  AXI/DMA/vendor
```

Implemented backends currently include:

- deterministic loopback reference backend;
- process-local two-peer simulator;
- POSIX VSPW-TP/UDP distributed backend.

Planned backend families include Linux character devices, embedded/RTOS adapters and physical FPGA/vendor implementations.

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

The mandatory common API does not expose transport-specific concepts such as UDP sockets, Ethernet addresses, AXI registers, DMA descriptors, physical addresses or vendor handles.

Backend-specific configuration structures may describe the selected implementation, but normal packet/link operations remain portable.

## C runtime and optional C++ use

The portability baseline is C11 end to end: the public ABI and `libspwkit` runtime are C. A project using only `SpWKit::spwkit` does not enable or link C++.

`SPWKIT_ENABLE_CPP=ON` adds the header-only C++17 `SpWKit::cpp` target. It provides move-only RAII/convenience syntax over the same C handles, structures and `spw_result_t` values; it contains no backend implementation.

```text
C application -----------------------------+
                                           |
C++ application -> optional SpWKit::cpp ---+--> stable C ABI --> C11 core/backends
```

See `docs/language-bindings.md` for the exact language/build contract.

## Backend model

Backends translate portable SpaceWire operations to an implementation while preserving the same application-visible contract.

Current backends:

- `SPW_BACKEND_LOOPBACK`: deterministic in-process reference backend;
- `SPW_BACKEND_SIMULATOR`: process-local equal-peer SpaceWire simulator;
- `SPW_BACKEND_UDP`: distributed VSPW-TP transport over UDP on supported POSIX hosts.

Planned backends:

- Linux character device;
- bare-metal MMIO/DMA;
- HardRT integration;
- FreeRTOS integration;
- RTEMS integration;
- vendor-specific hardware adapters.

A backend advertises capabilities rather than forcing every implementation to pretend it supports every optional feature.

## Local virtual SpaceWire

The v0.1 simulator pairs two process-local endpoints by `link_id` and A/B pairing label:

```text
Application A                            Application B
+-------------+                          +-------------+
| libspwkit   |<====== virtual link ====>| libspwkit   |
| endpoint A  |                          | endpoint B  |
+-------------+                          +-------------+
```

A/B are deterministic pairing labels only. There is no server/client role.

The simulator preserves packet boundaries, EOP/EEP, time codes, bounded resources, link lifecycle/recovery and zero-copy ownership semantics.

## Distributed virtual SpaceWire

v0.2.0 includes the distributed VSPW-TP/UDP backend:

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

Ethernet/IP timing, MTU and datagram boundaries do not become SpaceWire semantics. Transport loss/reordering and peer liveness are explicit v0.2 concerns and are not silently reclassified as SpaceWire errors.

## Linux device model

The long-term Linux-facing model distinguishes:

```text
/dev/vspw0    virtual SpaceWire port
/dev/spw0     physical SpaceWire port
```

`/dev/vspwX` and `vspwd` are not implemented yet. They are planned for the Linux virtual-device milestone after the distributed backend matures.

The application contract should remain stable across user-space UDP, future virtual devices and physical drivers.

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
- no mandatory C++ exceptions or RTTI;
- deterministic resource ownership;
- explicit error returns;
- bounded resources where implementations require them;
- shared backend contract testing;
- transport-independent packet semantics;
- stable application-facing ABI suitable for long-lived embedded integrations.
