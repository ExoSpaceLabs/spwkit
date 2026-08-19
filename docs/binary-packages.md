# Binary release artifacts

SpWKit remains buildable from source through ordinary CMake install/export flows. Tagged releases may also publish precompiled Linux artifacts, but only for architectures that have target-specific package installation and execution evidence in CI.

## Released v0.4.0 artifacts

The immutable v0.4.0 GitHub Release publishes:

```text
spwkit_0.4.0-1_amd64.deb
spwkit_0.4.0-1_arm64.deb
```

The matching v0.4.0 GHCR image supports:

```text
linux/amd64
linux/arm64
```

`arm64` is the Debian name for the 64-bit Arm/AArch64 target. v0.4.0 therefore already includes an AArch64 binary release.

## v0.5.0 development architecture matrix

v0.5 keeps the v0.4 targets and adds two hosted architectures after executing the real package/runtime under target emulation:

```text
Debian architecture   OCI platform
-------------------   ----------------
amd64                 linux/amd64
arm64                 linux/arm64
armhf                 linux/arm/v7
riscv64               linux/riscv64
```

The v0.5 package gate does not treat cross-compilation alone as support. For every architecture it:

1. enters an Ubuntu 22.04 target-architecture build environment;
2. builds the shared SpWKit runtime and Linux tools;
3. generates the real `.deb` with CPack;
4. installs the package in a clean target userspace;
5. executes installed `vspwd`, `spwctl`, and `spwmon` smoke paths;
6. builds and executes the architecture-specific runtime image;
7. exports the DEB and SHA-256 sidecar only after those checks succeed.

Non-native execution uses QEMU/binfmt through Docker buildx. This is hosted-software execution evidence, not physical CPU/HIL or electrical SpaceWire evidence.

## Debian package contents

Each hosted package contains:

- shared versioned `libspwkit` and the normal development link;
- public C headers;
- the optional header-only C++17 `spwkit::Port` wrapper;
- exported `find_package(SpWKit CONFIG REQUIRED)` CMake metadata;
- `vspwd`;
- `spwctl`;
- `spwmon`;
- Apache-2.0 `LICENSE` and `NOTICE` metadata.

The packages are built against an Ubuntu 22.04 userspace baseline. They therefore target the corresponding Ubuntu 22.04-era glibc/userspace ABI or newer compatible systems for the same architecture.

For development builds on `main`, package filenames use the current CMake project version. Stable filenames are only published from a finalized release tag.

Each released DEB has a SHA-256 sidecar generated from the exact package asset.

## Why packages are not split by GCC version

`libspwkit` is an authoritative C11 runtime. Its public compiled ABI is C, not the GNU C++ ABI. The optional C++17 layer is header-only and is compiled by the consuming application with that application's own C++ compiler.

Consequently, publishing `gcc-11`, `gcc-12`, `gcc-13`, and similar copies of the same runtime would imply a binary-compatibility distinction SpWKit does not actually expose. The meaningful hosted binary axes are:

```text
Linux userspace baseline + CPU architecture + SpWKit ABI/version
```

Compiler-specific builds can still be produced from source when an integration environment requires a particular compiler or hardening profile.

## GHCR image

Stable releases publish one multi-architecture runtime/toolbox image:

```text
ghcr.io/exospacelabs/spwkit:vX.Y.Z
```

For v0.5 the validated release matrix is intended to contain:

```text
linux/amd64
linux/arm64
linux/arm/v7
linux/riscv64
```

The immutable version tag is the preferred deployment reference. Publication also updates the matching minor alias, such as `0.5`, and `latest` for the newest stable release.

The image contains the same hosted package surface as the Debian artifacts: `vspwd`, `spwctl`, `spwmon`, the shared library, public headers and CMake package metadata.

By default the container starts:

```text
vspwd --socket /run/spwkit/vspwd.sock
```

`/run/spwkit` is declared as a volume so a Unix socket can be shared with another container or deliberately mounted to the host.

The image is a software runtime/simulation artifact. Its existence is not physical SpaceWire HIL or electrical-interoperability evidence.

## Bare-metal artifacts are separate

Bare-metal targets do not receive Debian packages or OCI images. Their compatibility axes are different and must be explicit, for example:

```text
target triple + CPU/ISA + float ABI + toolchain + SpWKit profile
```

The first v0.5 bare-metal contract is `arm-none-eabi` Cortex-M7, Thumb, soft-float, no-heap, integrated with HardRT's Cortex-M port. Its CI evidence is a fully linked firmware ELF/map and ABI/symbol inspection; runtime/HIL remains a separate claim.

A future precompiled embedded SDK/archive must encode those assumptions in its artifact identity rather than presenting one generic `libspwkit.a` as universally compatible with every Arm microcontroller.

## Publication gate

Binary artifacts are not published merely because a tag exists. The release audit first requires the complete exact-ref automated matrix, including Binary packages and Embedded portability. Only then does the reusable package workflow:

1. rebuild and validate every declared hosted target DEB;
2. verify architecture and version metadata plus SHA-256 sidecars;
3. build and push the declared multi-platform GHCR manifest;
4. verify the tag points exactly at `main` and matches the project/API version;
5. create the GitHub Release and attach all validated DEBs plus checksum files.

This keeps source, target execution evidence, package, container and release metadata on one exact commit boundary.
