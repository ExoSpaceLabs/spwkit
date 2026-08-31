# Binary release artifacts

SpWKit remains buildable from source through ordinary CMake install/export flows. Stable tags also publish precompiled Linux artifacts, but only for architectures with target-specific package installation and execution evidence in CI.

## Released v0.5.0 artifacts

The `v0.5.0` GitHub Release publishes one Debian package and matching SHA-256 sidecar for each validated hosted architecture:

```text
spwkit_0.5.0-1_amd64.deb
spwkit_0.5.0-1_arm64.deb
spwkit_0.5.0-1_armhf.deb
spwkit_0.5.0-1_riscv64.deb
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

1. enters a target-architecture Ubuntu 22.04 build/userspace under Docker buildx;
2. builds the shared SpWKit runtime and Linux tools;
3. creates the real `.deb` with CPack;
4. installs and exercises the package in target userspace;
5. validates installed `vspwd`, `spwctl`, and `spwmon` smoke paths;
6. builds and executes the architecture-specific runtime image;
7. verifies Debian architecture and `X.Y.Z-1` version metadata;
8. creates a SHA-256 sidecar only after the target checks succeed.

Non-native execution uses QEMU/binfmt. This is executable hosted-software evidence for the target userspace, not physical CPU, FPGA, or electrical SpaceWire evidence.

## Debian package contents

Each hosted package contains:

- shared versioned `libspwkit` and development link;
- public C headers;
- optional header-only C++17 `spwkit::Port` wrapper;
- exported `find_package(SpWKit CONFIG REQUIRED)` metadata;
- `vspwd`;
- `spwctl`;
- `spwmon`;
- Apache-2.0 `LICENSE` and `NOTICE` metadata.

Packages use an Ubuntu 22.04 userspace baseline and therefore target the corresponding Ubuntu 22.04-era glibc/userspace ABI or newer compatible systems for the same architecture.

## Why packages are not split by compiler version

`libspwkit` exposes an authoritative C11 compiled ABI. The optional C++17 layer is header-only and is compiled by the consuming application.

Publishing `gcc-11`, `gcc-12`, `gcc-13`, and similar copies would therefore imply a binary compatibility distinction that the public runtime ABI does not expose. The meaningful hosted binary axes are:

```text
Linux userspace baseline + CPU architecture + SpWKit ABI/version
```

Source builds remain available when an integration environment requires a particular compiler or hardening profile.

## GHCR runtime image

Stable releases publish one multi-architecture runtime/toolbox image:

```text
ghcr.io/exospacelabs/spwkit:vX.Y.Z
```

`v0.5.0` is published for:

```text
linux/amd64
linux/arm64
linux/arm/v7
linux/riscv64
```

Publication also updates the matching minor alias, for example `0.5`, and `latest` for the newest stable release.

The image contains the same hosted package surface as the Debian artifacts: `vspwd`, `spwctl`, `spwmon`, the shared library, public headers, and CMake package metadata.

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

The current Cortex-M7 evidence uses `arm-none-eabi`, Thumb, soft-float, no heap, and HardRT's Cortex-M port. CI links a complete firmware ELF and inspects its map, architecture attributes, and symbols. Runtime/HIL remains a separate claim.

A future precompiled embedded SDK/archive must encode these assumptions rather than presenting one generic `libspwkit.a` as universally compatible with every Arm microcontroller.

## Release workflow

There are two separate lifecycle workflows:

- ordinary pushes run the consolidated `CI` workflow;
- a `vX.Y.Z` tag runs the `Release` workflow.

The Release workflow checks out the exact requested tag and validates:

1. tag syntax and project version match;
2. `SPWKIT_API_VERSION_*` matches the project version;
3. the changelog contains a dated release heading and no matching `unreleased` heading;
4. versioned installed-package consumers present in that tag request the matching SpWKit minor version;
5. the tagged commit is the exact requested tag and is part of `main` history.

After validation, the four DEBs are built in parallel. GitHub Release publication depends only on validated DEB jobs, downloads all four packages and sidecars, verifies architecture/version/checksums, then creates the release without replacing existing immutable assets.

The GHCR multi-platform image is published independently. A container-image failure makes the Release workflow red but does not suppress otherwise verified Debian release assets.

Manual `workflow_dispatch` accepts an existing release tag for historical publication or repair. Validation is performed against the contents that actually existed in that tag rather than requiring later integrations to be present in older releases.

## v0.6 development

The v0.6 hardware-driver work does not change the hosted package architecture matrix by itself. A future hardware/RTOS backend may produce target-specific SDK/static artifacts, but those will remain separate from hosted Debian/GHCR distribution and will require their own explicit target identity and evidence.
