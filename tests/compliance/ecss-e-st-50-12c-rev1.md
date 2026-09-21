# ECSS-E-ST-50-12C Rev.1 software traceability

## Record

- Standard: **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks**
- Standard date: 15 May 2019
- SpWKit release target: `v0.7.0`
- Matrix revision: `v0.7-r2`
- Scope: public SpWKit endpoint/link software abstraction and the virtual/reference-provider evidence shipped by this repository
- Hardware companion: `ExoSpaceLabs/spwkit-fpga` for the ExoSpaceLabs FPGA/controller/PHY implementation
- Normative review date: 10 September 2026

This is a project-owned applicability and traceability record. Requirement summaries are deliberately paraphrased; the published ECSS document remains normative.

The matrix is intentionally narrower than a claim that SpWKit is a complete SpaceWire node. SpWKit exposes an endpoint/link software service and a portable provider boundary. Requirements that belong to an actual codec, link engine, node time-code engine, router or electrical implementation are not promoted into software claims merely because the public API can carry the resulting event.

Classification:

- **Software verified**: the specifically enumerated ECSS requirement/subclause is applicable to the SpWKit software abstraction, implemented, and backed by reproducible repository evidence.
- **Provider/hardware delegated**: the requirement belongs below the generic DRIVER/service boundary or to a concrete node/controller implementation and must be evidenced by that provider.
- **Not applicable**: outside the endpoint/link scope of SpWKit.
- **Not implemented / future**: relevant to a possible SpWKit software capability but not currently exposed as a supported contract.

## Software-verified requirements

| ID | ECSS clause | Project-owned requirement summary | SpWKit surface | Verification evidence | Classification |
|---|---|---|---|---|---|
| SW-SPW-001 | 5.6.2.1 c | Preserve the packet end indication as EOP or EEP rather than collapsing the two termination outcomes. | `spw_packet_t`, `spw_terminator_t`, copied and zero-copy packet APIs | `test_bidirectional_packets`, `test_receive_capacity_retention`; `backend_equivalence_v1_matrix` | **Software verified** |
| SW-SPW-002 | 5.6.2.1 d | Represent and transfer a packet containing zero data characters without inventing payload data. | zero-length `spw_packet_t` | `test_zero_length_packet`; common backend contract | **Software verified** |
| SW-SPW-003 | 5.6.2.1 f; 6.1.1.2-6.1.1.3 | Carry packet cargo transparently through the endpoint packet service and reproduce the submitted bytes at receive indication. | `spw_port_send`, `spw_port_receive`; zero-copy packet metadata/view API | `test_bidirectional_packets`, `test_large_packet`, copied/zero-copy interoperability tests; backend-equivalence matrix | **Software verified** |
| SW-SPW-004 | 6.1.1.1; 6.1.1.2.1-2; 6.1.1.3.1-2 | Provide endpoint-facing packet send-request and receive-indication semantics carrying the complete SpaceWire packet and its termination indication. | copied packet API; backend-neutral port interface | common `run_backend_contract()` across SIMULATOR, UDP, DEVICE/VSPD and DRIVER | **Software verified** |
| SW-SPW-005 | 5.6.3 a | Represent the supported time-code specialization as a SpaceWire time-code rather than another broadcast-code type. | `spw_time_code_t.control_flags`; public validation | capability-gated `test_time_codes`; invalid time-code edge-case tests | **Software verified** |
| SW-SPW-006 | 5.6.4.2 a | Restrict the represented SpaceWire time-code value to the six-bit time-count range. | `spw_time_code_t.value`; public validation | capability-gated `test_time_codes`; invalid time-code edge-case tests | **Software verified** |
| SW-SPW-007 | 6.1.2.1; 6.1.2.2.1-2 | Provide the endpoint-facing time-code service primitive when the selected backend advertises time-code capability, including a valid six-bit time-code value. | `SPW_CAP_TIME_CODE`, `spw_port_send_time_code`, `spw_port_receive_time_code` | capability-gated `test_time_codes`; backend-equivalence matrix | **Software verified** |

