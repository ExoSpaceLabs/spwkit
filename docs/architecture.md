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
| Local        |      | Distributed   |        | Linux hosted   |
| simulator    |      | VSPW-TP/UDP   |        | virtual device |
+------+-------+      +-------+--------+        +--------+-------+
       |                      |                          |
 process-local link         UDP/IP                VSPD / vspwd
                                                        |
                                             future /dev/vspwX
```

Implemented backends currently include:

- deterministic loopback reference backend;
- process-local two-peer simulator;
- POSIX VSPW-TP/UDP distributed backend;
- Linux hosted virtual-device backend speaking VSPD to `vspwd`.

The v0.4 virtual-device/service feature set is complete: the device backend, `vspwd`, readiness, management/monitoring, installed consumers and VSPW-TP/UDP bridge are implemented. A production `/dev/vspwX` CUSE presenter remains an optional post-v0.4 layer tracked by #78. Embedded/RTOS adapters and physical FPGA/vendor implementations follow on the roadmap.

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

The mandatory common API does not expose transport-specific concepts such as UDP sockets, Unix-domain sockets, VSPD frames, CUSE handles, Ethernet addresses, AXI registers, DMA descriptors, physical addresses or vendor handles.

Backend-specific configuration structures may describe the selected implementation using portable values, but normal packet/link operations remain portable.

## C runtime and optional C++ use

The portability baseline is C11 end to end: the public ABI and `libspwkit` runtime are C. A project using only `spwkit::spwkit` does not enable or link C++.

`SPWKIT_ENABLE_CPP=ON` adds the header-only C++17 `spwkit::cpp` target. It provides move-only RAII/convenience syntax over the same C handles, structures and `spw_result_t` values; it contains no backend implementation.

```text
C application -----------------------------+
                                           |
C++ application -> optional spwkit::cpp ---+--> stable C ABI --> C11 core/backends
```

`find_package(SpWKit)` remains the package/config name; imported CMake target namespaces are lowercase.

The Linux virtual-device path is tested through both the C API and the optional C++ wrapper. The wrapper reaches the same `SPW_BACKEND_DEVICE` implementation and does not speak VSPD directly.

See `docs/language-bindings.md` for the exact language/build contract.

## Backend model

Backends translate portable SpaceWire operations to an implementation while preserving the same application-visible contract.

Current backends:

- `SPW_BACKEND_LOOPBACK`: deterministic in-process reference backend;
- `SPW_BACKEND_SIMULATOR`: process-local equal-peer SpaceWire simulator;
- `SPW_BACKEND_UDP`: distributed VSPW-TP transport over UDP on supported POSIX hosts;
- `SPW_BACKEND_DEVICE`: Linux hosted VSPD client backend for `vspwd`.

Planned backend families:

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

v0.4 develops the virtual path first. The first working layer is userspace-hosted:

```text
Application
    |
spw_port_* / optional spwkit::Port
    |
SPW_BACKEND_DEVICE
    |
private VSPD over AF_UNIX/SOCK_SEQPACKET
    |
  vspwd
    |
virtual port 0 <================> virtual port 1
```

The public backend configuration contains only a bounded endpoint path and daemon `port_id`. Unix file descriptors, `sockaddr_un`, VSPD records and poll state remain private.

The backend translates lifecycle, DATA, EOP/EEP, time codes, state/capabilities and statistics to VSPD. It reassembles logical packets before returning them through `spw_port_receive()` and preserves receive-too-small retry semantics. After daemon/session loss, subsequent normal API calls attempt reconnect/HELLO/ATTACH and restore START intent without requiring a new public port object.

The current userspace socket is both the implementation transport and unprivileged CI boundary. CUSE/libfuse3 feasibility has been validated with a packet-record prototype; the production event-driven presenter is tracked in #78. Any future `/dev/vspwX` presentation remains below `spw_port_*` and does not become a second application API.

`vspwd` may also reserve one of the two reference ports as a topology-owned VSPW-TP/UDP bridge endpoint:

```text
local application -> SPW_BACKEND_DEVICE -> VSPD -> vspwd
                                                local port <-> bridged port
                                                                 |
                                                            SPW_BACKEND_UDP
                                                                 |
                                                            remote peer
```

The bridge reuses the existing UDP backend and keeps VSPW-TP reliability/transport logic out of the daemon itself.

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

Hosted Linux backends such as `SPW_BACKEND_DEVICE` are explicitly disabled in the freestanding/embedded portability profile. The VSPD codec itself remains portable C and may still be compiled there because fixed-width protocol encoding has no socket/POSIX dependency.

The VSPW-TP codec itself likewise has no socket/POSIX dependency so it can be reused by future lwIP-based distributed backends.

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

Hosted backends may add private platform dependencies when selected, but portability CI must prove those dependencies disappear when the backend is disabled.
