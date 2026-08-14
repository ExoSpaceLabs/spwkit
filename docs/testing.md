# Testing and CI Strategy

SpWKit uses a layered verification strategy because the project spans portable software, a virtual SpaceWire simulator, distributed device-to-device communication, embedded targets, and eventually physical SpaceWire hardware.

The central rule is simple: **the same observable SpaceWire contract should be exercised at every layer where practical**. Unit tests prove local logic. Simulator and Docker tests prove multi-endpoint behaviour. Hardware-in-the-loop tests prove that a physical backend implements the same contract.

## Test layers

| Layer | Purpose | Execution environment | Required for PRs |
|---|---|---|---|
| Build/portability | Verify that the portable core compiles on supported host toolchains | GitHub-hosted runners | Yes |
| Unit | Validate pure core logic and deterministic edge cases | GitHub-hosted runners | Yes |
| Contract | Run backend-independent API behaviour against each available backend | GitHub-hosted runners / target runner | Yes when backend is available |
| Simulator | Validate one or more `vspw` endpoints and simulator-visible SpaceWire semantics | GitHub-hosted Linux runner | Yes once simulator exists |
| Device-to-device | Validate two independently running nodes across a real virtual network | Docker Compose on GitHub-hosted Linux | Yes once distributed backend exists |
| Embedded | Validate portable/bare-metal build and platform adapters | Cross-compile CI and target runners | Yes for build; runtime where hardware exists |
| HIL | Validate physical FPGA/SpaceWire interface, DMA, interrupts, loopback, and peer transfer | Self-hosted hardware runner | Manual / release gate |
| Compliance evidence | Map verified behaviour to applicable ECSS requirements | Generated from automated test evidence | Release gate for claimed scope |

## CI topology

```text
Pull request / push
        |
        +------------------------------+
        |                              |
        v                              v
+---------------+              +------------------+
| Host CI       |              | Simulator CI     |
| build/unit    |              | vspw behaviour   |
| contract      |              | local endpoints  |
+-------+-------+              +---------+--------+
        |                                |
        |                                v
        |                       +------------------+
        |                       | Docker D2D CI    |
        |                       | node A <-> node B|
        |                       +---------+--------+
        |                                 |
        +----------------+----------------+
                         |
                         v
                  merge eligibility
                         |
                         v
              optional/manual HIL gate
                         |
            +------------+-------------+
            |                          |
            v                          v
     FPGA loopback               two physical nodes
     AXI/DMA/IRQ                 SpaceWire transfer
```

## 1. Host build and unit CI

The always-on workflow is `.github/workflows/ci.yml`.

It should remain fast enough to run on every pull request. It covers:

- CMake configure and build;
- unit tests through CTest;
- backend-independent contract tests that can run without hardware;
- Linux GCC and Clang builds;
- Windows/MSVC build of the portable layer;
- macOS/Clang build of the portable layer;
- AddressSanitizer and UndefinedBehaviorSanitizer on Linux;
- examples when examples become executable.

No test may depend on test execution order or persistent runner state.

### Test labels

CTest labels should be used consistently:

```text
unit
contract
simulator
integration
d2d
embedded
hil
compliance
```

Examples:

```bash
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L contract --output-on-failure
```

## 2. Backend contract tests

The most important reusable test suite is the **port/backend contract suite**.

A backend should be testable through a common fixture implementing operations equivalent to:

```text
create/open port
start link
observe running state
send packet
receive packet
preserve packet boundary
preserve EOP/EEP
send/receive time code when supported
exercise non-blocking/timeout behaviour
read counters/statistics
stop/reset link
recover after link loss when supported
```

The same logical tests should run against:

- in-process loopback;
- local virtual backend;
- Ethernet virtual backend;
- Linux `/dev/vspwX` backend;
- Linux `/dev/spwX` physical backend;
- bare-metal/HardRT backend where target execution is available;
- vendor adapters when contributed.

Capability flags determine which optional cases are applicable. A backend may skip a genuinely unsupported optional capability, but it must never silently change semantics.

## 3. Simulator tests

Simulator tests run the simulator directly on a GitHub-hosted Linux runner.

The initial simulator test profile should include:

1. create two virtual endpoints;
2. establish a virtual link;
3. verify link-state transitions;
4. transfer packets in both directions;
5. verify zero-length and large packets;
6. verify EOP and EEP preservation;
7. verify time-code delivery;
8. exercise queue boundaries;
9. inject deterministic disconnect/error conditions;
10. verify recovery and counters.

Timing-sensitive tests must use tolerances and simulator-controlled clocks where possible. Wall-clock timing on a shared CI VM must not be treated as proof of SpaceWire timing conformance.

The workflow `.github/workflows/simulator.yml` is intentionally staged before implementation. It becomes a required PR check when the simulator target and simulator-labelled CTest cases exist.

## 4. Device-to-device tests with Docker Compose

Distributed virtual SpaceWire must be tested with **independent processes and independent network namespaces**, not only two objects in one process.

Docker Compose is the reference CI mechanism:

```text
+--------------------- Docker network ----------------------+
|                                                           |
|  +----------------+                 +----------------+     |
|  | node-a         |                 | node-b         |     |
|  | application    |                 | application    |     |
|  | SpWKit         |<--- Ethernet -->| SpWKit         |     |
|  | vspw0          |                 | vspw0          |     |
|  +----------------+                 +----------------+     |
|           \                              /                 |
|            +---------- verifier --------+                  |
|                                                           |
+-----------------------------------------------------------+
```

