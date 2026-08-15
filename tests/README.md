# Test Suite Layout

SpWKit tests are organized by verification level rather than by implementation file.

```text
tests/
├── unit/          Pure core logic and deterministic edge cases
├── contract/      Backend-independent SpaceWire port/API contract
├── integration/   Multi-component tests within one host
├── d2d/           Distributed virtual-link tests
├── embedded/      Cross-build and target-side embedded test harnesses
├── hil/           Physical FPGA/SpaceWire hardware-in-the-loop harness
└── compliance/    Requirement-to-test mapping and generated evidence
```

CTest labels currently used include:

```text
unit contract edge example simulator integration noheap transport d2d
```

`embedded`, `hil`, and `compliance` remain verification-layer labels for the corresponding future runtime/evidence harnesses.

The common backend contract suite is intentionally central. New backends should reuse it rather than create their own interpretation of packet, EOP/EEP, link-state, timeout, ownership, or error semantics.

The current D2D workflow runs the real VSPW-TP/UDP integration test. A Docker Compose or network-namespace topology may be added later for stronger node isolation, but the D2D gate is no longer a staged no-op.

See [docs/testing.md](../docs/testing.md) for execution policy, CI gates, distributed transport testing, and HIL strategy.