Only the specifically enumerated rows above form the `v0.7.0` SpWKit software-conformance statement. The cited backend-equivalence tests are evidence that supported software backends preserve those semantics; backend equivalence is not treated as an additional ECSS requirement of its own.

The software-verified rows do **not** assert physical N-Char generation, FCT/NULL behavior, link-engine timing, time-code master/register sequencing, routing-switch propagation, electrical compliance, or conformance of an arbitrary hardware provider.

## Provider/hardware-delegated requirements

| ECSS clause | Area | SpWKit responsibility | Concrete-provider/node responsibility | Classification |
|---|---|---|---|---|
| 5.3 | Physical layer | Define no provider-specific electrical implementation in the generic runtime. | Cable/connector/electrical characteristics and physical signalling. | **Provider/hardware delegated** |
| 5.4 | Encoding layer | Preserve the higher-level events presented by the provider. | Data-Strobe encoding, serialization/deserialization, parity, NULL/FCT/control-character encoding, disconnect detection and signalling rate. | **Provider/hardware delegated** |
| 5.5.3 | Data-link management interface | Expose portable start/stop/reset/link-state intent and application-visible results. | Map those controls to a conforming physical link implementation and management parameters. | **Provider/hardware delegated** |
| 5.5.4-5.5.6 | Flow control, flow-control errors and send priority | Do not alter provider-visible packet/control outcomes at the public boundary. | Credit/FCT accounting, character priority and NULL/FCT/N-Char scheduling. | **Provider/hardware delegated** |
| 5.5.7 | Link initialisation | Expose lifecycle intent and portable observable state. | Implement the ECSS link-initialisation state machine and its timing/character conditions. | **Provider/hardware delegated** |
| 5.5.8 | Link error recovery | Provide reset/recovery intent and portable state/error mapping. | Implement character/link error detection and ECSS recovery behavior. | **Provider/hardware delegated** |
| 5.5.9 | Broadcast-code acceptance for sending | Validate and expose supported time-code requests through the public API. | Arbitrate and emit the actual broadcast code at the required data-link priority. | **Provider/hardware delegated** |
| 5.6.2.2 | N-Char interleaving | Preserve atomic packet units at the software service boundary. | Ensure the emitted physical N-Char stream obeys the ECSS packet non-interleaving rule. | **Provider/hardware delegated** |
| 5.6.4.1 b; 5.6.4.3-5.6.4.8 | Node/router time-code engine | Advertise whether the software service is available and preserve valid represented time-code events. | Implement any required time-code register, master generation, modulo-64 sequence validation, node handling, forwarding/propagation and unsupported-time-code behavior for the concrete node/router. | **Provider/hardware delegated** |
| 6.2 | Data-link service interface | DRIVER callbacks form the portable software/provider boundary. | Implement the corresponding N-Char/broadcast-code data-link service at the real controller boundary as required by the provider architecture. | **Provider/hardware delegated** |
| 6.3 | Encoding service interface | No generic encoding-layer implementation is claimed. | Implement and verify encoding/decoding service behavior. | **Provider/hardware delegated** |
| 6.4 | Physical-layer service interface | No generic physical-layer implementation is claimed. | Implement and verify line transmit/receive behavior. | **Provider/hardware delegated** |

For the ExoSpaceLabs hardware path, these rows are owned by `spwkit-fpga` to the extent that project implements the corresponding controller/PHY/node function. A different DRIVER provider owns its own evidence.

The delegation of 5.6.4.3-5.6.4.8 is particularly deliberate. SpWKit can transport a time-code service event, but the generic API does not implement a complete ECSS node time-code register/master/sequence-propagation engine. Carrying a six-bit value is not evidence for all the machinery that may sit behind it.

## Not implemented / future software capabilities