The Compose test must not rely on Docker container startup order as proof that a peer is ready. Endpoints use explicit readiness/handshake logic.

The D2D profile should test:

- A -> B and B -> A packet transfer;
- simultaneous full-duplex traffic;
- fragmentation/reassembly across configured transport MTU;
- multiple packet sizes and deterministic pseudo-random payloads;
- peer restart and reconnect;
- network interruption;
- packet sequencing/duplicate handling if the virtual transport implements it;
- configurable simulated link rate/latency without confusing Ethernet timing with SpaceWire timing.

The planned workflow is `.github/workflows/d2d.yml`. Once `tests/d2d/compose.yaml` and the D2D verifier exist, the workflow should run automatically for changes affecting the virtual/Ethernet transport.

## 5. Embedded build tests

Bare-metal and RTOS support has two distinct test levels.

### Cross-build CI

GitHub-hosted runners can validate:

- freestanding/embedded-compatible headers;
- no accidental POSIX dependency in portable code;
- static-allocation configuration;
- supported cross compiler/toolchain files;
- platform adapter compilation.

Cross-build success is necessary but is **not runtime verification**.

### Runtime target tests

Actual target execution is performed using a self-hosted runner or an attached test controller. The controller flashes the target, captures serial/test output, enforces timeouts, and resets the target between cases.

HardRT should reuse the same backend contract suite where possible, with a thin target-side test harness rather than a separate behavioural specification.

## 6. Hardware-in-the-loop testing

Physical SpaceWire cannot be meaningfully validated on a GitHub-hosted VM. HIL uses GitHub Actions self-hosted runners with explicit labels.

Suggested runner labels:

```text
self-hosted
linux
spw-hil
amd-soc
```

A hardware runner owns the physical lab setup and exposes deterministic scripts for power/reset/programming rather than embedding board-specific shell commands in the workflow.

### HIL profiles

#### `fpga-loopback`

One AMD SoC/FPGA board with the SpaceWire core in internal or cabled loopback.

Verify:

- driver probe;
- AXI4-Lite register access;
- TX/RX DMA;
- interrupt handling;
- EOP/EEP metadata;
- time codes;
- sustained packet traffic;
- reset/recovery;
- counter/error reporting.

#### `two-node`

Two physical SpaceWire endpoints connected by a real SpaceWire link.

Verify:

- link startup;
- bidirectional packet transfer;
- multiple link rates where supported;
- EOP/EEP;
- time codes;
- disconnect/reconnect;
- long-running traffic;
- interoperability when the peer is an independent implementation.

#### `virtual-physical-gateway`

A virtual `vspw` peer communicates over Ethernet with a hardware gateway and then over physical SpaceWire.

```text
CI/test host
  vspw0
    |
 Ethernet
    |
AMD SoC gateway
 /dev/spw0
    |
physical SpaceWire
    |
real endpoint
```

This profile validates one of SpWKit's key architectural claims: software developed against the virtual API can cross into physical hardware without changing the application-level contract.

### Trigger policy

HIL should not run on every untrusted pull request. The initial policy is:

- `workflow_dispatch` only;
- explicit `hardware_ready` confirmation;
- self-hosted runner labels;
- one HIL job per lab resource using GitHub Actions concurrency;
- required for releases that claim physical-backend support;
- optionally scheduled nightly once the lab is sufficiently automated.

## 7. Test data and determinism

Tests use deterministic inputs by default.

- Randomized tests must record and print the seed.
- Fault injection uses explicit seeds/events.
- Packet fixtures should include boundary sizes and non-text binary data.
- Large-packet tests must exercise fragmentation boundaries.
- Timeout tests must define upper bounds and avoid indefinite waits.
- HIL tests reset hardware into a known state before execution.

## 8. Failure artifacts

CI should retain useful evidence when a non-trivial integration or HIL test fails:

- application logs;
- simulator logs;
- test seed;
- configuration/topology file;
- packet trace when available;
- HIL serial logs;
- driver/kernel logs where relevant;
- test summary in machine-readable form.

Do not upload customer, proprietary HDL, or board secrets as public workflow artifacts.

## 9. ECSS verification relationship

Automated tests are engineering evidence, not automatic ECSS certification.

Where an ECSS requirement is within SpWKit's implemented scope, the compliance matrix should eventually reference:

```text
requirement identifier
applicability/tailoring
implementation component
test identifier
verification method
result/evidence artifact
software/hardware version
```

Signal-level, Data-Strobe, electrical, and exact link-timing requirements remain the responsibility of HDL, electrical, and physical interoperability verification rather than `vspw` software tests.

## Merge and release gates

### Pull request gate

Required when implemented:

- host build matrix passes;
- unit tests pass;
- applicable contract tests pass;
- simulator tests pass for simulator changes;
- Docker D2D tests pass for distributed-transport changes;
- no sanitizer failure.

### Release gate

Additionally require:

- full host matrix;
- all simulator and Docker integration profiles;
- target cross-build matrix;
- applicable HIL profile for any physical backend advertised by the release;
- archived verification summary;
- compliance matrix updated for any standards-related behaviour change.

## Current bootstrap status

At the architecture-bootstrap stage only the host build workflow can perform substantive execution. Simulator, D2D, embedded-runtime, and HIL workflow files are intentionally scaffolded with activation guards until their corresponding harnesses exist. They must not be interpreted as passing verification before actual tests are implemented.
