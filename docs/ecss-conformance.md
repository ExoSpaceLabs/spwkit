# ECSS SpaceWire conformance boundary

## Target standard

SpWKit targets the requirements of **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks** that are applicable to the public endpoint/link software abstraction implemented by this repository.

The project does **not** use a blanket statement that it makes no ECSS conformance claim. Instead, conformance is claimed requirement by requirement where the requirement is applicable to SpWKit software and supporting evidence exists.

The current project-owned release traceability record is [`../tests/compliance/ecss-e-st-50-12c-rev1.md`](../tests/compliance/ecss-e-st-50-12c-rev1.md). Its current v0.7 matrix revision is `v0.7-r2`. The v0.7.0 release gate is tracked by #224.

## Claim model

Each relevant ECSS requirement is classified into one of four states:

| Classification | Meaning |
|---|---|
| **Software verified** | The specifically enumerated requirement/subclause is applicable to SpWKit software, is implemented, and has executable or otherwise reproducible evidence. SpWKit may claim conformance for that row. |
| **Provider/hardware delegated** | The public SpWKit contract exposes or maps the behavior, but the concrete node/controller/FPGA/PHY implementation must satisfy and evidence the requirement. |
| **Not applicable** | The requirement is outside the endpoint/link software scope of this repository. |
| **Not implemented / future** | The requirement is applicable to a possible SpWKit capability but is not currently implemented; no conformance claim is made for it. |

Only **Software verified** rows form the SpWKit software conformance claim.

The traceability record identifies the ECSS clause/subclause, project-owned requirement summary, classification, SpWKit implementation surface, executable/documented evidence, limitations, and release target. The normative ECSS text is not copied into the repository.

## Software responsibility

SpWKit owns the software-facing behavior that applications and hardware providers rely on. Applicable verified areas currently include:

- preservation of packet cargo through the endpoint packet service;
- explicit EOP and EEP packet termination semantics;
- zero-length packet representation at the endpoint service boundary;
- packet send-request/receive-indication semantics;
- the represented SpaceWire time-code type and six-bit time-count range;
- the endpoint-facing time-code service primitive when the capability is advertised.

SpWKit also defines portable lifecycle, error, timeout, ownership and backend-equivalence behavior. Those project contracts are important evidence and provider requirements, but they are not promoted into ECSS conformance claims unless a specific ECSS requirement is enumerated in the matrix.

The exact conformance claim is therefore the set of rows classified **Software verified** in the traceability record, not a prose interpretation of this overview.

## Time-code boundary

The generic SpWKit API can represent, validate, send and receive a SpaceWire time-code service event when `SPW_CAP_TIME_CODE` is advertised. That does **not** mean the generic runtime implements a complete ECSS node/router time-code engine.

In particular, the v0.7 software claim does not cover the concrete-node responsibilities associated with the time-code register, master generation, modulo-64 sequence validation, propagation/forwarding, or node/router handling rules in the later 5.6.4 requirements. Those functions belong to the concrete node/controller/provider when applicable.

This distinction prevents an API capable of carrying a six-bit time-code from being mistaken for evidence that every ECSS time-code mechanism behind that API has been implemented. Humanity has suffered enough from interfaces being confused with implementations.

## Hardware/provider responsibility

A concrete physical provider owns requirements that depend on its implementation beneath the public DRIVER boundary. For the ExoSpaceLabs hardware path, the private `spwkit-fpga` project is responsible for claiming and evidencing the FPGA/controller/PHY/node side that it implements.

Depending on the provider, delegated evidence can include:

- Data-Strobe signalling and encoding;
- electrical levels and signal quality;
- connectors, cables and physical interconnect;
- character-level implementation, including control characters, NULLs and FCT handling;
- physical link initialization/state-machine timing implemented in hardware;
- flow-control implementation below the software abstraction;
- physical link rate/timing requirements;
- node time-code register/master/sequence behavior when implemented;
- FPGA timing closure and controller implementation details;
- interoperability against an independent physical SpaceWire endpoint.

A generic SpWKit release cannot claim those properties for an arbitrary provider. Conversely, their delegation does not justify disclaiming software requirements that SpWKit can implement and verify itself.

## Combined end-to-end claim

The intended composition is:

```text
application
    |
SpWKit public software contract
    |   enumerated ECSS requirements classified as Software verified
    |
SPW_BACKEND_DRIVER
    |
physical provider / spwkit-fpga
        remaining applicable ECSS requirements evidenced by provider/system
```

A full end-to-end SpaceWire conformance statement for a concrete system requires evidence from **both** applicable layers. SpWKit does not automatically inherit the provider's hardware claim, and a provider does not automatically inherit SpWKit's software evidence without using the conforming public contract.

## Virtual backends

SIMULATOR, VSPW-TP/UDP and DEVICE/VSPD are software-development and verification environments. They preserve the applicable application-visible SpaceWire semantics covered by the software contract.

They do not reproduce physical Data-Strobe signalling, cable/electrical behavior, physical character timing, the complete concrete-node time-code machinery, or a particular FPGA implementation. Their tests can support the specifically mapped software requirements without pretending that a UDP socket has somehow become an LVDS cable through optimism.

## Current explicit exclusions

The v0.7 traceability record does not claim:

- distributed-interrupt service support;
- a complete ECSS node-management parameter set;
- a complete ECSS SpaceWire MIB service;
- generic routing-switch implementation or router management;
- a complete node/router time-code register/master/sequence/propagation implementation;
- physical/encoding/data-link-engine requirements delegated to a concrete provider.

Those exclusions are recorded explicitly rather than hidden behind a blanket project disclaimer.

## Terminology

Use **conformance** for requirement-by-requirement statements against ECSS-E-ST-50-12C Rev.1.

Do not casually use **qualification** or **certification** as synonyms. Qualification of a concrete product/system can require additional process, environmental, hardware and organizational evidence beyond this repository's software tests.

## Release rule

Before publishing a release that makes an ECSS software-conformance statement:

1. freeze the applicable ECSS revision;
2. update and review the applicability/traceability matrix;
3. ensure every claimed software requirement has reproducible evidence on the release candidate;
4. ensure delegated requirements are clearly assigned to the provider/hardware boundary;
5. remove contradictory blanket disclaimers elsewhere in the documentation;
6. record known unsupported or future requirements explicitly rather than implying them.

For v0.7.0, this work is part of #224 and is a release gate alongside the performance-regression review.
