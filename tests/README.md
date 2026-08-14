# Test Suite Layout

SpWKit tests are organized by verification level rather than by implementation file.

```text
tests/
├── unit/          Pure core logic and deterministic edge cases
├── contract/      Backend-independent SpaceWire port/API contract
├── integration/   Multi-component tests within one host
├── d2d/           Independent-node tests using Docker Compose/networking
├── embedded/      Cross-build and target-side embedded test harnesses
├── hil/           Physical FPGA/SpaceWire hardware-in-the-loop harness
└── compliance/    Requirement-to-test mapping and generated evidence
```

CTest labels must mirror these categories where practical:

```text
unit contract simulator integration d2d embedded hil compliance
```

The common backend contract suite is intentionally central. New backends should reuse it rather than create their own interpretation of packet, EOP/EEP, link-state, timing, or error semantics.

See [docs/testing.md](../docs/testing.md) for execution policy, CI gates, Docker topology, and HIL strategy.