| ECSS clause | Area | Current status | Classification |
|---|---|---|---|
| 5.6.5 / 6.1.3 | Distributed interrupts | No stable public distributed-interrupt API is currently advertised. | **Not implemented / future** |
| 5.6.7 | Node management parameters | SpWKit exposes project-specific configuration/capability/statistics structures, but does not currently claim the complete standardized node-management parameter set. | **Not implemented / future** |
| 5.7 / 6.5 | SpaceWire MIB and MIB service interface | No complete ECSS MIB service is currently exposed as a stable public contract. | **Not implemented / future** |

These rows are explicit exclusions from the `v0.7.0` software-conformance claim; they are not silently treated as conformant.

## Not-applicable core scope

| ECSS clause | Area | Rationale | Classification |
|---|---|---|---|
| 5.6.8 | Routing | SpWKit is an endpoint/link toolkit. It can communicate through an external SpaceWire router, but it does not implement generic routing-switch forwarding. | **Not applicable** |
| 5.6.9 | Routing-switch management parameters | Router implementation and router management are outside the core project scope. | **Not applicable** |
| routing-switch-specific parts of 5.6.4 and 5.6.6 | Router/node topology behavior | SpWKit's software claim is limited to endpoint-facing behavior it implements; router-specific behavior is not claimed. | **Not applicable** |

## Clause-family review coverage

This table records the disposition of the standard areas relevant to the current endpoint/link architecture so the positive claim is not produced by reviewing only convenient clauses.

| Standard area | v0.7 disposition |
|---|---|
| 5.3 physical layer | Provider/hardware delegated |
| 5.4 encoding layer | Provider/hardware delegated |
| 5.5 data-link layer | Provider/hardware delegated below the portable lifecycle/service mapping |
| 5.6.2 packet | Mixed: explicitly enumerated software packet semantics verified; physical N-Char stream behavior delegated |
| 5.6.3-5.6.4 time codes | Mixed: represented type/value and service primitive verified; node/router time-code engine delegated |
| 5.6.5 distributed interrupts | Not implemented / future |
| 5.6.7 node management parameters | Not implemented / future |
| 5.6.8-5.6.9 routing/router management | Not applicable to core endpoint/link scope |
| 5.7 MIB | Not implemented / future |
| 6.1.1 packet service | Software verified for enumerated endpoint packet primitives |
| 6.1.2 time-code service | Software verified for enumerated endpoint service primitive only |
| 6.1.3 distributed-interrupt service | Not implemented / future |
| 6.2 data-link service | Provider/hardware delegated at DRIVER boundary |
| 6.3 encoding service | Provider/hardware delegated |
| 6.4 physical service | Provider/hardware delegated |
| 6.5 MIB service | Not implemented / future |

## Evidence execution

The primary hosted release evidence is the common backend contract:

```bash
ctest --test-dir build-hosted \
  -R '^backend_equivalence_v1_matrix$' \
  --output-on-failure
```

The aggregate executes the shared application-level contract against:

- SIMULATOR;
- VSPW-TP/UDP;
- Linux DEVICE/VSPD;
- deterministic DRIVER/reference provider.

Additional backend-specific tests cover transport/session recovery, zero-copy ownership, malformed arguments and provider behavior. `docs/runtime-contract.md` and `docs/backend-equivalence.md` define the software semantics being verified.

The requirement mapping must be reviewed independently from a passing test suite. A passing project test proves the mapped SpWKit behavior; it does not prove that an incorrect requirement mapping magically became correct.

## Release review rules

Before this matrix is used in a release claim:

1. verify the exact ECSS revision is still the project target;
2. review every **Software verified** row against the normative ECSS text;
3. run the cited evidence on the release candidate;
4. do not promote a delegated or future row to software-verified without implementation and reproducible evidence;
5. record any tailoring or limitation in the release notes;
6. do not call this evidence certification, qualification, or physical SpaceWire compliance.

The intended `v0.7.0` wording is therefore deliberately scoped: **SpWKit v0.7.0 conforms to the specifically enumerated ECSS-E-ST-50-12C Rev.1 requirements/subclauses marked Software verified in this matrix for the tested endpoint/link software abstraction.** Full physical SpaceWire conformity for a concrete system additionally requires a conforming physical provider and evidence for every other applicable requirement at that system boundary.
