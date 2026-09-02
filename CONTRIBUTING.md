# Contributing

SpWKit is intended to become shared infrastructure for SpaceWire software development. Contributions are welcome, but the portable API must remain independent from any one simulator, operating system, network transport, FPGA family, or vendor SDK.

## Design rules

- Model SpaceWire concepts in the public API, not transport implementation details.
- Keep backend-specific code below the backend boundary.
- Do not claim ECSS conformance without traceability and verification evidence.
- Prefer explicit error returns and deterministic ownership.
- Avoid mandatory heap allocation in portable/embedded code.
- Keep upper-layer protocols such as RMAP modular.
- Add tests together with new protocol-visible behaviour.
- Keep transport behavior such as UDP loss/reordering separate from simulated SpaceWire faults unless an explicit model maps between them.

## Testing requirements

Changes should add or update tests at the lowest meaningful verification layer and, where behaviour crosses a backend boundary, exercise the common backend contract suite or an appropriate integration/D2D fixture.

Use the repository test categories consistently:

- `unit` for isolated portable logic;
- `contract` for backend-independent SpaceWire API behaviour;
- `simulator` / `integration` for local multi-component behaviour;
- `transport` for VSPW-TP framing/validation;
- `d2d` for distributed backend behavior between independent logical peers/processes;
- `embedded` for cross-build and embedded-target verification;
- `hil` for physical FPGA/SpaceWire hardware;
- `compliance` for requirement-linked verification evidence.

The current D2D workflow includes independent processes, Linux network namespaces, and Docker Compose node isolation. Integration-specific Compose fixtures may add stronger deployment-shaped evidence, but the `d2d` label is about independent logical peers rather than any single orchestration mechanism.

Do not weaken or bypass a test because a backend behaves differently. If the difference represents a legitimate optional capability, express it through the capability model and make test applicability explicit.

See [docs/testing.md](docs/testing.md) and [tests/README.md](tests/README.md).

## Commit style

Use Conventional Commits with the project scope, for example:

```text
feat(spwkit): add virtual packet terminator model
fix(spwkit): preserve eep across udp transport
docs(spwkit): document link state mapping
test(spwkit): cover bounded receive queues
```

## Licensing

By contributing, you agree that your contribution is provided under the repository's Apache-2.0 license.

Use SPDX identifiers in source files where practical:

```text
SPDX-License-Identifier: Apache-2.0
```

## Standards references

Prefer links to official ECSS publications. Summarize requirements in project-owned language rather than copying substantial standard text into the repository.

## Pull requests

Keep changes narrowly scoped and explain:

- what layer is affected;
- whether public API semantics change;
- applicable ECSS concepts/clauses when relevant;
- how the change was verified;
- which CI/test categories are affected;
- whether simulator, distributed and physical backends are expected to behave differently.
