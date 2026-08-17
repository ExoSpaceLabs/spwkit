# Hosted platform support policy

SpWKit separates **public API/source portability** from **hosted backend runtime availability**. A platform may compile and install the portable C API without every backend being implemented on that host.

## Current hosted support matrix

| Host | C11 core / loopback | Local simulator | VSPW-TP/UDP runtime | Linux device / `vspwd` | Current validation level |
|---|---|---|---|---|---|
| Linux | supported | supported | **supported** | **supported** | full GCC/Clang host CI, pure-C static/shared consumers, simulator, D2D, VSPD/device/service/tools/bridge integration |
| macOS | supported | supported | **supported** | not implemented | host CI and UDP shared contract |
| Windows | supported | supported | **not implemented yet** | not implemented | MSVC build/test/install consumers; unsupported backend selection is verified |
| other CMake `UNIX` hosts | build path enabled | build path enabled where applicable | best effort / not release-validated | not release-validated | no release-support claim without dedicated evidence |

Linux is the primary distributed and virtual-device host. macOS is a supported second POSIX UDP host. Native Windows/Winsock UDP remains #42; the production Linux CUSE presenter remains #78. Neither is required for the v0.4 package boundary.

## C and C++ language support

The runtime itself is C11 on every supported host. `spwkit::spwkit` must not require a C++ compiler or runtime. Linux CI additionally configures the complete simulator + UDP runtime with `CXX=/bin/false`, executes pure-C behavioral tests, and validates static/shared installed C consumers.

The optional `spwkit::cpp` target is a C++17 header-only convenience layer and contains no backend implementation.

## Windows UDP behavior

A valid UDP backend configuration on a Windows build is recognized as a known backend configuration, but the hosted implementation is absent. Backend discovery/open therefore fails deterministically with:

```text
SPW_ERR_UNSUPPORTED
```

The library does not reinterpret Windows UDP selection as an invalid config and does not expose Winsock types in public headers.

```c
spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(42000, 42001, 42);
spw_port_config_t port = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
port.backend_config = &udp;
port.backend_config_size = sizeof(udp);

spw_port_workspace_requirements_t requirements;
spw_result_t result = spw_port_workspace_requirements(&port, &requirements);

if (result == SPW_ERR_UNSUPPORTED) {
    /* The selected backend is not available in this installed build. */
}
```

The same result is used on a POSIX host when SpWKit itself is configured with `SPWKIT_BUILD_UDP=OFF`.

## Build-time UDP option

`SPWKIT_BUILD_UDP` remains `ON` by default.

On a CMake `UNIX` host:

```text
SPWKIT_BUILD_UDP=ON  -> compile/link the POSIX UDP backend
SPWKIT_BUILD_UDP=OFF -> keep public UDP API, runtime selection unsupported
```

On a non-`UNIX` host such as Windows:

```text
SPWKIT_BUILD_UDP=ON  -> install public UDP API, print POSIX-only status, runtime unsupported
SPWKIT_BUILD_UDP=OFF -> install public UDP API, runtime unsupported
```

The option controls inclusion of an implementation, not existence of the public backend identifier/type definitions.

## Installed-package metadata

The generated `SpWKitConfig.cmake` exports:

```cmake
SpWKit_UDP_RUNTIME_SUPPORTED
SpWKit_UDP_RUNTIME_SCOPE
SpWKit_SIMULATOR_RUNTIME_SUPPORTED
SpWKit_DEVICE_RUNTIME_SUPPORTED
SpWKit_DEVICE_RUNTIME_SCOPE
SpWKit_CPP_WRAPPER_AVAILABLE
```

`SpWKit_UDP_RUNTIME_SCOPE` is currently `POSIX`. `SpWKit_UDP_RUNTIME_SUPPORTED` reports whether that **particular installed library build** contains the UDP runtime backend.

A pure-C CMake consumer can gate a hosted UDP example without probing native socket APIs:

```cmake
project(my_app LANGUAGES C)
find_package(SpWKit 0.4 CONFIG REQUIRED)

if(SpWKit_UDP_RUNTIME_SUPPORTED)
    add_executable(my_distributed_app main.c)
    target_link_libraries(my_distributed_app PRIVATE spwkit::spwkit)
endif()
```

The metadata is descriptive convenience for build systems. Runtime/backend-neutral application code should still handle `SPW_ERR_UNSUPPORTED` because a backend can be unavailable for build/configuration reasons.

The installed-package CI consumer validates the metadata against `spw_port_workspace_requirements()` on every host matrix entry. Windows therefore verifies the unsupported UDP runtime path through the installed package, not merely by compiling `udp.h`.

## Linux virtual-device support

v0.4 adds the Linux `SPW_BACKEND_DEVICE` runtime and pure-C `vspwd` service beneath the same public `spw_port_*` API. The installed package reports `SpWKit_DEVICE_RUNTIME_SUPPORTED` and `SpWKit_DEVICE_RUNTIME_SCOPE=Linux` so applications can gate hosted examples without exposing Unix socket types.

The unprivileged reference path uses private VSPD over `AF_UNIX`/`SOCK_SEQPACKET`. `spwctl` and `spwmon` are optional installed service tools. `vspwd` can also reserve one of its two reference ports as a VSPW-TP/UDP bridge endpoint while the opposite port remains a normal public device endpoint.

CUSE feasibility has been validated separately with libfuse3 and a private packet-record prototype. The production event-driven `/dev/vspwX` presenter remains #78 and is not claimed as part of v0.4.

## Why Winsock remains separate

Adding Winsock requires a private socket portability layer for:

- socket lifetime and invalid-handle representation;
- polling/readiness and timeout conversion;
- address/bind/send/receive calls;
- Winsock startup/cleanup ownership;
- error translation/interrupted-operation behavior;
- Windows process-level D2D coverage.

Those changes should not alter VSPW-TP or the public C ABI, but they are meaningful implementation/maintenance scope. They remain isolated in #42 so Linux virtual-device development does not entangle the released wire contract with a platform port.

## Public ABI boundary

The following remain private on every platform:

- POSIX file descriptors;
- `sockaddr*`, `pollfd`, `errno`;
- Unix-domain-socket structures;
- CUSE/FUSE handles;
- Winsock `SOCKET`, `WSADATA`, `WSAPOLLFD`, WSA error values;
- native address structures;
- event/select handles.

Public configuration contains only portable descriptive values. Backend implementation details never become mandatory common API types.

## Release interpretation

For current hosted support:

- Linux is the primary fully exercised distributed and virtual-device/service platform;
- macOS is a supported POSIX UDP host with host/shared-contract coverage;
- Windows is supported for the portable C runtime, simulator and package, but not yet for the UDP runtime backend;
- no claim is made for unvalidated UNIX/POSIX systems merely because CMake's `UNIX` condition enables implementation source.

Future releases may broaden the runtime matrix without changing application-facing SpaceWire semantics.
