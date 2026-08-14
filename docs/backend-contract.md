# Backend contract

This document defines the internal backend boundary used by `libspwkit`.

Applications do not use backend objects directly. Public SpaceWire operations enter through the SpWKit C/C++ API and are translated by the library to the selected backend.

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
       +-------+--------+-------------------+
       |                |                   |
       v                v                   v
   loopback          simulator          future HW
   test backend      backend            backend
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

Backend-specific configuration and construction are deliberately outside this interface. Public backend selection is tracked separately by the port-configuration work.

## Invariants

Every backend must preserve the software-visible SpWKit contract:

- packet boundaries are never merged or silently split;
- EOP and EEP are preserved;
- oversized receive buffers are never silently truncated;
- optional functions are reflected accurately by capabilities;
- link states map into the common SpaceWire state model;
- common statistics use the same meanings across backends;
- backend implementation types do not leak into public operation signatures.

## Allocation policy

The backend interface itself does not require heap allocation.

Backends may have different implementation strategies, but portable and embedded paths must be able to use caller-owned or statically allocated storage. The v0.1 loopback backend intentionally uses fixed-capacity storage to exercise this model.

## Loopback backend

The first backend is an in-process deterministic loopback used for contract development before the full virtual peer simulator exists.

Its current limits are:

```text
maximum packet size: 4096 bytes
packet queue depth:  8
time-code depth:     8
```

The loopback behavior is intentionally simple:

```text
send(packet)
    |
    v
bounded local packet queue
    |
    v
receive(packet)
```

It is not the final SpaceWire simulator and does not attempt to reproduce exchange-level timing. Its purpose is to validate the backend abstraction, ownership/error semantics, packet boundaries, EOP/EEP handling, bounded resources, time codes, statistics, and reset behavior in a deterministic environment.

### State behavior

The loopback backend begins in `SPW_LINK_ERROR_RESET`.

For this test backend:

- `start()` moves directly to `SPW_LINK_RUN`;
- `stop()` moves to `SPW_LINK_READY`;
- `reset()` clears pending queues and moves to `SPW_LINK_ERROR_RESET`.

The higher-fidelity simulator will implement the complete state-transition behavior tracked separately by the simulator link-state issue.

### Receive capacity

When the next packet is larger than the caller-provided receive capacity:

- `SPW_ERR_BUFFER_TOO_SMALL` is returned;
- the required payload length is reported;
- the packet terminator is reported;
- the packet remains queued for a later receive with adequate storage.

This behavior is deterministic in loopback and is intended to become part of the shared backend contract.

### Timeouts

The loopback backend has no scheduler or asynchronous wait source. Empty immediate or timed receives therefore return `SPW_ERR_TIMEOUT` rather than busy-waiting. Blocking/timed wait behavior will be implemented by backends that have an event source, including the virtual simulator.

## Testing

`tests/loopback_backend.cpp` validates:

- lifecycle and link state;
- advertised capabilities;
- EOP/EEP packet loopback;
- zero-length packets;
- insufficient receive capacity and queue retention;
- bounded queue exhaustion;
- time-code validation and transfer;
- statistics;
- reset and pending-data cleanup.

The test is registered with CTest using the labels:

```text
unit;contract
```

The existing GitHub Actions host CI executes CTest, so the loopback contract is exercised on the supported host build matrix.
