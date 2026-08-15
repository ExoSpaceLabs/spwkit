# Testing and CI strategy

SpWKit verifies one observable public contract across every backend. Unit tests cover implementation details; contract tests cover application-visible semantics; simulator tests cover paired-link behavior; distributed tests cover VSPW-TP/UDP behavior; later HIL will run the same contract against physical hardware.

## Active host gates

The always-on host CI verifies:

- Linux GCC and Clang;
- Windows MSVC;
- macOS Clang;
- C and C++ public-header consumers;
- public API/unit/contract tests;
- public C and C++ examples;
- installed-package `find_package(SpWKit)` consumer;
- ASan + UBSan;
- dedicated no-heap/allocation-free profile;
- exception/RTTI-disabled `spwkit` library compilation.

CI also rejects explicit `throw`, `try`, and `catch` syntax in C++ sources. Recoverable library failures must remain result-code based.

The dedicated Simulator workflow builds with the local simulator enabled and runs simulator-labelled and public contract tests.

The Device-to-device workflow is now active. It configures/builds the repository on Linux and runs the real VSPW-TP/UDP integration test rather than a staged placeholder.

The Embedded workflow currently validates the target/toolchain harness where available; missing cross toolchains are reported and the corresponding build steps remain staged rather than being misrepresented as runtime embedded verification.

## Test labels

Current CTest labels include:

```text
unit
contract
edge
example
simulator
integration
noheap
transport
d2d
```

Future physical/standards work may additionally use `embedded`, `hil`, and `compliance` as runtime evidence categories.

## Shared backend contract

The reusable public backend contract verifies, as applicable:

- lifecycle and observable link state;
- packet transfer;
- EOP/EEP preservation;
- zero-length and large packets;
- receive-capacity failure without packet truncation/consumption;
- immediate and finite timeout behavior;
- bounded resources and recovery;
- time codes when advertised;
- statistics when advertised;
- zero-copy ownership when advertised.

Capabilities gate genuinely optional features. A backend that advertises `SPW_CAP_ZERO_COPY` must execute the zero-copy ownership contract; it cannot silently skip it.

The loopback and local simulator execute the shared contract directly today. Distributed-backend coverage currently combines common public semantics with transport-specific integration tests and should continue converging on the reusable backend contract as v0.2 matures.

## v0.1 edge-case suite

The v0.1 edge tests intentionally exercise failure paths and exact boundaries rather than only nominal transfer.

Portable/public edges include:

- null API arguments;
- invalid config size/version/flags/backend;
- invalid backend configuration on loopback;
- workspace null/undersized/misaligned handling;
- operations before link start;
- invalid terminator, null payload, inconsistent capacity;
- exact maximum packet size and one-byte oversize rejection;
- zero-length packets;
- exact-fit receive;
- `SPW_ERR_BUFFER_TOO_SMALL` required-length and terminator reporting;
- proof that an undersized receive does not modify caller storage or consume the packet;
- bounded packet queue exhaustion/recovery;
- empty receive timeout;
- time-code count/control-field boundaries;
- statistics clear/reset;
- repeated stop/reset lifecycle calls.

Simulator-specific edges additionally include:

- simulator config size/version/endpoint validation;
- duplicate endpoint rejection;
- lone started endpoint remaining `CONNECTING`;
- unavailable-peer send/receive results;
- exact 4096-byte packet boundary and 4097-byte rejection;
- finite packet/time-code timeouts;
- bounded time-code queue;
- `SPW_CAP_ZERO_COPY` consistency;
- zero-copy minimum-capacity rejection;
- complete TX buffer-pool exhaustion and recovery;
- invalid zero-copy metadata;
- foreign-port ownership rejection with ownership preserved;
- submit -> reclaim -> release lifecycle;
- copied TX interoperating with zero-copy RX and vice versa;
- RX metadata immutability;
- peer stop, reset, close, reopen and surviving-handle recovery;
- all 16 local simulator link slots plus deterministic 17th-link exhaustion.

## v0.2 distributed transport tests

The current UDP integration test creates two localhost ports entirely through the public API and verifies:

- VSPW-TP framing through the actual backend;
- a 5 KiB EEP packet fragmented into 512-byte UDP payloads;
- reassembly before the packet becomes application-visible;
- payload and EEP preservation;
- `SPW_ERR_BUFFER_TOO_SMALL` retry without consuming the completed packet;
- reverse-direction packet traffic;
- time-code round trip.

The VSPW-TP codec also has golden-vector and malformed-frame tests covering magic/version/type/header/flag/fragment validation.

Additional v0.2 tests are required as ACK/retransmission, loss/reordering handling, liveness, latency/rate and fault injection are implemented.

## Determinism rules

- no test depends on execution order or persistent runner state;
- bounded resources have explicit advertised limits;
- randomized tests must report their seed;
- finite timeout tests use bounded waits and do not claim timing conformance from CI wall-clock timing;
- packet tests include binary data and exact boundary sizes;
- failures should report the operation/result clearly.

## No-heap verification

The no-heap profile builds with `SPWKIT_ENABLE_HEAP=OFF`. A dedicated test replaces global C++ allocation operations with counters and verifies zero allocations across mandatory in-place open/start/I/O/statistics/close/workspace-reuse operations.

The process-local simulator and POSIX UDP backend are hosted runtime backends, not the bare-metal allocation reference.

## Installed-package verification

Host CI installs SpWKit to a temporary prefix and separately configures `examples/installed` using:

```cmake
find_package(SpWKit 0.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE SpWKit::spwkit)
```

This catches broken exports, missing installed headers and accidental dependencies on source-private paths.

## Embedded and HIL layers

Bare-metal and RTOS work has two levels:

1. cross-build/portability checks on hosted CI;
2. runtime target tests on real targets.

Physical SpaceWire/FPGA HIL is intentionally deferred because a suitable FPGA test platform is not currently available. When hardware exists, it must reuse the same public backend contract and add hardware-specific checks for driver probe, AXI/MMIO, DMA, interrupts, physical link startup, EOP/EEP, time codes, disconnect/recovery and sustained traffic.

## ECSS evidence

Automated tests are engineering evidence, not automatic ECSS certification. Applicable ECSS requirements should eventually map to implementation component, test identifier, verification method, result/evidence and software/hardware version.

Electrical, Data-Strobe, exact timing and physical interoperability requirements remain outside the packet-level software simulator and require HDL/electrical/HIL verification.
