# SpWKit

[ExoSpaceLabs](https://github.com/ExoSpaceLabs)

**SpaceWire Development & Integration Toolkit**

SpWKit is a portable C11 SpaceWire software stack for simulation, distributed integration testing, Linux virtual devices, embedded/RTOS integration, and future hardware-backed links. Applications keep the same SpaceWire-facing API while the backend can move from a deterministic simulator to UDP, a Linux virtual device, or a platform/vendor driver.

```mermaid
flowchart TB
    APP[Application] --> API[spw_port_* public C API]
    API --> LOOP[Loopback reference]
    API --> SIM[Process-local simulator]
    API --> UDP[VSPW-TP / UDP]
    API --> DEV[Linux DEVICE / VSPD]
    DEV --> VSPWD[vspwd]
    VSPWD --> CUSE[spwcuse / /dev/vspwX]
    API --> DRIVER[Portable driver backend<br/>v0.6 development]
    DRIVER --> RTOS[RTOS / bare-metal driver]
    DRIVER --> FPGA[Future MMIO / DMA FPGA driver]
    FPGA --> PHY[Future physical SpaceWire implementation]
```

The runtime is C11. The optional C++17 layer is header-only and forwards to the same C ABI; it is a convenience surface, not a second implementation.

## Project status

### Stable: v0.5.0

`v0.5.0` is the current stable release. Its Git tag and release assets are immutable.

Highlights:

- process-local simulator plus distributed VSPW-TP/UDP transport;
- native POSIX and Windows/Winsock UDP runtime support behind `SPW_BACKEND_UDP`;
- Linux `SPW_BACKEND_DEVICE`, `vspwd`, `spwctl`, and `spwmon`;
- production CUSE `/dev/vspwX` presentation without adding libfuse to `libspwkit`;
- packet EOP/EEP preservation, time codes, link lifecycle/state, readiness, statistics, deterministic timing/fault support, and optional zero-copy ownership;
- C11 authoritative runtime with caller-owned/no-heap construction;
- optional header-only C++17 consumer layer;
- HardRT `0.4.0` POSIX and Cortex-M7 integration evidence;
- `.deb` releases for `amd64`, `arm64`, `armhf`, and `riscv64`;
- one GHCR runtime image for `linux/amd64`, `linux/arm64`, `linux/arm/v7`, and `linux/riscv64`.

### Development: v0.6.0

`develop` is the v0.6 integration branch. Current v0.6 work includes:

- the portable `SPW_BACKEND_DRIVER` callback/configuration boundary;
- DMA/zero-copy ownership mapping through the existing `spw_buffer_t` contract;
- a deterministic host reference driver and no-heap driver evidence;
- CCSDSPack PUS-C interoperability through installed packages, Linux DEVICE/VSPD, VSPW-TP/UDP, and a two-node Docker Compose topology;
- provisional CCSDSPack integration against `CCSDSPack/develop` at a validated snapshot while its final 2.x release baseline is still pending approval.

The v0.6 software boundary deliberately stops before a proprietary or physical FPGA SpaceWire implementation. STM32H755 runtime DMA/cache validation and the public FPGA/driver interface boundary remain separate evidence items.

See [v0.6 hardware integration boundary](docs/v0.6-scope.md) and [current project status](docs/current-status.md).

## Supported backends

| Backend | Linux | macOS | Windows | Embedded | Status |
|---|---:|---:|---:|---:|---|
| Loopback | yes | yes | yes | yes | stable |
| Process-local simulator | yes | yes | yes | no | stable |
| VSPW-TP / UDP | yes | yes | yes | transport-dependent | stable hosted |
| Linux DEVICE / VSPD | yes | no | no | no | stable |
| CUSE `/dev/vspwX` presenter | yes | no | no | no | stable optional service |
| Portable driver backend | yes | yes | yes | yes | v0.6 development |

Backend source visibility is separate from runtime availability. Applications should handle `SPW_ERR_UNSUPPORTED` when a backend or optional capability is disabled in a particular build.

## Public API model

### C11

```c
#include <spwkit/spwkit.h>

spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
spw_port_t* port = NULL;

if (spw_port_open(&config, &port) != SPW_OK) {
    return 1;
}

if (spw_port_start(port) != SPW_OK) {
    spw_port_close(port);
    return 2;
}

uint8_t payload[] = {0x01, 0x02, 0x03};
spw_packet_t packet = {
    .data = payload,
    .length = sizeof(payload),
    .capacity = sizeof(payload),
    .terminator = SPW_TERMINATOR_EOP,
};

spw_result_t result = spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE);
spw_port_close(port);
return result == SPW_OK ? 0 : 3;
```

### C++17 convenience wrapper

```cpp
#include <spwkit/spwkit.hpp>

#include <array>
#include <cstdint>

spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
spwkit::Port port;

if (spwkit::Port::open(config, port) != SPW_OK || port.start() != SPW_OK) {
    return 1;
}

std::array<std::uint8_t, 3> payload{{0x01, 0x02, 0x03}};
if (port.send(payload.data(), payload.size(), SPW_TERMINATOR_EOP) != SPW_OK) {
    return 2;
}

return port.stop() == SPW_OK ? 0 : 3;
```

On `develop`, `spwkit::Port` also forwards workspace requirements, in-place construction, readiness, time codes, statistics, fault statistics, and the zero-copy acquire/submit/reclaim/release API. The C ownership and result semantics remain authoritative.

Heap allocation is optional. Bare-metal/RTOS integrations can query workspace requirements and construct ports in caller-owned storage with `spw_port_open_in_place()` or, on current `develop`, `spwkit::Port::open_in_place()`.

## Virtual SpaceWire

### Process-local simulator

Two simulator ports with the same `link_id` and opposite A/B endpoint labels form equal peers. A/B are pairing labels, not server/client roles.

```mermaid
flowchart LR
    A[Application A] --> PA[libspwkit<br/>endpoint A]
    PA <-->|virtual link<br/>link_id = N| PB[libspwkit<br/>endpoint B]
    PB --> B[Application B]
```

### Distributed UDP

Independent processes, containers, or hosts can exchange the same logical SpaceWire events over VSPW-TP/UDP.

```mermaid
flowchart LR
    A[Application A] --> UA[SPW_BACKEND_UDP]
    UA <-->|VSPW-TP / UDP| UB[SPW_BACKEND_UDP]
    UB --> B[Application B]
```

UDP is only the carrier. Packet boundaries, EOP/EEP, time codes, session/retry behavior, virtual timing, and SpaceWire-side fault semantics remain SpWKit concepts.

### Linux virtual device

`vspwd` provides shared virtual SpaceWire endpoints for independent Linux processes. Applications can use the normal DEVICE backend, while `spwcuse` optionally presents a daemon port as a real character device.

```mermaid
flowchart TB
    APP[Application using spw_port_*] --> DEV[SPW_BACKEND_DEVICE]
    DEV --> VSPD[VSPD / AF_UNIX SOCK_SEQPACKET]
    VSPD --> D[vspwd]
    RAW[Application using /dev/vspwX] --> NODE["/dev/vspwX"]
    NODE --> CUSE[spwcuse]
    CUSE --> DEV2[SPW_BACKEND_DEVICE]
    DEV2 --> VSPD
    D --> P0[virtual port 0]
    D --> P1[virtual port 1]
    P0 <--> P1
```

`spwctl` provides non-owning management and `spwmon` provides passive observation. CUSE/libfuse stays outside the public runtime ABI.

## Build

Typical hosted build:

```bash
cmake -S . -B build \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Enable the optional C++17 wrapper target explicitly when it is needed by installed consumers:

```bash
cmake -S . -B build-cpp \
  -DSPWKIT_ENABLE_CPP=ON \
  -DSPWKIT_BUILD_CPP_EXAMPLES=ON
cmake --build build-cpp
```

A freestanding/no-heap profile disables hosted backends and uses caller-owned workspace:

```bash
cmake -S . -B build-freestanding \
  -DSPWKIT_ENABLE_HEAP=OFF \
  -DSPWKIT_BUILD_SIMULATOR=OFF \
  -DSPWKIT_BUILD_UDP=OFF \
  -DSPWKIT_BUILD_DEVICE=OFF
cmake --build build-freestanding
```

## Installation and consumers

Stable v0.5 consumers use the exported C target:

```cmake
find_package(SpWKit 0.5 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE spwkit::spwkit)
```

When the package was built with `SPWKIT_ENABLE_CPP=ON`:

```cmake
find_package(SpWKit 0.5 CONFIG REQUIRED)
target_link_libraries(my_cpp_app PRIVATE spwkit::cpp)
```

A source build from `develop` reports project/API version `0.6.0`; that does not retroactively change the stable `v0.5.0` package contract.

Standalone installed-package examples live under `examples/installed*`, distributed peers under `examples/distributed*`, and upper-layer integrations under `integrations/`.

## Binary releases

`v0.5.0` publishes:

```text
spwkit_0.5.0-1_amd64.deb
spwkit_0.5.0-1_arm64.deb
spwkit_0.5.0-1_armhf.deb
spwkit_0.5.0-1_riscv64.deb
```

The matching GHCR image supports:

```text
linux/amd64
linux/arm64
linux/arm/v7
linux/riscv64
```

See [binary release artifacts](docs/binary-packages.md).

## Development and release flow

```mermaid
flowchart LR
    F[Feature branch] --> PRD[PR to develop]
    PRD --> DEV[develop]
    DEV --> CI[Consolidated CI]
    CI --> PRM[Release PR to main]
    PRM --> MAIN[main]
    MAIN --> TAG[immutable vX.Y.Z tag]
    TAG --> REL[Release workflow]
```

Physical HIL remains separate from hosted CI until appropriate SpaceWire/FPGA hardware exists and the corresponding harness is executed.

## Documentation

- [Getting started](docs/getting-started.md)
- [Public API](docs/api.md)
- [Architecture](docs/architecture.md)
- [Backend contract](docs/backend-contract.md)
- [Configuration](docs/configuration.md)
- [Platform support](docs/platform-support.md)
- [Memory and portability](docs/memory.md)
- [Zero-copy buffers](docs/buffers.md)
- [C++ wrapper and language bindings](docs/language-bindings.md)
- [Simulator](docs/simulator.md)
- [VSPW-TP](docs/vspw-tp.md)
- [Linux VSPD device protocol](docs/vspw-device-protocol.md)
- [`vspwd`](docs/vspwd.md)
- [CUSE presenter](docs/cuse.md)
- [Driver backend](docs/driver-backend.md)
- [Testing](docs/testing.md)
- [Roadmap](docs/roadmap.md)

## Scope of compliance claims

SpWKit models and transports software-visible SpaceWire packet/link semantics. Automated simulation, transport, RTOS, package, and compile/link evidence is not a substitute for physical electrical interoperability or qualification evidence. No claim of real FPGA SpaceWire HIL is made until matching hardware exists and the HIL suite is executed against it.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
