# Architecture

SpWKit is designed around one rule: **applications depend on SpaceWire semantics, not on a transport or hardware implementation**.

## Layer model

```text
+-------------------------------------------------------------+
| Application                                                 |
+-----------------------------+-------------------------------+
                              |
+-----------------------------v-------------------------------+
| Public SpWKit API                                           |
|                                                             |
| Port | Packet | Terminator | TimeCode | LinkState | Stats    |
+-----------------------------+-------------------------------+
                              |
+-----------------------------v-------------------------------+
| Portable core                                               |
|                                                             |
| Validation | queues | state | errors | capabilities          |
+-----------------------------+-------------------------------+
                              |
                 backend interface / HAL
                              |
       +----------------------+--------------------------+
       |                      |                          |
+------v-------+      +-------v--------+        +--------v-------+
| Virtual      |      | Linux device  |        | Embedded       |
| backend      |      | backend       |        | backend        |
+------+-------+      +-------+--------+        +--------+-------+
       |                      |                          |
+------v-------+      +-------v--------+        +--------v-------+
| local / UDP |      | /dev/spwX     |        | MMIO / AXI     |
| raw Ethernet|      | ioctl / mmap  |        | DMA / IRQ      |
+--------------+      +-------+--------+        +--------+-------+
                              |                          |
                              +------------+-------------+
                                           |
                                  +--------v--------+
                                  | SpaceWire FPGA  |
                                  | / ASIC codec    |
                                  +--------+--------+
                                           |
                                  Physical SpaceWire
```

## Public API boundary

The public API should represent concepts visible to software using a SpaceWire interface:

- ports;
- packets;
- EOP/EEP termination;
- link state and management;
- time codes;
- statistics and errors;
- implementation capabilities.

The public API must not expose transport-specific concepts such as UDP sockets, Ethernet MAC addresses, AXI registers, DMA descriptor formats, or vendor handles.

## C ABI and C++ API

The portability baseline is planned as a C ABI. This allows use from bare-metal firmware, C flight software, RTOS environments, and foreign-function interfaces.

An idiomatic C++ wrapper can then provide RAII and type safety without making C++ runtime facilities mandatory for the core implementation.

```text
C++ API
   |
   v
stable C ABI
   |
   v
portable core
```

## Backend model

Backends translate the portable SpaceWire operations to an implementation.

Planned backend classes include:

- virtual local link;
- virtual Ethernet link;
- Linux character device;
- bare-metal MMIO/DMA;
- HardRT integration;
- FreeRTOS integration;
- RTEMS integration;
- vendor-specific hardware adapters.

A backend advertises capabilities rather than forcing every implementation to pretend it supports every optional feature.

## Virtual SpaceWire

The virtual implementation should support both local and distributed links.

### Local

```text
Process / node A                            Process / node B
+-------------+                             +-------------+
|   vspw0     |<====== virtual link ======>|   vspw1     |
+-------------+                             +-------------+
```

### Distributed over Ethernet

```text
Host A                                      Host B
+-------------+                             +-------------+
|   vspw0     |                             |   vspw0     |
| virtual SpW |<====== UDP/raw L2 =========>| virtual SpW |
+-------------+       real Ethernet         +-------------+
```

Ethernet is a transport for simulator events. Ethernet timing, MTU, addressing, and reliability must not silently become SpaceWire semantics.

## Linux device model

The long-term Linux-facing model is intended to distinguish:

```text
/dev/vspw0    virtual SpaceWire port
/dev/spw0     physical SpaceWire port
```

The simulator may initially implement a user-space service and later expose device-like semantics through CUSE or a kernel implementation. Physical hardware may be exposed through a kernel driver, vendor driver, or adapter library.

The application contract should remain stable across these implementations.

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
MAC / UDP / raw L2                     MMIO + DMA
```

The embedded core should support deterministic memory ownership, static allocation, polling, interrupt-driven operation, and optional zero-copy paths.

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
node A:vspw0 ----+
                  |
node B:vspw0 -----+---- virtual SpaceWire router ---- node D:vspw0
                  |
node C:vspw0 ----+
```

Routing, contention, finite buffering, and path/logical addressing belong to the SpaceWire simulation layer.

## Design constraints

The portable core should aim for:

- no mandatory heap allocation;
- no mandatory POSIX dependency;
- no mandatory C++ exceptions or RTTI;
- deterministic resource ownership;
- explicit error returns;
- bounded queue operation;
- testable backends;
- transport-independent packet semantics;
- a stable ABI boundary suitable for long-lived embedded integrations.
