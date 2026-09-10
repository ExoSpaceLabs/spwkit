# Compliance evidence

This directory contains project-owned requirement-to-evidence mappings for standards-visible behavior implemented by SpWKit.

## ECSS-E-ST-50-12C Rev.1

The current release-candidate matrix is:

- [`ecss-e-st-50-12c-rev1.md`](ecss-e-st-50-12c-rev1.md)

It classifies relevant SpaceWire requirements as:

- **Software verified**;
- **Provider/hardware delegated**;
- **Not applicable**;
- **Not implemented / future**.

The positive SpWKit conformance claim is limited to **Software verified** rows. Each such row identifies the public implementation surface and executable repository evidence.

Physical Data-Strobe, electrical, cable, character/FCT/NULL implementation, physical link timing and similar requirements must point to concrete provider evidence. For the ExoSpaceLabs FPGA path that evidence belongs to `spwkit-fpga`.

This directory does not contain copies of ECSS standards. Requirement text is paraphrased and clause identifiers point back to the normative standard.
