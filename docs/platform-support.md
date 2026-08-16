# v0.2 platform support policy

SpWKit separates **public API/source portability** from **hosted backend runtime availability**. A platform may compile and install the portable C API without every backend being implemented on that host.

## v0.2 support matrix

| Host | Core / loopback | Local simulator | VSPW-TP/UDP runtime | v0.2 validation level |
|---|---|---|---|---|
| Linux | supported | supported | **supported** | full host CI, shared UDP contract, D2D, process + network-namespace integration |
| macOS | supported | supported | **supported** | host CI and shared UDP contract |
| Windows | supported | supported | **not implemented in v0.2** | MSVC build/test/install consumer; UDP selection must return `SPW_ERR_UNSUPPORTED` |
| other CMake `UNIX` hosts | build path enabled | build path enabled where applicable | best effort / not release-validated | no v0.2 release CI claim |

The v0.2 distributed reference target is therefore POSIX-hosted, with Linux as the primary distributed/HIL-development platform and macOS covered as a second POSIX host. Native Windows/Winsock UDP transport is deferred beyond v0.2.

This is a runtime support decision, not a public-API fork. `spwkit/udp.h`, `SPW_BACKEND_UDP`, `spw_udp_config_t`, VSPW-TP concepts and the common `spw_port_*` operations remain available to Windows source consumers.

## Windows behavior

A valid UDP backend configuration on a v0.2 Windows build is recognized as a known backend configuration, but the hosted implementation is absent. Backend discovery/open therefore fails deterministically with:

```text
SPW_ERR_UNSUPPORTED
```

The library does not reinterpret Windows UDP selection as an invalid config and does not expose Winsock types in public headers.

This distinction is important for portable applications:

```c
spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(42000, 42001, 42);
spw_port_config_t port = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
port.backend_config = &udp;
port.backend_config_size = sizeof(udp);

spw_port_workspace_requirements_t requirements;
spw_result_t result = spw_port_workspace_requirements(&port, &requirements);

if (result == SPW_ERR_UNSUPPORTED) {
    /* The application/runtime selected a backend not available in this build. */
}
```

The same result is used on a POSIX host when SpWKit itself is configured with `SPWKIT_BUILD_UDP=OFF`.

## Build-time option

`SPWKIT_BUILD_UDP` remains `ON` by default.

On a CMake `UNIX` host:

```text
SPWKIT_BUILD_UDP=ON  -> compile/link the POSIX UDP backend
SPWKIT_BUILD_UDP=OFF -> keep public UDP API, runtime selection unsupported
```

On a non-`UNIX` host such as Windows v0.2:

```text
SPWKIT_BUILD_UDP=ON  -> install public UDP API, print POSIX-only status, runtime unsupported
SPWKIT_BUILD_UDP=OFF -> install public UDP API, runtime unsupported
```

The option therefore controls inclusion of an implementation, not existence of the public backend identifier/type definitions.

## Installed-package metadata

The generated `SpWKitConfig.cmake` exports:

```cmake
SpWKit_UDP_RUNTIME_SUPPORTED
SpWKit_UDP_RUNTIME_SCOPE
```

For v0.2, `SpWKit_UDP_RUNTIME_SCOPE` is `POSIX`. `SpWKit_UDP_RUNTIME_SUPPORTED` reports whether that **particular installed library build** contains the UDP runtime backend.

A CMake consumer can therefore gate a hosted UDP example or test without probing native socket APIs:

```cmake
find_package(SpWKit 0.2 CONFIG REQUIRED)

if(SpWKit_UDP_RUNTIME_SUPPORTED)
    add_executable(my_distributed_app main.c)
    target_link_libraries(my_distributed_app PRIVATE SpWKit::spwkit)
endif()
```

The metadata is descriptive convenience for build systems. Runtime/backend-neutral application code should still handle `SPW_ERR_UNSUPPORTED` because a backend can be unavailable for build/configuration reasons.

The installed-package CI consumer validates this metadata against `spw_port_workspace_requirements()` on every host matrix entry. Windows therefore verifies the unsupported runtime path through the installed package, not merely by compiling `udp.h`.

## Why Winsock is deferred

Adding Winsock at this point would require a private socket abstraction for:

- socket lifetime and invalid-handle representation;
- polling/readiness and timeout conversion;
- address/bind/send/receive calls;
- Winsock startup/cleanup ownership;
- error translation/interrupted-operation behavior;
- platform-specific integration and D2D CI.

None of those changes should alter VSPW-TP or the public C ABI, but they are meaningful implementation/maintenance scope. v0.2 already establishes the distributed contract on Linux/macOS and the project's next hardware path is Linux/embedded/HIL. Shipping a lightly tested Windows transport merely to make the support table symmetrical would weaken the release boundary rather than strengthen it.

Winsock can be added later behind the same backend/public contract and reuse the codec, reliability, timing, fault, capture and shared-contract work already completed in v0.2.

## Public ABI boundary

The following remain private on every platform:

- POSIX file descriptors;
- `sockaddr*`, `pollfd`, `errno`;
- Winsock `SOCKET`, `WSADATA`, `WSAPOLLFD`, WSA error values;
- native address structures;
- event/select handles.

Public configuration contains only portable descriptive values such as numeric IP strings, UDP port numbers, link IDs, timeouts and deterministic simulation controls.

## Release interpretation

For v0.2.0:

- Linux is the primary fully exercised distributed platform;
- macOS is a supported POSIX UDP host with shared-contract/host-CI coverage;
- Windows is supported for the portable/core API and package, but not for the UDP runtime backend;
- no claim is made for unvalidated UNIX/POSIX systems merely because CMake's `UNIX` condition enables the implementation source.

Future releases may broaden the runtime matrix without changing application-facing SpaceWire semantics.
