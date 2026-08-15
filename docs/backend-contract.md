# Backend contract

This document defines the internal backend boundary used by `libspwkit`.

Applications do not use backend objects directly. Public SpaceWire operations enter through the SpWKit C API and are translated by the library to the selected backend.

```text
Application
    |
    v
libspwkit public API
    |
    v
+-----------------------------+
| internal Backend contract   |
+--------------+--------------+
               |
       +-------+--------+-------------------+----------------+
       |                |                   |                |
       v                v                   v                v
   loopback          simulator          UDP/VSPW-TP      future HW
```

## Mandatory backend operations

The internal backend contract covers:

- link start;
- link stop;
- reset;
- link-state query;
- capability query;
- copied packet send/receive;
- time-code send/receive;
- common statistics query/reset.

Optional ownership-oriented hooks cover zero-copy TX/RX when a backend advertises `SPW_CAP_ZERO_COPY`.

Backend-specific configuration and construction remain outside the virtual `Backend` operation interface and are handled by the port factory/configuration layer.

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

Backends may have different implementation strategies, but portable and embedded paths must be able to use caller-owned or statically allocated storage where their platform contract requires it. The v0.1 loopback path intentionally uses fixed-capacity storage and is the no-heap reference path.

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

It is not a SpaceWire network simulator and does not reproduce exchange-level timing. Its purpose is to validate the backend abstraction, ownership/error semantics, packet boundaries, EOP/EEP handling, bounded resources, time codes, statistics and reset behavior deterministically.

For loopback:

- `start()` moves directly to `SPW_LINK_RUN`;
- `stop()` moves to `SPW_LINK_READY`;
- `reset()` clears pending queues and moves to `SPW_LINK_ERROR_RESET`.

## Process-local simulator backend

The v0.1 simulator is implemented as an equal-peer local virtual link. It adds:

- A/B endpoint pairing by `link_id`;
- `CONNECTING`/`RUN` peer lifecycle behavior;
- peer stop/reset/close/reopen recovery;
- independent bounded packet/time-code queues;
- finite and infinite waits using hosted synchronization;
- zero-copy ownership emulation with fixed host-memory buffers.

It is the primary executable behavioral reference for v0.1.

## Distributed UDP backend

Current `main` contains the initial v0.2 VSPW-TP/UDP backend. It implements the same internal contract while translating copied packet/time-code operations into versioned VSPW-TP datagrams.

The backend currently provides:

- POSIX IPv4 UDP transport;
- bounded packet fragmentation/reassembly;
- EOP/EEP preservation across fragments;
- time-code transfer;
- timeouts and statistics;
- a 1 MiB backend packet/reassembly limit;
- a default 1200-byte fragment payload.

Transport fragments are never surfaced through `spw_port_receive()`.

ACK/retransmission, peer keepalive/disconnect detection, explicit loss/reordering handling, latency/rate and deterministic fault injection remain v0.2 work.

## Receive capacity

When the next complete packet is larger than caller-provided receive capacity:

- `SPW_ERR_BUFFER_TOO_SMALL` is returned;
- the required payload length is reported;
- the packet terminator is reported;
- caller payload storage is not partially modified;
- the complete packet remains available for a later receive with adequate storage.

This behavior is part of the shared application-visible contract, not a loopback-only convenience.

## Timeouts

Timeout behavior is expressed in the common microsecond timeout type.

Different backends may wait using condition variables, socket polling, RTOS primitives, interrupts, hardware queues or polling loops. The backend mechanism is not part of the public API.

## Testing

The shared contract suite exercises loopback and the local simulator. The distributed UDP backend additionally has transport codec and end-to-end D2D integration coverage and should continue adopting reusable contract cases as v0.2 semantics mature.

The active host/simulator/D2D GitHub Actions workflows verify these paths on every relevant pull request and `main` update.
