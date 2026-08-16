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

The Device-to-device workflow configures/builds the repository on Linux and runs the real VSPW-TP/UDP integration tests. It also runs `backend_contract_udp`, installs SpWKit, builds the distributed example as a separate `find_package(SpWKit)` consumer, and executes both two-process and two-network-namespace restart scenarios.

The Tooling workflow is deliberately separate from the runtime/library gates. It installs tshark only inside its Ubuntu CI job, generates a deterministic Ethernet/IPv4/UDP PCAP, loads `tools/wireshark/vspw_tp.lua` through tshark's real Lua dissector engine, and verifies filterable VSPW-TP DATA fragments, KEEPALIVE, ACK/session binding, TIME_CODE fields, unsupported-version handling, and heuristic rejection of unrelated UDP. Wireshark/tshark remain development dependencies only; nothing from this gate is linked into `libspwkit`.

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
timing
fault
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

Successful transfer operations additionally use a fixture-provided transfer-timeout profile. The default remains `SPW_TIMEOUT_IMMEDIATE`, preserving strict local behavior. Distributed fixtures may provide a finite success budget because socket/kernel delivery and transport acknowledgements are asynchronous. Explicit non-blocking and finite-timeout contract tests remain fixed and therefore cannot be weakened by the fixture profile.

The bounded-queue test deliberately keeps queue filling and the full-queue assertion non-blocking so advertised capacity remains observable. After the receiver drains the queue, the subsequent successful recovery send uses the fixture transfer budget: on a reliable distributed backend the application packet may already be drained while its transport ACK is still in flight, so the sender's bounded reliable slot can remain occupied briefly. Local fixtures still use the immediate default.

The loopback, local simulator and VSPW-TP/UDP backend execute the applicable shared contract directly. The UDP fixture is capability-driven and does not require backend-name conditionals; unsupported zero-copy is skipped because the backend does not advertise it.

A reusable distributed-contract extension additionally checks peer loss and restart entirely through public operations: `SPW_LINK_ERROR_WAIT`, `SPW_ERR_LINK_UNAVAILABLE`, session/restart recovery to `SPW_LINK_RUN`, and successful packet transfer after recovery.

The common UDP contract intentionally keeps its 4 KiB large-packet case in one VSPW-TP datagram. Dedicated distributed tests separately verify fragmentation/reassembly, arbitrary ordering, reliability, timing and fault-injection mechanics. The shared contract therefore stays application-facing instead of accumulating UDP implementation details.

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

## v0.2 distributed verification

The UDP verification layer now has three complementary responsibilities.

### Reusable public contract

The reusable public contract creates two localhost ports through the public API and verifies lifecycle, full-duplex copied packets, EOP/EEP, zero-length packets, large packets, no-truncation retry, timeout behavior, bounded resources, time codes, statistics, peer-loss reporting and peer restart recovery.

### Transport-specific D2D tests

Transport-specific tests verify details that must remain invisible to ordinary applications:

- VSPW-TP framing through the actual backend;
- packets larger than the transport MTU fragmented/reassembled before delivery;
- arbitrary-order fragments, exact duplicates and valid overlap handling;
- EOP/EEP and time-code preservation;
- session-bound ACK/retransmission and duplicate suppression;
- peer loss and new-session restart recovery;
- deterministic SpaceWire-side virtual rate/latency behavior;
- deterministic transport drop/duplicate/reorder/delay injection;
- explicit SpaceWire-side EEP injection kept distinct from transport failure.

The VSPW-TP codec also has golden-vector and malformed-frame tests covering magic/version/type/header/flag/fragment validation.

### Process and network isolation

The D2D workflow installs SpWKit to a temporary prefix and builds `examples/distributed` separately with `find_package(SpWKit 0.2 CONFIG REQUIRED)`. This guarantees the process example consumes the installed public package rather than source-private backend interfaces.

`tests/d2d/run_multi_process.sh` launches two independent peer processes. A starts first and waits in the public connecting state; B starts later. The peers exchange 8 KiB packets and time codes in both directions, B exits, A must observe `SPW_LINK_ERROR_WAIT`, a new B process/session starts, and both perform a second exchange.

`tests/d2d/run_netns.sh` repeats that scenario in two Linux network namespaces connected by a 1500-byte-MTU veth pair. The namespaces use distinct IPv4 addresses (`10.231.0.1` and `10.231.0.2`), so communication crosses an actual isolated IP interface boundary. The 8 KiB logical packet therefore exercises VSPW-TP fragmentation/reassembly while the applications remain independent installed-package consumers.

The namespace gate requires `iproute2` and network-administration privilege. GitHub-hosted Ubuntu runners provide passwordless `sudo`, and the D2D workflow actively executes the namespace scenario rather than recording it as a manual-only test.

## Capture-tool verification

The VSPW-TP Lua dissector is not trusted merely because its source resembles the protocol table. `tools/wireshark/validate_dissector.py` constructs a deterministic capture using the documented v1 framing and invokes the actual tshark Lua engine. This catches Lua API mistakes, field-width problems, heuristic registration issues, and display-filter regressions without making Wireshark a library dependency.

The generated capture includes one fragmented logical DATA message, ACK binding, KEEPALIVE, TIME_CODE, an unsupported-version VSPW frame, and unrelated UDP. The validator asserts both positive decode fields and negative heuristic behavior. See `tools/wireshark/README.md` for the capture and display-filter workflow.

## Determinism rules

- no test depends on execution order or persistent runner state;
- bounded resources have explicit advertised limits;
- randomized tests must report or configure their seed;
- finite timeout tests use bounded waits and do not claim timing conformance from CI wall-clock timing;
- packet tests include binary data and exact boundary sizes;
- failures should report the operation/result clearly.

## No-heap verification

The no-heap profile builds with `SPWKIT_ENABLE_HEAP=OFF`. A dedicated test replaces global C++ allocation operations with counters and verifies zero allocations across mandatory in-place open/start/I/O/statistics/close/workspace-reuse operations.

The process-local simulator and POSIX UDP backend are hosted runtime backends, not the bare-metal allocation reference.

## Installed-package verification

Host CI installs SpWKit to a temporary prefix and separately configures the minimal `examples/installed` consumer. D2D CI independently configures the full distributed peer example against an installed package:

```cmake
find_package(SpWKit 0.2 CONFIG REQUIRED)
target_link_libraries(app PRIVATE SpWKit::spwkit)
```

Consumers pinned to v0.1 use `SpWKit 0.1`. These gates catch broken exports, missing installed headers and accidental dependencies on source-private paths.

## Embedded and HIL layers

Bare-metal and RTOS work has two levels:

1. cross-build/portability checks on hosted CI;
2. runtime target tests on real targets.

Physical SpaceWire/FPGA HIL is intentionally deferred because a suitable FPGA test platform is not currently available. When hardware exists, it must reuse the same public backend contract and add hardware-specific checks for driver probe, AXI/MMIO, DMA, interrupts, physical link startup, EOP/EEP, time codes, disconnect/recovery and sustained traffic.

## ECSS evidence

Automated tests are engineering evidence, not automatic ECSS certification. Applicable ECSS requirements should eventually map to implementation component, test identifier, verification method, result/evidence and software/hardware version.

Electrical, Data-Strobe, exact timing and physical interoperability requirements remain outside the packet-level software simulator and require HDL/electrical/HIL verification.
