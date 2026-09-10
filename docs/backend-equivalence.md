# Backend behavioral equivalence

## Matrix version

**SpWKit backend behavioral-equivalence matrix: v1-r1**

`v1-r1` is the first candidate matrix for the future stable SpWKit 1.x software contract. It versions the behavioral proof itself; it does not claim that the current development branch is already the `v1.0.0` release.

The normative runtime meanings for threading, lifetime, results, timeouts, link state, resources and zero-copy ownership are defined in [`runtime-contract.md`](runtime-contract.md).

## Portability claim

SpWKit's software portability claim is deliberately narrow and executable:

> Application packet/link logic written against the public `spw_port_*` / `spw_buffer_*` contract and validated against the process-local simulator can move to another conforming backend without changing its SpaceWire-facing API or the semantics listed in this matrix.

Backend selection, transport addresses, daemon startup and hardware/provider configuration may change. Socket framing, VSPD, DMA descriptors, vendor SDK calls and FPGA details remain below the public API.

The process-local simulator is therefore a supported development environment, not a placeholder for future hardware.

## Canonical application scenario

The canonical scenario is implemented once in:

- `tests/contract/contract_suite.cpp`;
- entry point `spwkit::test::run_backend_contract()`.

Backend fixtures only construct and control the environment. The packet/link assertions are shared application logic and contain no backend-name switch.

The same scenario is consumed by:

| Backend family | Fixture / executable evidence |
|---|---|
| SIMULATOR | `tests/contract/simulator_contract.cpp` / `backend_contract_simulator` |
| VSPW-TP / UDP | `tests/contract/udp_contract.cpp` / `backend_contract_udp` |
| Linux DEVICE / VSPD | `tests/contract/device_contract.cpp` / `backend_contract_device` |
| DRIVER / deterministic provider | `tests/reference_driver/driver_contract.cpp` / `backend_contract_driver` |

UDP and DEVICE additionally execute `run_distributed_backend_contract()` for peer loss, replacement and recovery because those environments have a separately observable transport/session lifetime.

## Canonical full-matrix runner

A full Linux hosted build with SIMULATOR, UDP, DEVICE/VSPD, VSPWD and C++ contract fixtures enabled registers one aggregate CTest entry:

```bash
ctest --test-dir build-hosted \
  -R '^backend_equivalence_v1_matrix$' \
  --output-on-failure
```

That aggregate test executes the four backend contract fixtures listed above. A failure in any required backend family fails the matrix.

Individual fixtures remain ordinary CTest tests, so smaller builds can execute only the backend families compiled into that configuration without inventing substitutes for unavailable platforms.

## v1-r1 behavior matrix

Legend:

- **Common**: asserted by `run_backend_contract()` using the same application logic.
- **Distributed**: asserted by `run_distributed_backend_contract()` for peer/session transports.
- **Capability**: asserted only when the public capability is advertised.
- **Environment**: executable evidence exists, but the exact setup is specific to that backend/platform.
- **N/A**: the behavior is not meaningful for that backend/profile and is not emulated merely to fill a table cell.

