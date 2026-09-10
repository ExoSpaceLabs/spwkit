# ECSS scope and compliance policy

SpWKit uses **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks (15 May 2019)** as the normative SpaceWire reference for the portions of the standard applicable to this repository's public software/runtime abstraction.

SpWKit makes **scoped, evidence-backed conformance claims** for applicable requirements that it implements and verifies. It does not make a blanket claim over requirements implemented by a physical controller, PHY, cable, routing switch, or other provider-specific layer.

The detailed claim model is defined in [`ecss-conformance.md`](ecss-conformance.md). The release traceability record is maintained in [`../tests/compliance/ecss-e-st-50-12c-rev1.md`](../tests/compliance/ecss-e-st-50-12c-rev1.md).

## Primary normative reference

- **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks (15 May 2019)**
- Official ECSS source: <https://ecss.nl/standard/ecss-e-st-50-12c-rev-1-spacewire-links-nodes-routers-and-networks-15-may-2019/>

The revision supersedes ECSS-E-ST-50-12C dated 31 July 2008.

Related optional protocols such as ECSS-E-ST-50-51C protocol identification, ECSS-E-ST-50-52C RMAP, and ECSS-E-ST-50-53C CCSDS packet transfer remain separate modules/layers and are not silently included in the core SpWKit conformance claim.

## Conformance boundary

The repository separates requirements into four evidence classes:

- **Software verified**: implemented by SpWKit and backed by reproducible software evidence. These requirements form the positive SpWKit conformance claim.
- **Provider/hardware delegated**: implemented below the generic DRIVER boundary by a concrete controller/PHY provider.
- **Not applicable**: outside the endpoint/link software scope of this repository, such as generic routing-switch implementation.
- **Not implemented / future**: potentially relevant functionality for which SpWKit does not currently claim support.

This is intentionally stricter than either extreme. Saying only that SpWKit is "ECSS-oriented" understates verified software behavior; saying simply that the whole package is "ECSS compliant" would overstate evidence belonging to a concrete physical implementation.

## Software conformance scope

The software claim includes applicable, verified application-visible semantics such as:

- complete packet boundaries and arbitrary data;
- distinct EOP and EEP termination;
- zero-length packet representation at the endpoint service boundary;
- packet send/receive service behavior;
- six-bit SpaceWire time-code values and the permitted time-code type/control field;
- time-code send/receive services when the capability is advertised;
- portable lifecycle, state, timeout and error semantics needed to expose provider behavior consistently;
- backend-neutral packet/link behavior through the common contract.

The exact claim is the set of **Software verified** rows in the traceability matrix, not this prose summary.

## Physical/provider scope

A concrete physical provider owns requirements implemented below the public DRIVER contract. This includes, as applicable:

- Data-Strobe signalling and encoding;
- electrical levels, connectors and cables;
- serialization, parity and character/control-code generation;
- NULL/FCT flow-control implementation;
- physical link initialization/error-recovery machinery and timing;
- physical signalling rate and interoperability;
- controller/FPGA implementation details and timing closure.

For the ExoSpaceLabs hardware implementation, these claims and evidence belong to the private **`spwkit-fpga`** project. A third-party DRIVER provider owns the corresponding evidence for its own hardware.

A complete SpaceWire system conformance statement therefore composes the applicable SpWKit software evidence with the applicable physical-provider evidence. Neither layer automatically inherits the other's claim.

## Virtual backends

SIMULATOR, VSPW-TP/UDP and DEVICE/VSPD are valid software verification/development environments for the application-visible requirements they implement. They do not need to emulate electrical waveforms in order to verify packet-service semantics, just as a unit test need not acquire a cable to have a purpose.

They do not claim physical Data-Strobe, electrical, cable, character/FCT/NULL timing, or FPGA implementation requirements.

## Claim terminology

Use **conformance** for requirement-by-requirement statements against the named ECSS revision.

Do not use **certified**, **qualified**, **flight qualified**, or **validated for flight** as synonyms. Those terms can require evidence beyond this software repository and beyond the ECSS SpaceWire requirements traced here.

A release claiming software conformance must identify:

1. the exact ECSS standard and revision;
2. the matrix revision;
3. the SpWKit release/commit tested;
4. the requirements classified Software verified;
5. tailoring, limitations and unsupported/future requirements;
6. the verification evidence used.

## Copyright and standards text

Project documentation paraphrases requirements and references clause identifiers instead of reproducing substantial ECSS text. The ECSS document remains normative.
