# Contributing

SpWKit is intended to become shared infrastructure for SpaceWire software development. Contributions are welcome, but the portable API must remain independent from any one simulator, operating system, FPGA family, or vendor SDK.

## Design rules

- Model SpaceWire concepts in the public API, not transport concepts.
- Keep backend-specific code below the backend boundary.
- Do not claim ECSS conformance without traceability and verification evidence.
- Prefer explicit error returns and deterministic ownership.
- Avoid mandatory heap allocation in portable/embedded code.
- Keep upper-layer protocols such as RMAP modular.
- Add tests together with new protocol-visible behaviour.

## Testing requirements

Changes should add or update tests at the lowest meaningful verification layer and, where behaviour crosses a backend boundary, exercise the common backend contract suite.

Use the repository test categories consistently:

- `unit` for isolated portable logic;
- `contract` for backend-independent SpaceWire API behaviour;
- `simulator` / `integration` for local multi-component behaviour;
- `d2d` for independent nodes communicating through the Docker network harness;
- `embedded` for cross-build and embedded-target verification;
- `hil` for physical FPGA/SpaceWire hardware;
- `compliance` for requirement-linked verification evidence.

Do not weaken or bypass a test because a backend behaves differently. If the difference represents a legitimate optional capability, express it through the capability model and make the test applicability explicit.

See [docs/testing.md](docs/testing.md) and [tests/README.md](tests/README.md).

## Commit style

Use Conventional Commits with a project scope, for example:

```text
feat(spwkit): add virtual packet terminator model
fix(spwkit): preserve eep across ethernet transport
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
- whether simulator and physical backends are expected to behave differently.
