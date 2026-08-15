# Backend Contract Tests

This directory contains the reusable public-API behavioral contract for SpWKit backends.

A backend is not considered compatible merely because it compiles against the internal interface. It must exhibit the same application-visible behavior through `spw_port_*` as every other backend, subject only to explicitly advertised optional capabilities.

## Structure

```text
contract_suite.hpp/.cpp
        |
        +-- shared lifecycle, packet, timeout, queue, statistics,
        |   time-code, capacity and capability assertions
        |
        +-- loopback_contract.cpp      fixture
        +-- simulator_contract.cpp     fixture
        +-- future distributed fixture
        +-- future Linux-device fixture
        +-- future embedded/HIL fixture
```

Backend adapters are deliberately small. They provide endpoint setup, teardown, start, stop and reset behavior. The actual behavioral assertions remain in `contract_suite.cpp` and use only the public SpWKit C API.

A loopback backend may map logical endpoints A and B to the same `spw_port_t`. A point-to-point backend such as the local simulator maps them to two distinct peer handles. The shared tests do not depend on that topology detail.

## Mandatory copied-I/O contract

The common suite verifies:

- initial/reset lifecycle and transition to `SPW_LINK_RUN` where applicable;
- A-to-B and B-to-A packet transfer;
- EOP and capability-gated EEP preservation;
- zero-length packets;
- deterministic large-packet transfer up to the advertised backend limit;
- insufficient receive capacity without packet consumption or silent truncation;
- immediate and finite receive timeout behavior;
- bounded queue exhaustion and recovery when queue depth is advertised;
- capability-gated time-code transfer;
- capability-gated statistics progression;
- backend/capability profile reporting in test output.

## Optional capabilities

Optional tests are selected from `spw_capabilities_t`.

The v0.1 process-local simulator advertises `SPW_CAP_ZERO_COPY` and the shared contract verifies its ownership-oriented behavior, including acquire/fill/submit/reclaim/release, RX acquire/release, capacity/alignment constraints, pool exhaustion, ownership preservation on failed operations, and copied/zero-copy interoperability.

This rule is intentional: unsupported optional features are skipped explicitly, while mandatory copied SpaceWire packet semantics cannot be redefined by an adapter.

## Distributed backend

The initial VSPW-TP/UDP backend is verified today by codec unit tests plus an end-to-end D2D integration test. As ACK/liveness/loss-recovery semantics mature, a dedicated distributed fixture should reuse as much of this common suite as possible rather than duplicating the contract in transport-specific tests.

## CTest

The shared contract tests use the `contract` label:

```bash
ctest --test-dir build -L contract --output-on-failure
```

With simulator support enabled, both loopback and the local virtual peer backend run the common suite. Future distributed, embedded and hardware-in-the-loop fixtures should reuse the same behavioral assertions and add only capability/profile-specific setup.
