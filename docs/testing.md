# Testing and CI strategy

SpWKit verifies one observable public contract across every backend. Unit tests cover implementation details; contract tests cover application-visible semantics; simulator tests cover paired-link behavior; later HIL will run the same contract against physical hardware.

## Active v0.1 gates

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

The dedicated Simulator workflow builds with the simulator enabled and runs simulator-labelled and public contract tests.

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
```

Later phases additionally use `d2d`, `embedded`, `hil`, and `compliance`.

## Shared backend contract

The reusable public backend contract verifies, as applicable:

- lifecycle and observable link state;
- bidirectional packet transfer;
- EOP/EEP preservation;
- zero-length and large packets;
- receive-capacity failure without packet truncation/consumption;
- immediate and finite timeout behavior;
- bounded queues and recovery after drain;
- time codes when advertised;
- statistics when advertised;
- zero-copy ownership when advertised.

Capabilities gate genuinely optional features. A backend that advertises `SPW_CAP_ZERO_COPY` must execute the zero-copy ownership contract; it cannot silently skip it.

## Edge-case suite

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

This list is the current v0.1 boundary set, not a claim that arbitrary future backend-specific failure modes are already exhaustively known. New backends must extend the shared suite when they introduce new capabilities or resource constraints.

## Determinism rules

- no test depends on execution order or persistent runner state;
- bounded resources have explicit advertised limits;
- randomized tests must report their seed;
- finite timeout tests use bounded waits and do not claim timing conformance from CI wall-clock timing;
- packet tests include binary data and exact boundary sizes;
- failures should report the operation/result clearly.

## No-heap verification

The no-heap profile builds with `SPWKIT_ENABLE_HEAP=OFF`. A dedicated test replaces global C++ allocation operations with counters and verifies zero allocations across mandatory in-place open/start/I/O/statistics/close/workspace-reuse operations.

The process-local simulator is a hosted runtime reference backend, not the bare-metal allocation reference.

## Installed-package verification

Host CI installs SpWKit to a temporary prefix and separately configures `examples/installed` using:

```cmake
find_package(SpWKit 0.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE SpWKit::spwkit)
```

This catches broken exports, missing installed headers and accidental dependencies on source-private paths.

## Future D2D, embedded and HIL layers

Distributed virtual SpaceWire will be tested with independent processes/network namespaces, preferably Docker Compose, once the Ethernet/IPC backend exists.

Bare-metal and RTOS work has two levels:

1. cross-build/portability checks on hosted CI;
2. runtime target tests on real targets.

Physical SpaceWire/FPGA HIL is intentionally deferred for v0.1 because a suitable FPGA test platform is not currently available. When hardware exists, it must reuse the same public backend contract and add hardware-specific checks for driver probe, AXI/MMIO, DMA, interrupts, physical link startup, EOP/EEP, time codes, disconnect/recovery and sustained traffic.

## ECSS evidence

Automated tests are engineering evidence, not automatic ECSS certification. Applicable ECSS requirements should eventually map to implementation component, test identifier, verification method, result/evidence and software/hardware version.

Electrical, Data-Strobe, exact timing and physical interoperability requirements remain outside the packet-level software simulator and require HDL/electrical/HIL verification.
