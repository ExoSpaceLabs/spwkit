# ECSS-E-ST-50-12C Rev.1 software traceability

## Record

- Standard: **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks**
- Standard date: 15 May 2019
- SpWKit release target: `v0.7.0`
- Matrix revision: `v0.7-r1`
- Scope: public SpWKit software/runtime abstraction and the virtual/reference-provider evidence shipped by this repository
- Hardware companion: `ExoSpaceLabs/spwkit-fpga` for the ExoSpaceLabs FPGA/controller/PHY implementation

This is a project-owned applicability and traceability record. Requirement summaries below are paraphrases; the ECSS document remains normative.

Classification:

- **Software verified**: applicable to the SpWKit software abstraction, implemented, and backed by reproducible repository evidence.
- **Provider/hardware delegated**: the behavior belongs below the generic DRIVER boundary and must be evidenced by the concrete controller/PHY provider.
- **Not applicable**: outside the endpoint/link scope of SpWKit.
- **Not implemented / future**: relevant to a possible software capability but not currently exposed as a supported SpWKit contract.

## Software-verified requirements

| ID | ECSS clause | Project-owned requirement summary | SpWKit surface | Verification evidence | Classification |
|---|---|---|---|---|---|
| SW-SPW-001 | 5.6.2.1 a,c | Preserve a SpaceWire packet as one data unit terminated explicitly by EOP or EEP. | `spw_packet_t`, `spw_terminator_t`, `spw_port_send`, `spw_port_receive` | `test_bidirectional_packets`, `test_receive_capacity_retention`; `backend_equivalence_v1_matrix` | **Software verified** |
| SW-SPW-002 | 5.6.2.1 d | Represent and transfer a packet with zero data characters without inventing payload data. | zero-length `spw_packet_t` | `test_zero_length_packet`; common backend contract | **Software verified** |
| SW-SPW-003 | 6.1.1 | Provide application-facing packet send/receive service semantics with the packet and its EOP/EEP indication preserved. | copied packet API | common `run_backend_contract()` across SIMULATOR, UDP, DEVICE/VSPD and DRIVER | **Software verified** |
| SW-SPW-004 | 5.6.3 a | Encode the supported time-code specialization with the broadcast-code type/control field fixed to the permitted time-code value. | `spw_time_code_t.control_flags`; public validation | `test_time_codes`, public edge-case validation | **Software verified** |
| SW-SPW-005 | 5.6.4.2 a | Restrict the SpaceWire time-code value to the six-bit time-count range. | `spw_time_code_t.value`; public validation | `test_time_codes`, public edge-case validation | **Software verified** |
| SW-SPW-006 | 6.1.2 | Provide application-facing time-code send/receive service semantics when the backend advertises time-code capability. | `SPW_CAP_TIME_CODE`, `spw_port_send_time_code`, `spw_port_receive_time_code` | capability-gated `test_time_codes`; backend-equivalence matrix | **Software verified** |
| SW-SPW-007 | 5.6.2 / 6.1.1 | Preserve complete packet boundaries and payload bytes across supported software backends without silent truncation. | packet API and backend contract | `test_bidirectional_packets`, `test_large_packet`, `test_receive_capacity_retention` | **Software verified** |
| SW-SPW-008 | 5.6.2 / 6.1.1 | Keep application-visible packet semantics backend-neutral so transport framing does not change EOP/EEP or payload interpretation. | SIMULATOR, VSPW-TP/UDP, DEVICE/VSPD, DRIVER | `backend_equivalence_v1_matrix` and `docs/backend-equivalence.md` | **Software verified** |

The release conformance statement is limited to the rows classified **Software verified**. The rows above do not assert physical N-Char generation, physical link timing, routing-switch behavior, or electrical compliance.

## Delegated requirements

| ECSS clause | Area | SpWKit responsibility | Concrete-provider responsibility | Classification |
|---|---|---|---|---|
| 5.3 | Physical layer | Define no provider-specific electrical implementation in the generic runtime. | Cable/connector/electrical characteristics and physical signalling. | **Provider/hardware delegated** |
| 5.4 | Encoding layer | Preserve higher-level packet/control semantics presented by the provider. | Data-Strobe encoding, serialization/deserialization, parity, Null/FCT/control-character encoding, disconnect detection and signalling rate. | **Provider/hardware delegated** |
| 5.5.3 | Data-link management interface | Expose portable start/stop/reset/link-state operations. | Map those controls to a conforming physical link implementation and management parameters. | **Provider/hardware delegated** |
| 5.5.4-5.5.6 | Flow control, flow-control errors and send priority | Do not contradict provider-reported packet/control behavior at the application boundary. | Credit/FCT accounting, character priority and Null/FCT/N-Char scheduling. | **Provider/hardware delegated** |
| 5.5.7 | Link initialisation | Expose lifecycle intent and portable observable state. | Implement the ECSS link-initialisation state machine and its physical timing/character conditions. | **Provider/hardware delegated** |
| 5.5.8 | Link error recovery | Provide reset/recovery semantics to the application and map provider state/errors. | Implement ECSS character/link error detection and recovery behavior. | **Provider/hardware delegated** |
| 5.5.9 | Broadcast-code acceptance for sending | Expose supported time-code requests through the public API. | Arbitrate and transmit broadcast codes at the required data-link priority. | **Provider/hardware delegated** |
| 5.6.2.2 | N-Char interleaving | Preserve atomic packet units in the software service. | Ensure the emitted physical N-Char stream obeys the ECSS non-interleaving rule. | **Provider/hardware delegated** |
| 6.2 | Data-link service interface | DRIVER callbacks form the portable software/provider boundary. | Implement the corresponding N-Char/broadcast-code service at the real controller boundary as required by the provider architecture. | **Provider/hardware delegated** |
| 6.3 | Encoding service interface | No generic encoding-layer implementation is claimed. | Implement and verify encoding/decoding service behavior. | **Provider/hardware delegated** |
| 6.4 | Physical-layer service interface | No generic physical-layer implementation is claimed. | Implement and verify line transmit/receive behavior. | **Provider/hardware delegated** |

For the ExoSpaceLabs hardware path, these rows are owned by `spwkit-fpga` to the extent that project implements the corresponding controller/PHY function. A different DRIVER provider owns its own evidence.

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
| 5.6.8 | Routing | SpWKit is an endpoint/link toolkit. It may communicate through an external SpaceWire router, but it does not implement generic routing-switch forwarding. | **Not applicable** |
| 5.6.9 | Routing-switch management parameters | Router implementation and router management are outside the core project scope. | **Not applicable** |
| routing-switch-specific parts of 5.6.4 and 5.6.6 | Router/node topology behavior | SpWKit's software claim is limited to endpoint-facing behavior it implements; router-specific requirements are not claimed. | **Not applicable** |

## Evidence execution

The primary release evidence is the common backend contract:

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

## Release review rules

Before this matrix is used in a release claim:

1. verify the exact ECSS revision is still the project target;
2. review every **Software verified** row against the normative ECSS text;
3. run the cited release-candidate evidence;
4. do not promote a delegated or future row to software-verified without implementation and reproducible evidence;
5. record any tailoring or limitation in the release notes;
6. do not call this evidence certification, qualification, or physical SpaceWire compliance.

The intended `v0.7.0` wording is therefore scoped: **SpWKit v0.7.0 conforms to the ECSS-E-ST-50-12C Rev.1 requirements identified as Software verified in this matrix for the tested software abstraction.** Full physical SpaceWire conformity for a concrete system additionally requires a conforming physical provider.