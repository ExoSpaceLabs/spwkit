# Binary release artifacts

SpWKit remains buildable from source through ordinary CMake install/export flows. Stable tags also publish precompiled Linux artifacts, but only for architectures with target-specific package installation and execution evidence in CI.

## v0.6.1 artifacts

The `v0.6.1` Release workflow publishes one Debian package and matching SHA-256 sidecar for each validated hosted architecture:

```text
spwkit_0.6.1-1_amd64.deb
spwkit_0.6.1-1_arm64.deb
spwkit_0.6.1-1_armhf.deb
spwkit_0.6.1-1_riscv64.deb
```

Architecture mapping:

```text
Debian architecture   OCI platform
-------------------   ----------------
amd64                 linux/amd64
arm64                 linux/arm64
armhf                 linux/arm/v7
riscv64               linux/riscv64
```

`arm64` is the Debian name for AArch64. `armhf` is the 32-bit Arm hard-float target used for the `linux/arm/v7` image platform.

## Validation model

The package gate does not treat cross-compilation alone as release evidence. For every architecture it:

1. enters the target platform through Docker buildx/QEMU where execution is non-native;
2. builds the shared SpWKit runtime and Linux tools;
3. creates the real `.deb` with CPack;
4. installs and exercises the package in target userspace;
5. validates installed `vspwd`, `spwctl`, and `spwmon` smoke paths;
6. builds and executes the architecture-specific runtime image;
7. verifies Debian architecture and `X.Y.Z-1` version metadata;
8. creates a SHA-256 sidecar only after the target checks succeed.

This is executable hosted-software evidence for the target userspace, not physical CPU, FPGA, or electrical SpaceWire evidence.

## Debian package contents

Each hosted package contains:

- shared versioned `libspwkit` and development link;
- public C headers;
- optional header-only C++17 `spwkit::Port` wrapper when built/enabled by the package profile;
- exported `find_package(SpWKit CONFIG REQUIRED)` metadata;
- `vspwd`;
- `spwctl`;
- `spwmon`;
- Apache-2.0 `LICENSE` and `NOTICE` metadata.

Packages use the release workflow's Linux userspace baseline and therefore target compatible systems for the same architecture.

## Why packages are not split by compiler version

`libspwkit` exposes an authoritative C11 compiled ABI. The optional C++17 layer is header-only and is compiled by the consuming application.

Publishing `gcc-11`, `gcc-12`, `gcc-13`, and similar copies would imply a binary compatibility distinction that the public runtime ABI does not expose. The meaningful hosted binary axes are:

```text
Linux userspace baseline + CPU architecture + SpWKit ABI/version
```

Source builds remain available when an integration environment requires a particular compiler or hardening profile.

## GHCR runtime image

Stable releases publish one multi-architecture runtime/toolbox image:

```text
ghcr.io/exospacelabs/spwkit:vX.Y.Z
```

`v0.6.1` targets:

```text
linux/amd64
linux/arm64
linux/arm/v7
linux/riscv64
```

Publication also updates the matching minor alias (`0.6`) and `latest` for the newest stable release.

The image contains the hosted package surface: `vspwd`, `spwctl`, `spwmon`, the shared library, public headers, and CMake package metadata.

By default the container starts:

```text
vspwd --socket /run/spwkit/vspwd.sock
```

`/run/spwkit` is a volume so the Unix socket can be shared deliberately with another container or the host.

## Bare-metal artifacts are separate

Bare-metal targets do not receive Debian packages or OCI images. Their compatibility axes are different and must be explicit, for example:

```text
target triple + CPU/ISA + float ABI + toolchain + SpWKit profile
```

The v0.6 STM32H755 evidence uses `arm-none-eabi`, Cortex-M7, caller-owned/no-heap SpWKit construction, pinned CMSIS device/core headers, real DMA2, and explicit Cortex-M7 D-cache synchronization. The physical NUCLEO-H755ZI-Q phase-7 qualification passed the documented runtime evidence contract.

This embedded evidence is separate from physical SpaceWire PHY/electrical interoperability.

## Release workflow

There are two lifecycle workflows relevant to publication:

- ordinary pushes run the consolidated `CI` workflow;
- pushing a `vX.Y.Z` tag runs the `Release` workflow;
- `workflow_dispatch` is available as an exact-tag recovery/dispatch path and is subject to the same release-boundary validation.

The Release workflow checks out the exact tag and validates:

1. tag syntax and project version match;
2. `SPWKIT_API_VERSION_*` matches the project version;
3. the changelog contains a dated release heading and no matching `unreleased` heading;
4. versioned installed-package consumers present in that tag request the matching SpWKit minor version;
5. the tagged commit is exactly the current `main` head.

After validation, the four DEBs are built in parallel. GitHub Release publication depends on validated DEB jobs, downloads all four packages and sidecars, verifies architecture/version/checksums, then creates the release without replacing an existing release.

The GHCR multi-platform image is published independently. A container-image failure makes the Release workflow red but does not suppress otherwise verified Debian release assets.

Normal release publication remains exact-tag driven. Manual dispatch exists for exact-tag recovery/re-execution; it is not a bypass around tag/project/main alignment or the immutable-release checks.

## Hardware-driver scope

The v0.6 public driver work does not change the hosted package architecture matrix. Platform-specific FPGA/RTOS drivers and any future ASIC/adapter SDK artifacts remain separate from hosted Debian/GHCR distribution and require their own explicit target identity and evidence.
