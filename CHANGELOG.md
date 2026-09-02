# Changelog

Notable user-visible changes are recorded here. SpWKit follows semantic versioning for package releases while the public C ABI remains explicitly versioned through `SPWKIT_API_VERSION_*`.

## v0.6.0 — unreleased

Post-v0.5 interoperability and hardware-driver integration development line. `v0.5.1` is the current stable maintenance release; the earlier `v0.5.0` tag also remains immutable.

### Added

- optional CCSDSPack 2.x installed-package interoperability evidence using PUS-C TC/TM packets transported byte-for-byte through independent VSPW-TP/UDP and Linux DEVICE/VSPD peers (#90);
- typed receiver-side CCSDSPack parsing and structured validation after transport byte-identity checks, while keeping SpaceWire EOP metadata separate from CCSDS packet contents;
- portable hardware-driver callback/configuration boundary, DMA/zero-copy ownership mapping and deterministic reference-driver validation for the v0.6 hardware integration line.

### Changed

- CCSDSPack integration is provisional against the validated `CCSDSPack/develop` snapshot until the user-approved immutable 2.x release baseline is selected; a moving development branch is not a final SpWKit release dependency;
- the final v0.6 release audit must replace that provisional CCSDSPack reference with the explicitly approved immutable tag/commit before publication;
- CCSDSPack remains an optional external integration dependency and is not linked into or included by `libspwkit`;
- HardRT integration continues to use released baseline `0.4.0` at validated commit `1b861393cd7967ce5d0b3ac6f45928828a2d63aa`.

## v0.5.1 — 2026-09-02

Maintenance release on the v0.5 line. It preserves the v0.5.0 C runtime ABI, backend behavior, VSPW-TP wire format, VSPD contract and hardware-evidence boundary while completing the optional C++17 convenience surface.

### Fixed

- completed `spwkit::Port` forwarding for workspace requirements and the zero-copy ownership operations already present in the public C API;
- added `spwkit::Buffer`, `spwkit::BufferView` and `spwkit::WorkspaceRequirements` aliases without introducing a second ABI or backend implementation;
- preserved the C ownership contract, including pointer clearing after successful zero-copy submit/release operations;
- added C++ compile coverage for the forwarding surface;
- added a simulator-backed C++ zero-copy example covering acquire, fill, submit, receive, release, reclaim and final release;
- extended the loopback C++ example to verify capability-gated `SPW_ERR_UNSUPPORTED` behavior.

### Verification

- hosted C/C++ validation passed on Linux GCC, Linux Clang, Windows MSVC and macOS Clang;
- pure-C hosted and dedicated no-heap contracts passed;
- C++ simulator zero-copy execution and Linux ASan+UBSan validation passed;
- Debian packages were published for `amd64`, `arm64`, `armhf` and `riscv64`, with SHA-256 sidecars;
- the runtime image was validated across `linux/amd64`, `linux/arm64`, `linux/arm/v7` and `linux/riscv64`;
- physical SpaceWire/FPGA HIL remains outside the release claim.

## v0.5.0 — 2026-08-21

Hosted-platform parity and embedded/RTOS integration release. v0.5 builds on the v0.4 software-simulation/service boundary while keeping the public C API authoritative and keeping host-, RTOS- and presentation-specific implementation details private.

### Added

- production event-driven Linux CUSE `/dev/vspwX` presentation over the existing DEVICE/VSPD path, preserving packet boundaries, EOP/EEP, zero-length DATA, time codes, blocking/non-blocking I/O and non-consuming readiness semantics (#78);
- native Windows/Winsock implementation of `SPW_BACKEND_UDP` using the existing VSPW-TP wire contract and shared reliability/session/timing/fault state machine, without exposing Winsock types in the public ABI (#42);
- independent Windows installed-package process validation covering DATA, EOP/EEP, time codes, peer loss, fresh-session restart and `SPW_LINK_RUN` recovery;
- hosted binary validation for `armhf` and `riscv64` in addition to `amd64` and `arm64`, with target-userspace package installation, installed C/C++ consumers, tool execution and runtime-image smoke evidence (#88);
- four-platform OCI runtime image validation for `linux/amd64`, `linux/arm64`, `linux/arm/v7` and `linux/riscv64`;
- HardRT POSIX integration using installed packages and task-level public SpWKit behavior, while keeping HardRT outside the `libspwkit` dependency graph (#89);
- Cortex-M7 `arm-none-eabi`/Thumb/soft-float/no-heap HardRT integration evidence with complete firmware link, ELF/map validation and hosted/C++ runtime leakage checks.

### Changed

- installed package metadata now reports the hosted UDP runtime scope as `POSIX` or `Winsock` while keeping runtime availability as a separate build-dependent boolean;
- the same UDP backend contract, fragmentation/reassembly, deterministic-fault and virtual-link timing tests now execute on MSVC/Windows rather than maintaining a reduced Windows-only behavior path;
- Linux CUSE presentation remains optional and outside `libspwkit`, so ordinary SpWKit consumers do not gain a libfuse dependency;
- hosted release architecture claims remain evidence-backed: non-native Linux targets execute under QEMU/binfmt rather than being accepted from metadata-only cross compilation;
- bare-metal/RTOS artifacts remain separate from `.deb`/OCI distribution and must identify their target triple, CPU/ABI and toolchain assumptions.

### Verification

- the Windows UDP workflow compiles under MSVC, passes the shared UDP contract matrix, installs the package and executes independent-process peer-loss/restart recovery;
- generic Windows CI builds and tests the complete hosted suite and standalone installed C and C++ consumers with the Winsock-enabled package;
- CUSE build/package validation runs under GCC and Clang and live CI exercises the real `/dev/cuse` character-device contract;
- `amd64`, `arm64`, `armhf` and `riscv64` DEBs are built, installed and executed in their target Ubuntu 22.04 userspaces, with SHA-256 sidecars produced only after validation;
- the combined four-platform OCI image is built and verified before publication;
- HardRT POSIX behavior executes under GCC and Clang, while the Cortex-M7 profile is cross-built and fully linked with architecture/map/symbol checks;
- release publication requires the exact-tag CI fan-out, including the dedicated Windows UDP gate, before binary packages and GHCR images are published;
- physical SpaceWire HIL remains outside automated claims until matching hardware is available.

### Deferred beyond v0.5

- CCSDSPack 2.x packet-transport integration (#90) remains open until the user-approved immutable 2.x release baseline is selected; development integration may use a validated `CCSDSPack/develop` snapshot, but a moving branch is not a SpWKit release dependency;
- physical FPGA/HIL backend and electrical SpaceWire interoperability evidence;
- generic SpaceWire routing/topology management and router simulation.

## v0.4.0 — 2026-08-19

Linux virtual-device and userspace-service release. The public C API remains authoritative; VSPD, Unix sockets, CUSE and daemon-management protocol details remain private implementation layers.

### Added

- private VSPD v1.3 backend↔daemon protocol with a fixed 40-byte network-order header, bounded 32 KiB records and 1 MiB logical DATA fragmentation/reassembly;
- Linux `SPW_BACKEND_DEVICE` selected through the normal `spw_port_*` API, including reconnect/reattach after daemon loss;
- pure-C `vspwd` userspace service with two deterministic virtual ports, lifecycle, packets, EOP/EEP, zero-length packets, time codes, statistics and restart recovery;
- backend-neutral `SPW_CAP_READINESS`, `SPW_READY_RX_PACKET`, `SPW_READY_RX_TIME_CODE` and non-consuming level-triggered `spw_port_wait()`;
- VSPD 1.1 HELLO-only non-owning management and installed pure-C `spwctl` (`list`, `show`, `stats`, `clear-stats`);
- VSPD 1.2 bounded passive subscriptions and installed pure-C `spwmon` with human and JSON Lines output;
- standalone installed-package C11 and optional C++17 Linux device consumers, including mixed C/C++ peer validation;
- standalone installed-package C++17 VSPW-TP/UDP distributed peer using only `spwkit::cpp`;
- CUSE/libfuse3 feasibility work with a private fixed-width packet-record prototype; the production presenter remains tracked separately in #78;
- VSPD 1.3 bridged-port metadata and an optional topology-owned `vspwd` endpoint backed by the existing VSPW-TP/UDP runtime;
- end-to-end device↔daemon↔VSPW-TP/UDP DATA/time-code exchange with remote peer loss and fresh-process restart recovery;
- two-container Docker Compose distributed-simulation topology using isolated network namespaces and installed-package C/C++ peers;
- reproducible Ubuntu 22.04+ Debian packages for `amd64` and `arm64`, including the shared runtime, public C/C++ integration surface, `vspwd`, `spwctl` and `spwmon`;
- multi-architecture GHCR runtime/toolbox image for `linux/amd64` and `linux/arm64`, published under `v0.4.0`, `0.4` and `latest` tags;
- release packaging of Apache-2.0 `LICENSE` and `NOTICE` metadata.

### Changed

- completed the C-first v0.3 architecture by keeping `vspwd`, the Linux device backend and all daemon tools pure C with no mandatory C++ runtime;
- extended the shared backend contract to the Linux device backend and documented distributed/service-specific queue and peer-loss timing semantics;
- kept `/dev/vspwX` CUSE presentation optional and outside `libspwkit`; no kernel module or libfuse dependency is introduced into ordinary builds;
- kept bridge transport reliability in the existing `SPW_BACKEND_UDP` implementation rather than creating a second VSPW-TP stack inside `vspwd`;
- clarified simulation boundaries: `SPW_BACKEND_SIMULATOR` is intentionally process-local, while VSPW-TP/UDP and DEVICE/VSPD provide process-isolated simulation paths;
- versioned the shared library with SONAME major `0`, while the optional C++ wrapper remains header-only and compiled by the consuming application;
- keyed precompiled Linux artifacts by userspace baseline and CPU architecture rather than GCC version because the compiled public runtime ABI is C11.

### Verification

- Linux device, daemon, management, monitoring and bridge profiles run under GCC and Clang with `CXX=/bin/false`;
- public device and daemon paths run under ASan+UBSan;
- standalone installed C and C++ device consumers exercise C↔C, C++↔C++, C↔C++ and C++↔C interoperability;
- installed VSPW-TP/UDP C and C++ peers exercise C↔C, C++↔C++, C↔C++ and C++↔C as independent processes with 8 KiB DATA, EOP/EEP, time codes, peer loss and fresh-session recovery;
- distributed isolation is validated both with Linux network namespaces/veth and with a two-container Docker Compose bridge topology; these remain software simulation evidence, not physical HIL;
- `amd64` and `arm64` DEBs are built for their target architecture, installed in clean Ubuntu 22.04 containers, smoke-tested with the installed tools and exported with SHA-256 sidecars;
- ARM package and runtime-image validation executes under QEMU rather than relying on metadata-only cross packaging;
- the multi-arch OCI runtime image is built for both `linux/amd64` and `linux/arm64` before the release can publish GHCR tags;
- the cross-platform package matrix, pure-C static/shared gates, simulator contract, freestanding portability checks and Wireshark/tshark validation remain release gates;
- CUSE feasibility is compile/API validated under GCC and Clang without claiming `/dev/cuse` runtime evidence when hosted runners do not expose it.

### Deferred beyond v0.4

- production event-driven CUSE `/dev/vspwX` presenter (#78);
- native Windows/Winsock VSPW-TP runtime (#42);
- physical FPGA/HIL backend and electrical interoperability evidence;
- additional hosted precompiled architectures beyond `amd64` and `arm64` until target-specific CI evidence is added;
- generic SpaceWire routing/topology management and router simulation.

The v0.4.0 software release boundary is finalized by the dated release-preparation commit; the `v0.4.0` tag is accepted only after the tag-triggered exact-ref release matrix passes. Binary artifacts are published only after that full matrix, including the binary-package gate, succeeds on the exact tag commit.

## v0.3.0 — 2026-08-16

C-first runtime and packaging architecture. The public C API remains authoritative while the implementation no longer requires a C++ toolchain.

### Changed

- converted port dispatch, workspace ownership and backend polymorphism to a C11 vtable/context model;
- converted loopback, process-local simulator, zero-copy simulator path, VSPW-TP codec, fragment reassembly, virtual timing, deterministic fault logic and POSIX UDP backend to C11;
- preserved the released VSPW-TP v1 wire format and v0.2 session/reliability semantics through the implementation-language conversion;
- exported installed/runtime targets as lowercase `spwkit::spwkit` and optional `spwkit::cpp`, while retaining `find_package(SpWKit)` as the package lookup name;
- separated pure-C tests/examples from optional C++ development fixtures so CTest can execute meaningful behavior with `CXX=/bin/false`;
- made the installed distributed VSPW-TP example a genuine C-only project instead of forcing a C++ linker;
- replaced the previous placeholder embedded workflow with a real freestanding/no-heap portability build.

### Added

- optional header-only C++17 `spwkit::Port` RAII/convenience wrapper controlled by `SPWKIT_ENABLE_CPP`, with no alternate backend implementation;
- independent `SPWKIT_BUILD_CPP_TESTS` and `SPWKIT_BUILD_CPP_EXAMPLES` switches;
- pure-C two-peer simulator behavioral coverage for EOP/EEP and time codes;
- pure-C no-heap caller-owned workspace behavior and workspace-reuse coverage;
- C++ wrapper loopback example;
- Linux C-only static and shared installed-package validation;
- repository hygiene checks rejecting stale pre-C11 target names, forced C++ linker workarounds, obsolete package requests and removed runtime source paths;
- explicit freestanding C/no-heap compile evidence separate from future ARM/HardRT target-HIL claims.

### Portability contract

- `spwkit::spwkit` configures/builds with no C++ compiler, linker or runtime;
- the complete simulator + UDP runtime is exercised in a pure-C profile;
- static archives are checked for accidental C++ ABI/runtime references;
- the optional C++ wrapper remains exception-free and delegates exclusively to the public C API;
- hosted simulator thread primitives and POSIX socket details remain private implementation dependencies.

The dedicated release audit verified package/API version alignment and the full hosted, pure-C, simulator, D2D and freestanding portability gates before the `v0.3.0` tag boundary.

## v0.2.0 — 2026-08-16

Distributed virtual SpaceWire over the existing portable application API.

### Added

- VSPW-TP v1 distributed transport with the released 40-byte network-order header and 64-bit sender session identity;
- POSIX IPv4 UDP backend selected through the normal `spw_port_*` API;
- bounded fragmentation and arbitrary-order reassembly for logical packets up to the backend's 1 MiB limit;
- reliable DATA and TIME_CODE delivery using session-bound logical-message ACKs, bounded retransmission and duplicate suppression;
- KEEPALIVE/session peer discovery, timeout detection and restart recovery;
- deterministic virtual SpaceWire rate/latency modelling separate from incidental host-network timing;
- deterministic transport drop/duplicate/reorder/delay injection and explicit SpaceWire-side EEP injection;
- backend-neutral fault-domain statistics;
- reusable shared public backend contract coverage for the UDP backend and distributed peer-loss/restart extensions;
- installed-package equal-peer distributed example with two-process and Linux network-namespace integration;
- VSPW-TP Wireshark Lua dissector plus deterministic PCAP/tshark validation;
- installed-package metadata describing whether the current build contains the UDP runtime.

### Platform scope

- Linux is the primary fully exercised distributed runtime platform;
- macOS is supported as a POSIX UDP host through host/shared-contract CI;
- Windows retains the portable API/package and public UDP configuration surface, but the v0.2 UDP runtime is not implemented and returns `SPW_ERR_UNSUPPORTED`;
- native Winsock transport is deferred beyond v0.2.0 and tracked separately.

### Release hardening

- package version and public API version are aligned at `0.2.0`;
- Release-mode test targets explicitly keep `assert()` active so test operations and assertions cannot disappear under `NDEBUG`;
- the release audit runs the full Release suite with active assertions and stress-validates simulator edge behavior;
- stale pre-release documentation was reconciled with the completed v0.2 implementation.

## v0.1.0

Portable core and process-local virtual SpaceWire baseline:

- public C ABI and opaque port handles;
- copied packet I/O with EOP/EEP preservation and no-truncation receive semantics;
- link lifecycle/state, time codes, capabilities and statistics;
- deterministic loopback backend;
- process-local equal-peer simulator;
- caller-owned/no-heap port construction;
- optional zero-copy ownership API;
- reusable backend contract tests;
- CMake install/export and standalone `find_package(SpWKit)` consumption.