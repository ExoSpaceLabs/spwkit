# Current project status

## Released

`v0.5.0` is the current stable release. It includes:

- the portable C11 runtime and optional header-only C++17 wrapper;
- process-local simulation and distributed VSPW-TP/UDP transport;
- native POSIX and Windows/Winsock UDP runtime support;
- Linux VSPD virtual-device support, `vspwd`, `spwctl`, `spwmon`, and production CUSE `/dev/vspwX` presentation;
- hosted package validation for `amd64`, `arm64`, `armhf`, and `riscv64`;
- one four-platform GHCR runtime image;
- HardRT POSIX and Cortex-M7 integration evidence;
- release publication with architecture-specific DEBs and SHA-256 sidecars.

## Development

`develop` is the integration branch for v0.6.0. `main` remains the stable merge boundary. Ordinary pushes run the consolidated CI workflow. A release is published only from a version tag matching `vX.Y.Z` after the release validator confirms tag, project version, public API version, changelog, and main-history ancestry.

v0.6 currently adds the pinned CCSDSPack v2.0.0 interoperability evidence and is moving next toward a portable hardware-driver/DMA integration contract.

## Not claimed yet

SpWKit does not yet contain a real FPGA SpaceWire HDL core and does not claim physical SpaceWire interoperability. The software driver contract, mock/reference implementation and compile-time RTOS/bare-metal evidence can be completed before that hardware boundary is defined.
