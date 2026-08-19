# Binary release artifacts

SpWKit remains buildable from source through ordinary CMake install/export flows. Starting with v0.4.0, tagged releases also publish precompiled Linux artifacts for the two primary hosted architectures.

## Debian packages

The v0.4.0 GitHub Release publishes:

```text
spwkit_0.4.0-1_amd64.deb
spwkit_0.4.0-1_arm64.deb
```

Each package contains:

- shared `libspwkit.so.0` and the normal `libspwkit.so` development link;
- public C headers;
- the optional header-only C++17 `spwkit::Port` wrapper;
- exported `find_package(SpWKit 0.4 CONFIG REQUIRED)` CMake metadata;
- `vspwd`;
- `spwctl`;
- `spwmon`;
- Apache-2.0 `LICENSE` and `NOTICE` metadata.

The packages are built and installed in Ubuntu 22.04 target-architecture containers before publication. They therefore target a glibc 2.35-or-newer Linux userspace. The release CI performs the same install/tool smoke test for both `amd64` and `arm64`; the ARM build executes under QEMU on the hosted CI runner rather than merely relabelling an x86 artifact.

Install with:

```bash
sudo apt install ./spwkit_0.4.0-1_amd64.deb
```

or, on 64-bit ARM:

```bash
sudo apt install ./spwkit_0.4.0-1_arm64.deb
```

The Release also publishes a SHA-256 sidecar for each package.

## Why packages are not split by GCC version

`libspwkit` is an authoritative C11 runtime. Its public compiled ABI is C, not the GNU C++ ABI. The optional C++17 layer is header-only and is compiled by the consuming application with that application's own C++ compiler.

Consequently, publishing `gcc-11`, `gcc-12`, `gcc-13`, and similar copies of the same C runtime would imply a compatibility distinction that SpWKit does not actually have. The meaningful binary compatibility axes for v0.4 are:

```text
Linux userspace baseline + CPU architecture + SpWKit ABI/version
```

Compiler-specific builds can still be produced from source when an integration environment requires a particular compiler or hardening profile.

## GHCR image

The release publishes one multi-architecture runtime/toolbox image:

```text
ghcr.io/exospacelabs/spwkit:v0.4.0
```

Supported image platforms:

```text
linux/amd64
linux/arm64
```

The following aliases are updated by the v0.4.0 publication job:

```text
ghcr.io/exospacelabs/spwkit:0.4
ghcr.io/exospacelabs/spwkit:latest
```

The immutable version tag is the preferred deployment reference. `latest` is a convenience alias and may move on future stable releases, because apparently humans enjoy giving mutable things names that sound immutable.

The image installs the same `.deb` payload used for the GitHub Release and contains `vspwd`, `spwctl`, `spwmon`, the shared library, public headers and CMake package metadata.

By default the container starts:

```text
vspwd --socket /run/spwkit/vspwd.sock
```

`/run/spwkit` is declared as a volume so a Unix socket can be shared with another container or deliberately mounted to the host.

Example:

```bash
docker run --rm \
  -v spwkit-run:/run/spwkit \
  ghcr.io/exospacelabs/spwkit:v0.4.0
```

Inspect the daemon from another container sharing the same volume:

```bash
docker run --rm \
  -v spwkit-run:/run/spwkit \
  --entrypoint spwctl \
  ghcr.io/exospacelabs/spwkit:v0.4.0 \
  --socket /run/spwkit/vspwd.sock list
```

The image is a software runtime/simulation artifact. Its existence is not physical SpaceWire HIL or electrical-interoperability evidence.

## Publication gate

Binary artifacts are not published merely because a tag name exists. The v0.4 release audit first requires the complete automated matrix, including binary-package validation. Only then does the reusable package workflow:

1. rebuild and validate both target-architecture DEBs;
2. build and push the `amd64`/`arm64` GHCR manifest;
3. verify the tag points exactly at `main`;
4. create the GitHub Release and attach both DEBs plus their SHA-256 files.

This keeps source, package, container and release metadata on one exact commit boundary.
