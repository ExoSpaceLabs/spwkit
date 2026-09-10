# ECSS SpaceWire conformance boundary

## Target standard

SpWKit targets the requirements of **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks** that are applicable to the public software/runtime abstraction implemented by this repository.

The project does **not** use a blanket statement that it makes no ECSS conformance claim. Instead, conformance is claimed requirement by requirement where the requirement is applicable to SpWKit software and supporting evidence exists.

The current project-owned release traceability record is [`../tests/compliance/ecss-e-st-50-12c-rev1.md`](../tests/compliance/ecss-e-st-50-12c-rev1.md). The v0.7.0 release gate is tracked by #224.

## Claim model

Each relevant ECSS requirement is classified into one of four states:

| Classification | Meaning |
|---|---|
| **Software verified** | The requirement is applicable to SpWKit software, is implemented, and has executable or otherwise reproducible evidence. SpWKit may claim conformance for this requirement. |
| **Provider/hardware delegated** | The public SpWKit contract exposes or maps the behavior, but the concrete controller/FPGA/PHY/electrical implementation must satisfy and evidence the requirement. |
| **Not applicable** | The requirement is outside the endpoint/link software scope of this repository. |
| **Not implemented / future** | The requirement is applicable to a possible SpWKit capability but is not currently implemented; no conformance claim is made for it. |

Only **Software verified** rows form the SpWKit software conformance claim.

The traceability record identifies the ECSS clause, project-owned requirement summary, classification, SpWKit implementation surface, executable/documented evidence, limitations, and release target. The normative ECSS text is not copied into the repository.

## Software responsibility

SpWKit owns the software-facing behavior that applications and hardware providers rely on. Applicable verified areas include:

- preservation of complete packet boundaries and arbitrary payload data;
- explicit EOP and EEP packet termination semantics;
- zero-length packet representation at the endpoint service boundary;
- packet send/receive service semantics;
- six-bit time-code values and the permitted time-code type/control field;
- time-code transmit/receive semantics when the capability is advertised;
- portable lifecycle, error, timeout and state mapping required to expose provider behavior consistently;
- backend-neutral application packet/link semantics.

The exact conformance claim is defined by the rows classified **Software verified** in the traceability record. Broader runtime features are not promoted into ECSS claims merely because SpWKit tests them.

## Hardware/provider responsibility

A concrete physical provider owns requirements that depend on its implementation beneath the public DRIVER boundary. For the ExoSpaceLabs hardware path, the private `spwkit-fpga` project is responsible for claiming and evidencing the FPGA/controller/PHY side that it implements.

Depending on the provider, delegated evidence can include:

- Data-Strobe signalling and encoding;
- electrical levels and signal quality;
- connectors, cables and physical interconnect;
- character-level implementation, including control characters, NULLs and FCT handling;
- physical link initialization/state-machine timing implemented in hardware;
- flow-control implementation below the software abstraction;
- physical link rate/timing requirements;
- FPGA timing closure and controller implementation details;
- interoperability against an independent physical SpaceWire endpoint.

A generic SpWKit release cannot claim those properties for an arbitrary provider. Conversely, their delegation does not justify disclaiming software requirements that SpWKit can implement and verify itself.

## Combined end-to-end claim

The intended composition is:

```text
application
    |
SpWKit public software contract
    |   ECSS requirements classified as Software verified
    |
SPW_BACKEND_DRIVER
    |
physical provider / spwkit-fpga
        ECSS requirements classified as Provider/hardware delegated
```

A full end-to-end SpaceWire conformance statement for a concrete system requires evidence from **both** applicable layers. SpWKit does not automatically inherit the provider's hardware claim, and a provider does not automatically inherit SpWKit's software evidence without using the conforming public contract.

## Virtual backends

SIMULATOR, VSPW-TP/UDP and DEVICE/VSPD are software-development and verification environments. They preserve the applicable application-visible SpaceWire semantics covered by the software contract.

They do not reproduce physical Data-Strobe signalling, cable/electrical behavior, physical character timing or a particular FPGA implementation. That distinction affects which ECSS requirements their evidence can support; it does not make their verified packet/link software semantics non-conformant by definition.

## Current explicit exclusions

The v0.7 traceability record does not claim:

- distributed-interrupt service support;
- a complete ECSS SpaceWire MIB service;
- generic routing-switch implementation or router management;
- physical/encoding/data-link-engine requirements delegated to a concrete provider.

Those exclusions are recorded rather than hidden behind a blanket project disclaimer.

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
