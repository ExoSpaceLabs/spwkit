# Backend Contract Tests

This directory contains the reusable public-API behavioral contract for SpWKit backends.

The purpose is simple: a backend is not considered compatible merely because it compiles against the internal interface. It must exhibit the same application-visible behavior through `spw_port_*` as every other backend, subject only to explicitly advertised optional capabilities.

## Structure

```text
contract_suite.hpp/.cpp
        |
        +-- shared lifecycle, packet, timeout, queue, statistics,
        |   time-code, capacity and capability assertions
        |
        +-- loopback_contract.cpp      fixture only
        +-- simulator_contract.cpp     fixture only
        +-- future Ethernet fixture
        +-- future Linux-device fixture
        +-- future embedded/HIL fixture
```

Backend adapters are deliberately small. They provide endpoint setup, teardown, start, stop and reset behavior. The actual behavioral assertions remain in `contract_suite.cpp` and use only the public SpWKit C API.

A loopback backend may map logical endpoints A and B to the same `spw_port_t`. A point-to-point backend such as the local simulator maps them to two distinct peer handles. The shared tests do not depend on that topology detail.

## Mandatory copied-I/O contract

The common suite currently verifies:

- initial/reset lifecycle and transition to `SPW_LINK_RUN`;
- A-to-B and B-to-A packet transfer;
- EOP and capability-gated EEP preservation;
- zero-length packets;
- deterministic large-packet transfer up to 4096 bytes or the advertised backend limit;
- insufficient receive capacity without packet consumption or silent truncation;
- immediate and finite receive timeout behavior;
- bounded queue exhaustion and recovery when queue depth is advertised;
- capability-gated time-code transfer;
- capability-gated statistics progression;
- backend/capability profile reporting in test output.

## Optional capabilities

Optional tests are selected from `spw_capabilities_t`.

In particular, zero-copy ownership tests are invoked only when both logical endpoints advertise `SPW_CAP_ZERO_COPY`. The fixture must then provide the zero-copy contract hook. No current v0.1 backend advertises that capability yet; the concrete ownership API and simulator implementation remain tracked by issue #10.

This rule is intentional: unsupported optional features are skipped explicitly, while mandatory copied SpaceWire packet semantics cannot be redefined by an adapter.

## CTest

The shared contract tests use the `contract` label:

```bash
ctest --test-dir build -L contract --output-on-failure
```

With simulator support enabled, both loopback and the local virtual peer backend run the same common suite. Future hardware-in-the-loop runners should reuse this suite unchanged and provide only the hardware fixture plus its capability declaration.
