# Backend contract

This document defines the internal backend boundary used by `libspwkit`.

Applications do not use backend objects directly. Public SpaceWire operations enter through the SpWKit C API and are translated by the library to the selected backend.

```text
Application
    |
    v
libspwkit public C API
    |
    v
+-----------------------------+
| C backend vtable + context  |
+--------------+--------------+
               |
       +-------+--------+-------------------+----------------+
       |                |                   |                |
       v                v                   v                v
   loopback          simulator          UDP/VSPW-TP      future device/HW
```

## Mandatory backend operations

The internal C backend contract covers:

- construction/destruction of caller-owned backend context;
- link start;
- link stop;
- reset;
- link-state query;
- capability query;
- copied packet send/receive;
- time-code send/receive;
- common statistics query/reset.

Optional ownership-oriented hooks cover zero-copy TX/RX when a backend advertises `SPW_CAP_ZERO_COPY`.

Backend polymorphism is implemented by a C function-pointer table plus opaque backend context. Backend-specific configuration/validation is handled by the port factory/configuration layer. No C++ class hierarchy is required by the runtime.

## Invariants

Every backend must preserve the software-visible SpWKit contract:

- packet boundaries are never merged or exposed as transport fragments;
- EOP and EEP are preserved;
- receives never silently truncate packets;
- optional functions are reflected accurately by capabilities;
- link states map into the common SpaceWire state model;
- common statistics use documented meanings across backends;
- backend implementation types do not leak into common operation signatures;
- failed ownership-transfer calls do not steal application-owned zero-copy buffers.

## Allocation policy

The backend interface itself does not require heap allocation.

Backends may have different implementation strategies, but portable and embedded paths must be able to use caller-owned or statically allocated storage where their platform contract requires it. The loopback path intentionally uses fixed-capacity storage and remains the no-heap reference path.

Hosted backends such as the process-local simulator and POSIX UDP transport may use platform services internally, but must not change common application ownership semantics.

## Loopback backend

The deterministic loopback backend is used for contract development and portable-core verification.

Its current limits are:

```text
maximum packet size: 4096 bytes
packet queue depth:  8
time-code depth:     8
```

```text
send(packet)
    |
    v
bounded local packet queue
    |
    v
receive(packet)
```

It is not a SpaceWire network simulator and does not reproduce exchange-level timing. Its purpose is to validate backend dispatch, ownership/error semantics, packet boundaries, EOP/EEP handling, bounded resources, time codes, statistics and reset behavior deterministically.

For loopback:

- `start()` moves directly to `SPW_LINK_RUN`;
- `stop()` moves to `SPW_LINK_READY`;
- `reset()` clears pending queues and moves to `SPW_LINK_ERROR_RESET`.

## Process-local simulator backend

The simulator is an equal-peer local virtual link. It adds:

- A/B endpoint pairing by `link_id`;
- `CONNECTING`/`RUN` peer lifecycle behavior;
- peer stop/reset/close/reopen recovery;
- independent bounded packet/time-code queues;
- finite and infinite waits using private hosted synchronization;
- zero-copy ownership emulation with fixed host-memory buffers.

The simulator runtime is C. The hosted synchronization shim uses native platform primitives privately and does not enter the portable core ABI.

## Distributed UDP backend

The VSPW-TP/UDP backend implements the same internal contract while translating copied packet/time-code operations into versioned VSPW-TP datagrams.

It provides:

- C11 POSIX IPv4 UDP transport;
- bounded packet fragmentation/reassembly;
- EOP/EEP preservation across fragments;
- time-code transfer;
- logical-message ACK/retransmission and duplicate suppression;
- session/KEEPALIVE peer liveness and restart recovery;
- bounded arbitrary-order fragment reassembly;
- deterministic virtual rate/latency;
- deterministic transport fault injection;
- explicit SpaceWire-side EEP injection;
- separate fault-domain diagnostics;
- a 1 MiB backend packet/reassembly limit;
- a default 1200-byte fragment payload.

Transport fragments are never surfaced through `spw_port_receive()`.

## Linux virtual-device backend direction

v0.4 adds a Linux virtual-device/userspace-service path beneath the same C vtable/context contract. The application must not gain a daemon-specific API.

The intended layering is:

```text
spw_port_* API
     |
Linux device backend
     |
private Unix/device protocol
     |
   vspwd
```

Unix-domain sockets, `poll()` descriptors, CUSE handles and future kernel interfaces stay private to that implementation. The backend must eventually pass the same observable contract as other ports for every capability it advertises.

## Receive capacity

When the next complete packet is larger than caller-provided receive capacity:

- `SPW_ERR_BUFFER_TOO_SMALL` is returned;
- the required payload length is reported;
- the packet terminator is reported;
- caller payload storage is not partially modified;
- the complete packet remains available for a later receive with adequate storage.

This behavior is part of the shared application-visible contract.

## Timeouts

Timeout behavior is expressed in the common microsecond timeout type.

Different backends may wait using condition variables, socket polling, RTOS primitives, interrupts, hardware queues or polling loops. The backend mechanism is not part of the public API.

## Testing

The shared contract suite exercises loopback, the local simulator and the distributed UDP backend. Distributed extensions verify peer loss/restart through public APIs, while codec/D2D tests verify fragmentation, arbitrary ordering, retry/deduplication, timing, deterministic faults, installed-package process isolation and Linux network-namespace operation.

Pure-C CI separately proves that core and simulator behavior execute with no C++ compiler. v0.4 device work must add its integration fixtures without weakening those existing gates.
