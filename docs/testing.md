# Testing and CI strategy

SpWKit verifies one observable public contract across every backend. Unit tests cover implementation details; contract tests cover application-visible semantics; simulator tests cover paired-link behavior; distributed tests cover VSPW-TP/UDP behavior; later device/embedded/HIL layers must reuse the same public contract rather than invent parallel semantics.

## Active gates

### Hosted matrix (`CI`)

The cross-platform host matrix verifies:

- Linux GCC and Clang;
- Windows MSVC;
- macOS Clang;
- C and C++ public-header consumers;
- public API/unit/contract tests;
- public C examples and optional C++ examples;
- installed-package `find_package(SpWKit)` C consumer;
- installed optional C++ wrapper consumer when enabled;
- ASan + UBSan;
- no-heap C++ development fixture;
- exception/RTTI-free C++ wrapper/test policy.

CI rejects explicit `throw`, `try`, and `catch` syntax in C++ sources. Recoverable library failures remain result-code based.

### Pure-C runtime (`C-only runtime`)

This gate is the authoritative proof that the runtime is not merely C-compatible at the header boundary. It runs with `CXX=/bin/false` and verifies:

- static C11 core with real CTest behavior and C examples;
- no CXX compiler entry in the CMake cache;
- no C++ ABI/runtime symbols in the archive;
- installed C-only consumer using `spwkit::spwkit`;
- complete simulator + UDP runtime build without CXX;
- pure-C two-peer simulator behavior including EOP/EEP and time codes;
- Linux shared `libspwkit.so` installed and consumed by a C-only project;
- no-heap `spw_port_open_in_place()` behavior in C.

`SPWKIT_BUILD_CPP_TESTS` and `SPWKIT_BUILD_CPP_EXAMPLES` are separate switches so `SPWKIT_BUILD_TESTS=ON` and `SPWKIT_BUILD_EXAMPLES=ON` do not force C++ into a C-only project.

### Simulator

The dedicated Simulator workflow enables the process-local simulator and runs both simulator-labelled tests and the reusable public backend contract. It complements the pure-C simulator test with deeper C++ development fixtures for threading, edge cases, zero-copy ownership and shared contract behavior.

### Device-to-device

The D2D workflow runs the real VSPW-TP/UDP integration tests and `backend_contract_udp`. It installs SpWKit, then builds `examples/distributed` as a **separate C-only** `find_package(SpWKit 0.3)` consumer with `CXX=/bin/false`.

It executes both:

- two independent peer processes with disconnect/new-session restart;
- two Linux network namespaces connected by a 1500-byte-MTU veth pair.

The namespace scenario validates actual IP-interface isolation while the 8 KiB logical packet forces VSPW-TP fragmentation/reassembly.

### Embedded portability

The Embedded portability workflow is a real build, not a green placeholder for missing target files. It compiles the portable static core with:

- C only;
- `SPWKIT_ENABLE_HEAP=OFF`;
- simulator/UDP disabled;
- `-ffreestanding -fno-builtin`;
- warnings as errors;
- no hosted thread/socket/C++ runtime symbol references.

This is portability evidence only. ARM/HardRT target execution remains future v0.5 work and must be reported separately when actual cross toolchains/targets exist.

### Tooling

The Tooling workflow is deliberately separate from runtime/library gates. It installs tshark only inside its Ubuntu job, generates a deterministic Ethernet/IPv4/UDP PCAP, loads `tools/wireshark/vspw_tp.lua` through tshark's real Lua dissector engine, and verifies filterable VSPW-TP DATA fragments, KEEPALIVE, ACK/session binding, TIME_CODE fields, unsupported-version handling, and heuristic rejection of unrelated UDP.

Wireshark/tshark remain development dependencies only; nothing from this gate is linked into `libspwkit`.

### HIL

