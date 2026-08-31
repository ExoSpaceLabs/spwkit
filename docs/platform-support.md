# Hosted platform support policy

SpWKit separates public API/source portability from runtime backend availability. A platform may compile the portable C API while a specific backend is disabled or unavailable in that build.

## Current hosted support matrix

| Host | C11 core / loopback | Local simulator | VSPW-TP / UDP | Linux DEVICE / `vspwd` | CUSE presenter | Validation |
|---|---|---|---|---|---|---|
| Linux | supported | supported | **supported** | **supported** | **supported** | GCC/Clang host CI, pure-C static/shared consumers, simulator, D2D, VSPD/device/tools, CUSE |
| macOS | supported | supported | **supported** | not implemented | not implemented | Clang host CI and UDP contract |
| Windows | supported | supported | **supported** | not implemented | not implemented | MSVC host CI, native Winsock shared UDP contract, installed independent-process restart/recovery |
| other CMake UNIX hosts | best effort | best effort | build-path dependent | not release-validated | not release-validated | no support claim without dedicated evidence |

Linux is the primary distributed and virtual-device host. macOS is a supported POSIX UDP host. Windows uses a private Winsock compatibility layer while retaining the same public `SPW_BACKEND_UDP` configuration and VSPW-TP wire behavior.

## Windows UDP / Winsock

`SPW_BACKEND_UDP` is implemented natively on Windows without exposing `SOCKET`, `WSADATA`, `WSAPOLLFD`, WSA error values, or native address structures in the public ABI.

The private Windows layer owns:

- Winsock startup/cleanup;
- native socket lifetime and invalid-handle representation;
- bind/send/receive operations;
- readiness and bounded waits;
- timeout/error translation;
- normalization of Windows-specific UDP ICMP/`WSAECONNRESET` behavior.

The VSPW-TP codec, fragmentation/reassembly, ACK/retry, session/liveness, virtual timing, and deterministic fault behavior remain shared with POSIX builds.

Dedicated Windows CI executes the shared UDP contract plus installed independent-process peer-loss/restart recovery.

## Build-time UDP option

`SPWKIT_BUILD_UDP` is `ON` by default.

When enabled on a supported hosted platform, the package reports the UDP runtime as available. When disabled, the public UDP types remain source-visible but selecting the backend returns `SPW_ERR_UNSUPPORTED`.

Generated package metadata includes:

```cmake
SpWKit_UDP_RUNTIME_SUPPORTED
SpWKit_UDP_RUNTIME_SCOPE
SpWKit_SIMULATOR_RUNTIME_SUPPORTED
SpWKit_DEVICE_RUNTIME_SUPPORTED
SpWKit_DEVICE_RUNTIME_SCOPE
SpWKit_CPP_WRAPPER_AVAILABLE
```

`SpWKit_UDP_RUNTIME_SCOPE` is `POSIX` on POSIX builds and `Winsock` on Windows builds.

Applications should still handle `SPW_ERR_UNSUPPORTED` because a runtime can be omitted intentionally by build configuration.

## Linux virtual-device support

Linux provides the public `SPW_BACKEND_DEVICE` runtime and pure-C `vspwd` userspace service beneath the normal `spw_port_*` API.

The device path preserves:

- packet boundaries;
- EOP/EEP and zero-length packets;
- time codes;
- link lifecycle/state;
- statistics;
- non-consuming receive readiness;
- peer loss and fresh-process restart recovery.

`spwctl` performs non-owning management and `spwmon` passive observation. `vspwd` can also bridge one virtual port through the existing VSPW-TP/UDP backend.

## CUSE `/dev/vspwX`

The production CUSE presenter is implemented and validated on Linux. It exposes VSPD packet records as character devices while preserving packet boundaries, EOP/EEP, zero-length DATA, time codes, blocking/non-blocking semantics, poll/readiness behavior, and bounded queues.

CUSE/libfuse3 remains a separate executable boundary. Ordinary `libspwkit` consumers do not acquire a FUSE dependency and no FUSE types are present in the public C API.

## Embedded / RTOS scope

SpWKit supports caller-owned no-heap port construction and a freestanding C core. HardRT integration currently provides:

- installed-package POSIX runtime evidence under GCC and Clang;
- Cortex-M7 `arm-none-eabi` Thumb/soft-float/no-heap compile/link evidence;
- standalone firmware ELF/map/symbol validation.

This is software/ABI evidence, not physical SpaceWire HIL.

## v0.6 driver boundary

v0.6 introduces a portable hardware-driver backend so a vendor, RTOS, MMIO, or DMA implementation can sit behind the same `spw_port_*` API.

The driver contract must remain free of POSIX/Winsock/native descriptor types. DMA-capable buffers are intended to map onto the existing SpWKit zero-copy ownership API rather than create a second application packet interface.

A deterministic host reference driver will provide executable CI evidence before real hardware exists.

## Public ABI boundary

The following remain private implementation details:

- POSIX file descriptors and `pollfd`;
- `sockaddr*` and `errno`;
- Unix-domain-socket structures;
- CUSE/FUSE handles;
- Winsock handles and WSA values;
- future MMIO register structures;
- future DMA descriptors and physical/bus addresses;
- interrupt-controller and RTOS-native objects.

Public configuration contains portable descriptive values and callback contracts only.

## Release interpretation

Automated hosted tests prove software behavior on the declared host/toolchain/runtime combinations. Cross-architecture QEMU package execution proves target userspace behavior, not physical processor or electrical SpaceWire interoperability. Physical FPGA/HIL claims remain manual and separate until suitable hardware exists.
