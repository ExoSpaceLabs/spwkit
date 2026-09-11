# Changelog

Notable user-visible changes are recorded here. SpWKit follows semantic versioning for package releases while the public C ABI remains explicitly versioned through `SPWKIT_API_VERSION_*`.

## v0.7.0 — 2026-09-11

Behavioral/backend-contract hardening release. v0.7 defines the software-visible semantics that virtual and future physical providers must preserve, adds requirement-level ECSS SpaceWire software-conformance traceability, and makes performance-regression acceptance an explicit release gate.

### Added

- reusable public backend-contract execution for the deterministic DRIVER/reference provider (#211);
- versioned backend behavioral-equivalence matrix `v1-r1` and aggregate proof across SIMULATOR, VSPW-TP/UDP, Linux DEVICE/VSPD and DRIVER (#214);
- project-owned ECSS-E-ST-50-12C Rev.1 applicability/traceability matrix `v0.7-r2`, with seven specifically enumerated `Software verified` requirements and explicit provider/hardware-delegated, not-applicable and not-implemented/future dispositions;
- release-performance comparison tooling against immutable release baselines, repeated hosted screening and controlled same-runner aggregation;
- explicit pre-v1 ECSS-E-ST-40 and ECSS-Q-ST-80 sequencing in the v0.8/v0.9 roadmap.

### Changed

- defined and enforced same-handle serialization expectations, distinct-handle concurrency scope, lifecycle behavior, complete-operation timeout semantics, canonical result meanings, bounded-resource semantics and reset-safe zero-copy ownership epochs (#212);
- constrained the represented ECSS time-code profile to six-bit counts with `control_flags == 0`;
- removed duplicate generic packet-shape validation from the LOOPBACK backend after the public core validation path became authoritative;
- retained the zero-copy ownership guard after controlled batched measurement isolated its actual cost at approximately **+2.06 invariant-counter ticks per acquire+release pair**, while a semantics-preserving direct rewrite measured slightly slower;
- aligned package/API versioning and installed consumer requirements on the `0.7` package line.

### Verification

- common backend-contract evidence covers copied packet paths, zero-length packets, EOP/EEP, receive capacity retention, time codes, zero-copy interoperability, lifecycle, timeout/error/resource semantics and reset ownership behavior across the applicable backend families;
- controlled v0.7 performance campaigns compare against immutable `v0.6.1` at `03869c0b3bc9e895e0fe61a36f24cff2ace7d527` on matched hosts/toolchains/counters;
- post-cleanup controlled measurements show no recurring LOOPBACK regression, no recurring significant UDP regression, effectively flat DRIVER copied/lifecycle behavior, and DEVICE variation consistent with hosted measurement noise rather than a reproducible source-correlated regression;
- the existing NUCLEO-H755ZI-Q DMA2/Cortex-M7 cache qualification and immutable CCSDSPack `v2.0.0` integration baseline remain part of the provider/integration evidence;
- scoped ECSS conformance claims are backed by executable tests and limited to the requirement rows explicitly marked `Software verified`.

### Scope

- SpWKit v0.7.0 conforms to the specifically enumerated ECSS-E-ST-50-12C Rev.1 requirements/subclauses marked `Software verified` in the release matrix for the tested endpoint/link software abstraction;
- physical-layer, encoding, concrete controller/link-engine, flow-control, link-initialization/recovery, electrical and complete node time-code-engine requirements remain delegated to concrete providers/hardware;
- distributed interrupts, standardized node-management parameters and the SpaceWire MIB/service are not part of the v0.7 positive claim and are evaluated before the v1.0 API freeze;
- generic router implementation remains outside the core endpoint/link product scope;
- ECSS-E-ST-40 and ECSS-Q-ST-80 applicability/compliance work is planned for v0.8/v0.9 and is not claimed by v0.7.0.

## v0.6.1 — 2026-09-10

Maintenance and performance-consolidation release on the v0.6 line. It preserves the existing public application/backend contract while recording the completed profiling infrastructure, accepted performance optimizations, and synchronized post-v0.6 documentation.

### Changed

- completed benchmark backend capability discovery and host/build/counter metadata, including explicit `measured`, `unsupported-platform`, `not-built`, and `not-implemented-benchmark` coverage states (#171, #173);
- optimized VSPW-TP fragmented RX reassembly bookkeeping: the controlled 4096-byte paired `SpWKit - native` RX overhead fell from 60,281 to 25,032 invariant-TSC ticks (**58.5%**), while the isolated reassembly component fell by roughly **96-97%** (#201/#202);
- optimized POSIX UDP ready I/O by attempting `MSG_DONTWAIT` send/receive before readiness polling and falling back on `EAGAIN`/`EWOULDBLOCK`, preserving timeout/retry/liveness semantics; hosted normalized paired results show roughly **10-18%** relative transport-overhead reduction across representative payloads (#204/#206);
- optimized Linux DEVICE/VSPD ready-record I/O with the equivalent optimistic-ready strategy and a fair native comparator; hosted paired readiness measurements remove roughly **813-850 TSC ticks**, about **41-51%** of the raw ready-record operation (#205/#207);
- synchronized stable-version, STM32H755, CCSDSPack, DEVICE contract, release-workflow, DRIVER initializer, public-header, profiling and current-status documentation (#208).

### Verification

- consolidated CI, lifecycle profiling, profiling, profile benchmark, profiling-backend-boundary, and STM32H755 DMA-evidence workflows pass on the v0.6.1 consolidation candidate;
- profiling result sets now record the host/build/counter context required for reproducible comparisons and state unsupported/unbuilt/unimplemented benchmark coverage explicitly rather than silently omitting it;
- the existing physical NUCLEO-H755ZI-Q DMA2/Cortex-M7 cache qualification and immutable CCSDSPack `v2.0.0` baseline remain unchanged release evidence;
- hosted TSC measurements are retained as named-host regression/reference evidence and are not promoted into universal or physical SpaceWire performance claims.

### Scope

- no intentional public API/ABI contract break is introduced by v0.6.1;
- physical FPGA-backed SpaceWire controller/PHY/electrical HIL remains a later evidence layer;
- the staged pre-v1 behavioral, API/ABI, robustness and governance work remains tracked under #209 and #210-#216.

## v0.6.0 — 2026-09-08

Hardware-driver integration release. v0.6 adds the public software boundary needed to move an application from virtual SpaceWire backends to platform/vendor hardware drivers without changing the application-facing `spw_port_*` API or publishing proprietary hardware implementation details.

### Added

- portable `SPW_BACKEND_DRIVER` callback boundary for caller/vendor-owned SpaceWire hardware drivers, preserving lifecycle, copied packet I/O, EOP/EEP, time-code, readiness, statistics and timeout/error semantics without exposing native handles (#110);
- DMA-capable driver buffer descriptors mapped onto the existing opaque SpWKit zero-copy ownership API, with opaque driver tokens, configurable no-heap wrapper slots and optional cache/coherency synchronization hooks (#111);
- deterministic host reference-driver execution and freestanding/no-heap driver evidence (#112);
- proprietary-safe public FPGA/driver integration boundary and generic future HIL acceptance criteria (#113);
- optional CCSDSPack `v2.0.0` installed-package interoperability evidence using PUS-C TC/TM packets transported byte-for-byte through independent VSPW-TP/UDP and Linux DEVICE/VSPD peers (#90);
- typed receiver-side CCSDSPack parsing and structured validation after transport byte-identity checks while keeping SpaceWire EOP/EEP metadata separate from CCSDS packet contents;
- deployment-shaped two-node Docker Compose CCSDSPack-over-VSPW-TP/UDP integration (#117);
- standalone STM32H755 Cortex-M7 evidence firmware using real DMA2 memory-to-memory transfer, DMA-visible D2 SRAM and explicit D-cache clean/invalidate synchronization through the public driver contract (#119);
- scripted ST-LINK/OpenOCD/GDB board qualification that records deterministic `g_stm32h755_spwkit_evidence` values and rejects stale pre-reset buffers after `spw_port_reset()`.

### Changed

- the CCSDSPack integration baseline is finalized at immutable tag `v2.0.0`, commit `c2f318c330c564429bcc565a8acbff22728b2851`; `CCSDSPack/develop` is not a SpWKit release dependency;
- CCSDSPack remains an optional external integration dependency and is not linked into or included by `libspwkit`;
- hosted applications can use the same public SpWKit API while a hardware provider supplies controller-specific behavior below `spw_driver_ops_t`;
- release-facing documentation now distinguishes MCU DMA/cache qualification from physical SpaceWire PHY/electrical interoperability.

### Verification

- consolidated CI passes the hosted, pure-C, C++ convenience, simulator, VSPW-TP/UDP, Linux DEVICE/VSPD, package/consumer and freestanding/embedded gates on the v0.6 release candidate;
- the dedicated STM32H755 workflow cross-builds and links the Cortex-M7 integration against the pinned STM32CubeH7 CMSIS baseline;
- the physical NUCLEO-H755ZI-Q phase-7 run completed with `magic=0x53505736`, `phase=0x0000700d`, `result=0`, two DMA transfers, two TX packets, two RX packets, cache synchronization in both directions, and `reset_stale_invalidated=1` (`RESULT: PASS`);
- CCSDSPack PUS-C TC/TM serialized bytes survive SpWKit transport unchanged through independent UDP and Linux DEVICE/VSPD peers and the deployment-shaped Compose fixture;
- exact-tag publication requires the tagged commit to be the exact `main` head and validates project/API/changelog/installed-consumer version alignment before producing release assets.

### Deferred beyond v0.6

- proprietary FPGA/HDL implementation and register/descriptor/internal bus details;
- physical FPGA-backed SpaceWire PHY/electrical interoperability HIL;
- formal ECSS conformance/certification claims beyond the evidence explicitly documented by the project;
- generic router implementation; SpWKit remains focused on endpoint/link communication while allowing applications to communicate through external SpaceWire routing infrastructure.

## v0.5.1 — 2026-09-02

Maintenance release on the v0.5 line. It does not change the C runtime ABI, backend behavior, VSPW-TP wire format, VSPD contract, or hardware evidence boundary.

### Fixed

- completed the optional C++17 `spwkit::Port` convenience wrapper for workspace-requirement and zero-copy ownership operations already present in the v0.5.0 public C API;
- exposed `Buffer`, `BufferView`, and `WorkspaceRequirements` aliases without introducing a second ABI or backend implementation;
- preserved C ownership semantics, including pointer clearing after successful submit/release operations;
- added C++ compile coverage for the additional forwarding surface;
- added a simulator-backed C++ zero-copy example covering acquire, fill, submit, receive, release, reclaim and release;
- extended the loopback C++ example to verify capability-gated `SPW_ERR_UNSUPPORTED` behavior.

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

- CCSDSPack 2.x packet-transport integration (#90) remains open until CCSDSPack 2.x is a public validated/tagged release; a moving development branch is not a SpWKit release dependency;
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
