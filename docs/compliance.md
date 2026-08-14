# ECSS scope and compliance policy

SpWKit is designed with ECSS SpaceWire standards as normative references, but **the project does not currently claim ECSS conformance, certification, or qualification**.

That distinction is intentional.

## Primary normative reference

The core SpaceWire behaviour targeted by SpWKit is defined by:

- **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks (15 May 2019)**

Official reference:
<https://ecss.nl/standard/ecss-e-st-50-12c-rev-1-spacewire-links-nodes-routers-and-networks-15-may-2019/>

The revision supersedes ECSS-E-ST-50-12C dated 31 July 2008.

## Related standards

SpWKit may implement optional modules associated with:

- **ECSS-E-ST-50-51C — SpaceWire protocol identification**;
- **ECSS-E-ST-50-52C — SpaceWire — Remote memory access protocol (RMAP)**;
- **ECSS-E-ST-50-53C — SpaceWire — CCSDS packet transfer protocol**.

These protocols sit above the raw SpaceWire link/packet abstraction and should remain modular.

## What 'ECSS-oriented' means in this project

Until formal evidence exists, documentation should use wording such as:

- `based on ECSS-E-ST-50-12C Rev.1`;
- `targets ECSS SpaceWire semantics`;
- `ECSS-oriented behavioural model`;
- `designed against ECSS requirements`.

Documentation should **not** use unqualified claims such as:

- `ECSS compliant`;
- `ECSS certified`;
- `flight qualified`;
- `validated for flight`;
- `conformance tested`.

## Compliance boundary

SpWKit contains or plans several layers with different verification boundaries.

### Portable software API

The API represents application-visible SpaceWire concepts. Its conformance target is semantic consistency with the relevant ECSS concepts, not physical-layer implementation.

Examples:

- EOP and EEP remain distinguishable;
- packet boundaries are preserved;
- link management operations have documented mappings;
- time codes are not treated as packet payloads;
- backend-specific details do not alter protocol semantics.

### Virtual simulator

The software simulator is intended to be packet-accurate and optionally behaviourally accurate at the link-management level.

It is **not** intended to prove:

- electrical compliance;
- LVDS levels or margins;
- Data-Strobe waveform correctness;
- codec clocking behaviour;
- metastability robustness;
- FPGA timing closure;
- cable compliance.

### Physical FPGA/ASIC implementation

A physical codec implementation has a broader conformance burden, including signal, character, exchange, packet, and possibly network-level requirements depending on implemented scope.

Verification of a physical core should be maintained separately from the portable software simulator.

## Requirements traceability

Before SpWKit makes any formal conformance claim, the project should maintain a requirements matrix derived from the implemented subset of the applicable ECSS standard.

Suggested structure:

| Requirement ID | ECSS clause | Component | Verification method | Test/evidence | Status |
|---|---|---|---|---|---|
| TBD | TBD | virtual link | test | TBD | planned |

The matrix should distinguish:

- applicable requirements;
- non-applicable physical requirements for the software simulator;
- requirements delegated to hardware;
- optional features;
- tailoring decisions.

## Verification methods

Depending on the component, acceptable evidence may include:

- unit tests;
- integration tests;
- deterministic simulator tests;
- property/assertion testing;
- HDL simulation;
- static analysis;
- timing analysis;
- loopback hardware tests;
- interoperability testing against independent SpaceWire equipment;
- protocol trace inspection.

## Simulator fidelity declaration

Every simulator mode should declare what it models.

### Packet mode

Expected scope:

- packet transfer;
- EOP/EEP;
- link availability;
- time codes;
- queue limits;
- modeled throughput/latency;
- controlled error injection.

### Behavioural link mode

Potential additional scope:

- link-state transitions;
- modeled receive credit;
- flow-control effects;
- disconnect handling;
- finite buffering;
- character-related fault events.

### HDL / physical verification

Required separately for:

- Data-Strobe encoding;
- character encoding and parity;
- NULL/FCT generation and reception;
- exact exchange-level timing;
- electrical interface requirements.

## Compliance claim policy

Any future claim of conformance should identify:

1. the exact ECSS standard and revision;
2. the implemented feature subset;
3. any tailoring or exclusions;
4. the verification configuration;
5. the hardware/software version tested;
6. the evidence package or traceability matrix.

A blanket statement such as `SpWKit is ECSS compliant` is not acceptable because different backends and simulator modes cover different portions of the stack.

## Copyright and standards text

SpWKit documentation should summarize and reference ECSS requirements rather than reproduce large portions of copyrighted standards text. Contributors should link to the official ECSS source and use project-owned wording for explanations, mappings, and implementation guidance.