The HIL workflow remains explicit/manual and must fail if someone claims hardware is ready without the required harness. No hosted workflow result is presented as physical SpaceWire evidence.

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
c
cpp
wrapper
```

Future device/physical/standards work may additionally use `device`, `embedded`, `hil`, and `compliance` as evidence categories.

## C versus C++ test policy

Runtime behavior belongs to C and must have a meaningful pure-C verification path. C++ tests are still valuable for richer development fixtures, historical transport vectors and optional wrapper behavior, but they are not allowed to be the only proof that `libspwkit` works.

The build therefore separates:

```text
SPWKIT_BUILD_TESTS          C test infrastructure / CTest
SPWKIT_BUILD_CPP_TESTS      development-side C++ fixtures
SPWKIT_BUILD_EXAMPLES       C examples
SPWKIT_BUILD_CPP_EXAMPLES   C++ examples
SPWKIT_ENABLE_CPP           installed/public C++ wrapper
```

The optional C++ wrapper is tested against the same C runtime, not against a duplicate backend implementation.

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

## Portable/public edge cases

Portable/public tests intentionally cover failure paths and exact boundaries rather than only nominal transfer:

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

Simulator-specific fixtures additionally cover:

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

## Distributed verification

### Reusable public contract

The reusable UDP public contract creates two localhost ports through the public API and verifies lifecycle, full-duplex copied packets, EOP/EEP, zero-length packets, large packets, no-truncation retry, timeout behavior, bounded resources, time codes, statistics, peer-loss reporting and peer restart recovery.

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

`examples/distributed` is an installed-package **C-only** consumer:

```cmake
project(spwkit_distributed_peer LANGUAGES C)
find_package(SpWKit 0.3 CONFIG REQUIRED)
target_link_libraries(spwkit_udp_peer PRIVATE spwkit::spwkit)
```

The D2D workflow actively rejects a CXX compiler appearing in that consumer's CMake cache.

`tests/d2d/run_multi_process.sh` launches two independent peer processes. A starts first and waits in the public connecting state; B starts later. The peers exchange 8 KiB packets and time codes in both directions, B exits, A must observe `SPW_LINK_ERROR_WAIT`, a new B process/session starts, and both perform a second exchange.

`tests/d2d/run_netns.sh` repeats that scenario in two Linux network namespaces connected by a 1500-byte-MTU veth pair. The namespaces use distinct IPv4 addresses (`10.231.0.1` and `10.231.0.2`), so communication crosses an actual isolated IP interface boundary.

## Capture-tool verification

The VSPW-TP Lua dissector is not trusted merely because its source resembles the protocol table. `tools/wireshark/validate_dissector.py` constructs a deterministic capture using the documented v1 framing and invokes the actual tshark Lua engine. This catches Lua API mistakes, field-width problems, heuristic registration issues, and display-filter regressions without making Wireshark a library dependency.

## Determinism rules

- no test depends on execution order or persistent runner state;
- bounded resources have explicit advertised limits;
- randomized tests must report or configure their seed;
- finite timeout tests use bounded waits and do not claim timing conformance from CI wall-clock timing;
- packet tests include binary data and exact boundary sizes;
- failures should report the operation/result clearly.

## No-heap verification

The no-heap profile builds with `SPWKIT_ENABLE_HEAP=OFF`.

Two complementary fixtures are retained:

- a **pure-C** behavioral test verifies `spw_port_open()` is unavailable while caller-owned `spw_port_open_in_place()` still performs packet I/O correctly;
- the deeper C++ development fixture retains allocation counters around the mandatory in-place lifecycle.

The process-local simulator and POSIX UDP backend are hosted runtime backends, not the bare-metal allocation reference.

## Installed-package verification

Host CI installs SpWKit to a temporary prefix and separately configures C and optional C++ consumers. D2D independently configures the distributed C peer against the installed package.

C consumer:

```cmake
find_package(SpWKit 0.3 CONFIG REQUIRED)
target_link_libraries(app PRIVATE spwkit::spwkit)
```

Optional C++ wrapper consumer:

```cmake
find_package(SpWKit 0.3 CONFIG REQUIRED)
target_link_libraries(app PRIVATE spwkit::cpp)
```

These gates catch broken exports, stale target names, missing installed headers and accidental dependencies on source-private paths.

## v0.4 device verification direction

The Linux virtual-device milestone must add a new integration layer without weakening existing gates. Planned evidence includes:

- versioned daemon/device protocol golden and malformed-message tests;
- daemon process lifecycle and restart;
- C-only installed application through the Linux-device backend;
- optional C++ wrapper application through the same backend;
- `poll()`/non-blocking readiness behavior;
- packet, EOP/EEP, time-code, link-state and statistics parity with existing backends;
- unprivileged Unix-socket fallback in normal CI;
- CUSE `/dev/vspwX` integration where the runner environment permits it.

## ECSS evidence

Automated tests are engineering evidence, not automatic ECSS certification. Applicable ECSS requirements should eventually map to implementation component, test identifier, verification method, result/evidence and software/hardware version.

Electrical, Data-Strobe, exact timing and physical interoperability requirements remain outside the packet-level software simulator and require HDL/electrical/HIL verification.
