# Backend Contract Tests

This directory contains the reusable public-API behavioral contract for SpWKit backends.

A backend is not considered compatible merely because it compiles against the internal interface. It must exhibit the same application-visible behavior through `spw_port_*` as every other backend, subject only to explicitly advertised optional capabilities and fixture-declared timing/environment profiles.

## Structure

```text
contract_suite.hpp/.cpp
        |
        +-- shared lifecycle, packet, timeout, queue, statistics,
        |   time-code, capacity and capability assertions
        |
        +-- distributed_contract.cpp   reusable peer-loss/restart extension
        +-- loopback_contract.cpp      fixture
        +-- simulator_contract.cpp     fixture
        +-- udp_contract.cpp           VSPW-TP/UDP fixture
        +-- future Linux-device fixture
        +-- future embedded/HIL fixture
```

Backend adapters are deliberately small. They provide endpoint setup, teardown, start, stop and reset behavior. The actual behavioral assertions remain in the reusable contract sources and use only the public SpWKit C API.

A loopback backend may map logical endpoints A and B to the same `spw_port_t`. Point-to-point backends such as the local simulator and UDP backend map them to two distinct peer handles. The shared tests do not depend on that topology detail.

## Mandatory copied-I/O contract

The common suite verifies:

- initial/reset lifecycle and transition to `SPW_LINK_RUN` where applicable;
- A-to-B and B-to-A packet transfer;
- EOP and capability-gated EEP preservation;
- zero-length packets;
- deterministic large-packet transfer up to the advertised backend limit used by the common suite;
- insufficient receive capacity without packet consumption or silent truncation;
- immediate and finite receive timeout behavior;
- bounded queue exhaustion and recovery when queue depth is advertised;
- capability-gated time-code transfer;
- capability-gated statistics progression;
- backend/capability/timing profile reporting in test output.

Successful transfer operations use the fixture's `transfer_timeout_us()` profile. The default is `SPW_TIMEOUT_IMMEDIATE`, so loopback and the process-local simulator retain strict immediate-observation behavior. A distributed fixture may provide a finite budget because kernel/network delivery can be asynchronous even when the logical SpaceWire event is valid. Explicit non-blocking and finite-timeout assertions remain fixed by the common suite and are not weakened by this profile.

## Optional capabilities

Optional tests are selected from `spw_capabilities_t`.

The v0.1 process-local simulator advertises `SPW_CAP_ZERO_COPY` and the shared contract verifies its ownership-oriented behavior, including acquire/fill/submit/reclaim/release, RX acquire/release, capacity/alignment constraints, pool exhaustion, ownership preservation on failed operations, and copied/zero-copy interoperability.

The UDP backend does not advertise zero-copy, so the common suite skips that optional contract without any backend-name special case.

This rule is intentional: unsupported optional features are skipped explicitly, while mandatory copied SpaceWire packet semantics cannot be redefined by an adapter.

## Distributed extension

`DistributedBackendContractFixture` extends the shared fixture with reusable peer disconnect/restart hooks and an observation budget for link-state transitions. `distributed_contract.cpp` asserts through public SpWKit operations that:

- a previously running peer loss becomes `SPW_LINK_ERROR_WAIT`;
- service-dependent send reports `SPW_ERR_LINK_UNAVAILABLE` while the peer is absent;
- recreating the peer as a new transport/session incarnation recovers both endpoints to `SPW_LINK_RUN`;
- packet transfer succeeds again after recovery.

The UDP fixture executes both the common contract and this distributed extension. Transport parser, fragmentation/reordering, reliability/fault and timing implementation tests remain separate because those are backend/transport mechanics rather than common application-facing semantics.

The UDP contract fixture keeps the common suite's 4 KiB large packet within one VSPW-TP datagram. Dedicated D2D tests retain responsibility for MTU-scale fragmentation, arbitrary fragment ordering, retry/deduplication, virtual timing and deterministic fault scenarios. This prevents the common contract from accidentally becoming a transport-specific test while still requiring UDP to satisfy the same public behavior.

## CTest

The shared contract tests use the `contract` label:

```bash
ctest --test-dir build -L contract --output-on-failure
```

With simulator and UDP support enabled, loopback, the local virtual peer backend and the VSPW-TP/UDP backend execute the applicable reusable contract. `backend_contract_udp` is also labelled `d2d`, so the dedicated Device-to-device workflow gates distributed public-contract behavior together with the transport integration tests.

Future embedded, Linux-device and hardware-in-the-loop fixtures should reuse the same assertions and add only capability/profile-specific setup plus reusable environment-specific extensions where necessary.