| Behavior | SIMULATOR | UDP | DEVICE / VSPD | DRIVER reference | Executable evidence |
|---|---|---|---|---|---|
| construct/open and initial reset state | Common | Common | Common | Common | `test_lifecycle` |
| start / RUN observation | Common | Common | Common | Common | `test_lifecycle`, fixture `start_link()` |
| stop / reset and local `INVALID_STATE` | Common | Common | Common | Common | lifecycle/state tests in `contract_suite.cpp` |
| A -> B and B -> A packet transfer | Common | Common | Common | Common | `test_bidirectional_packets` |
| arbitrary binary payloads | Common | Common | Common | Common | bidirectional + large-packet tests |
| EOP / capability-gated EEP | Common | Common | Common | Common | bidirectional and capacity-retention tests |
| zero-length packet | Common | Common | Common | Common | `test_zero_length_packet` |
| representative large packet | Common | Common | Common | Common | `test_large_packet` using advertised maxima |
| undersized receive, no truncation/consumption | Common | Common | Common | Common | `test_receive_capacity_retention` |
| invalid destination does not consume packet | Common | Common | Common | Common | invalid-receive retention test |
| immediate receive / readiness semantics | Common | Common | Common | Common | timeout/readiness tests where applicable |
| finite timeout budget | Common | Common | Common | Common | `test_timeout_and_nonblocking` plus runtime contract |
| infinite wait with later peer progress | Environment | Environment | Environment | provider-dependent | simulator concurrent-link test; transport/process tests use bounded CI waits; the runtime rule is normative without forcing a host blocking primitive into every provider |
| bounded queue/resource exhaustion and recovery | Common | Common where strict | service capacity, not immediate-send depth | Common | `test_bounded_queue`; DEVICE raw/service tests cover daemon capacity |
| time codes | Capability | Capability | Capability | Capability | `test_time_codes` |
| readiness | Capability | Capability | Capability | Capability | `test_readiness` |
| statistics | Capability | Capability | Capability | Capability | `test_statistics` |
| zero-copy acquire/submit/reclaim/release | Capability | unsupported | unsupported | Capability | fixture `run_zero_copy_contract()` |
| reset invalidates pre-reset zero-copy ownership | Capability | N/A | N/A | Capability | shared reset-ownership assertions |
| copied operation after zero-copy ownership use | Environment | N/A | N/A | Environment | simulator and DRIVER zero-copy fixture coverage |
| peer disappearance / replacement / recovery | Environment | Distributed | Distributed | provider-specific | simulator peer close/reopen test; distributed contract for UDP/DEVICE |
| concurrent use of distinct port handles | Environment | Environment | Environment | provider obligation | simulator threaded full-duplex test; UDP/DEVICE independent-process tests; DRIVER synchronization obligation in `driver-backend.md` |
| overlapping operations on one port handle | forbidden precondition | forbidden precondition | forbidden precondition | forbidden precondition | `runtime-contract.md`; tests do not deliberately create C/C++ data races |

## Optional behavior rule

The matrix does not branch on backend names to decide whether an application feature exists. Optional application behavior is gated by `spw_capabilities_t`.

For example, SIMULATOR and the deterministic DRIVER advertise zero-copy and therefore must pass their ownership fixtures. UDP and DEVICE do not currently advertise zero-copy, so the common application scenario skips those assertions rather than pretending that a copy through a socket is DMA ownership.

The same rule applies to readiness, time codes, statistics and other optional capabilities.

## Distributed recovery

Peer disappearance is a portable application concern, but the observation mechanism differs by environment.

- SIMULATOR exposes deterministic peer stop/reset/close/reopen through its shared in-process link.
- UDP observes session/liveness loss and a fresh remote session.
- DEVICE/VSPD observes daemon-side peer disappearance and replacement.
- DRIVER maps whatever the provider can observe from its controller/link into the common state/result vocabulary; the deterministic reference provider is not required to manufacture a hot-plug transport session merely to imitate UDP.

The portable requirement is the resulting link/state/error behavior, not identical internal timing.

## Timing boundary

Behavioral equivalence is not timing equivalence.

A simulator, UDP transport, VSPD service and physical provider can have radically different latency and throughput while preserving the same packet/link semantics. Hosted virtual timing is therefore functional and regression evidence, not a prediction of physical SpaceWire performance.

Likewise, this matrix does not model Data-Strobe signaling, electrical levels, character/FCT/NULL timing, cable behavior or cycle-accurate controller implementation.

## Moving an application from simulator to hardware

Within the matrix contract, application logic remains centered on:

```text
spw_port_start / stop / reset / get_link_state
spw_port_send / receive
spw_port_send_time_code / receive_time_code
spw_port_wait                       when advertised
spw_port_get_statistics             when advertised
spw_port_acquire_* / submit / reclaim / release
                                    when zero-copy is advertised
```

Moving to a DRIVER-backed controller changes the `spw_port_config_t` backend configuration and provider integration. It does not require rewriting packet payload handling, EOP/EEP handling, timeout/result control flow or zero-copy ownership logic that only depends on advertised capabilities.

## Evidence boundary

Passing `v1-r1` proves application-visible software behavior for the tested backend/profile. It is not proof of physical SpaceWire PHY/electrical interoperability, cycle timing, radiation behavior or formal ECSS certification.
