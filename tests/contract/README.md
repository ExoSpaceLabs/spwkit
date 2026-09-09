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
        +-- device_contract.cpp        Linux DEVICE/VSPD fixture
        +-- future embedded/HIL fixture
```

Backend adapters are deliberately small. They provide endpoint setup, teardown, start, stop and reset behavior. The actual behavioral assertions remain in the reusable contract sources and use only the public SpWKit C API.

A loopback backend may map logical endpoints A and B to the same `spw_port_t`. Point-to-point backends such as the local simulator, UDP backend and Linux DEVICE/VSPD fixture map them to distinct peer handles. The shared tests do not depend on that topology detail.

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

The process-local simulator advertises `SPW_CAP_ZERO_COPY` and the shared contract verifies its ownership-oriented behavior, including acquire/fill/submit/reclaim/release, RX acquire/release, capacity/alignment constraints, pool exhaustion, ownership preservation on failed operations, and copied/zero-copy interoperability.

Backends that do not advertise zero-copy skip that optional contract without backend-name special cases.

This rule is intentional: unsupported optional features are skipped explicitly, while mandatory copied SpaceWire packet semantics cannot be redefined by an adapter.

## Distributed extension

`DistributedBackendContractFixture` extends the shared fixture with reusable peer disconnect/restart hooks and an observation budget for link-state transitions. `distributed_contract.cpp` asserts through public SpWKit operations that:

- a previously running peer loss becomes `SPW_LINK_ERROR_WAIT`;
- service-dependent send reports `SPW_ERR_LINK_UNAVAILABLE` while the peer is absent;
- recreating the peer as a new transport/session incarnation recovers both endpoints to `SPW_LINK_RUN`;
- packet transfer succeeds again after recovery.

Distributed fixtures execute the common contract plus the applicable environment-specific extension. Transport parser, fragmentation/reordering, reliability/fault and timing implementation tests remain separate because those are backend/transport mechanics rather than common application-facing semantics.

The UDP contract fixture keeps the common suite's 4 KiB large packet within one VSPW-TP datagram. Dedicated D2D tests retain responsibility for MTU-scale fragmentation, arbitrary fragment ordering, retry/deduplication, virtual timing and deterministic fault scenarios. This prevents the common contract from accidentally becoming a transport-specific test while still requiring UDP to satisfy the same public behavior.

## CTest

The shared contract tests use the `contract` label:

```bash
ctest --test-dir build -L contract --output-on-failure
```

With the relevant features enabled, loopback, the process-local simulator, VSPW-TP/UDP and Linux DEVICE/VSPD execute the applicable reusable contract. Distributed fixtures are also included in the dedicated D2D/device verification paths.

Future embedded and hardware-in-the-loop fixtures should reuse the same assertions and add only capability/profile-specific setup plus reusable environment-specific extensions where necessary.
