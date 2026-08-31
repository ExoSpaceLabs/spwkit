# SpWKit

**SpaceWire Development & Integration Toolkit**

SpWKit is a portable C11 SpaceWire software stack for simulation, distributed integration testing, Linux virtual devices, embedded/RTOS integration, and future hardware-backed links. The public C API stays the same while the implementation underneath can move from a simulator to UDP, a Linux device, or a vendor/FPGA driver.

```text
Application
    |
    v
spw_port_* public C API
    |
    +-- loopback reference backend
    +-- process-local SpaceWire simulator
    +-- VSPW-TP / UDP distributed backend
    +-- Linux virtual-device backend -> VSPD -> vspwd
    +-- portable driver backend          (v0.6 development)
            |
            +-- RTOS / bare-metal driver
            +-- future MMIO / DMA FPGA driver
                    |
                    +-- future SpaceWire HDL/IP core
```

The runtime is C11. The optional C++17 layer is header-only and wraps the same C ABI; it does not provide a second implementation.

## Project status

### Stable: v0.5.0

`v0.5.0` is the current stable release. It adds hosted-platform parity and embedded/RTOS integration on top of the v0.4 virtual-device boundary.

Highlights:

- process-local simulator plus distributed VSPW-TP/UDP transport;
- native POSIX and Windows/Winsock UDP runtime support behind the same `SPW_BACKEND_UDP` API;
- Linux `SPW_BACKEND_DEVICE`, `vspwd`, `spwctl`, and `spwmon`;
- production CUSE `/dev/vspwX` presentation without adding libfuse to `libspwkit`;
- packet EOP/EEP preservation, time codes, link lifecycle/state, readiness, statistics, deterministic timing/fault support, and optional zero-copy ownership;
- C11 authoritative runtime with optional no-heap construction;
- installed-package C and C++ consumer validation;
- HardRT POSIX and Cortex-M7 integration evidence;
- `.deb` releases for `amd64`, `arm64`, `armhf`, and `riscv64`;
- one GHCR runtime image for `linux/amd64`, `linux/arm64`, `linux/arm/v7`, and `linux/riscv64`.

The `v0.5.0` Git tag and release assets are immutable.

### Development: v0.6.0

`develop` is the v0.6 integration branch. Current v0.6 work includes:

- pinned CCSDSPack `v2.0.0` PUS-C packet interoperability through UDP and Linux DEVICE/VSPD paths;
- a portable hardware-driver integration boundary behind the normal `spw_port_*` API;
- mapping future DMA-capable driver buffers onto the existing zero-copy ownership API;
- deterministic host-side reference hardware-driver validation before physical hardware exists.

The v0.6 software boundary deliberately stops before implementing a real FPGA SpaceWire HDL core. The future HDL/IP register map, descriptor format, interrupt model, clock/reset domains, coherency rules, and physical HIL evidence will be specified separately.

See [v0.6 hardware integration boundary](docs/v0.6-scope.md) and [current project status](docs/current-status.md).

## Supported hosted backends

| Backend | Linux | macOS | Windows | Notes |
|---|---:|---:|---:|---|
| Loopback | yes | yes | yes | deterministic reference backend |
| Local simulator | yes | yes | yes | process-local equal peers |
| VSPW-TP / UDP | yes | yes | yes | POSIX sockets or native Winsock |
| Linux DEVICE / VSPD | yes | no | no | `vspwd` virtual-device service |
| CUSE `/dev/vspwX` presenter | yes | no | no | optional libfuse3 process, outside `libspwkit` |
| Portable driver backend | development | development | development | v0.6 hardware/RTOS integration boundary |

Hosted availability is separate from source/API visibility. Applications should still handle `SPW_ERR_UNSUPPORTED` when a backend was disabled in a particular build.

## Public API model

Copied packet I/O:

```c
#include <spwkit/spwkit.h>

spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
spw_port_t* port = NULL;

if (spw_port_open(&config, &port) != SPW_OK) {
    return 1;
}

spw_port_start(port);

uint8_t payload[] = {0x01, 0x02, 0x03};
spw_packet_t packet = {
    .data = payload,
    .length = sizeof(payload),
    .capacity = sizeof(payload),
    .terminator = SPW_TERMINATOR_EOP,
};

spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE);
spw_port_close(port);
```

Heap allocation is optional. Bare-metal/RTOS integrations can query workspace requirements and construct ports in caller-owned storage with `spw_port_open_in_place()`.

Backends that advertise `SPW_CAP_ZERO_COPY` additionally expose the common acquire/submit/reclaim/release buffer ownership API. Backends that advertise `SPW_CAP_READINESS` support non-consuming level-triggered receive readiness through `spw_port_wait()`.

## Virtual SpaceWire

### Process-local simulator

Two simulator ports with the same `link_id` and opposite A/B endpoint labels form equal peers:

```text
Application A                               Application B
     |                                           |
 libspwkit                                   libspwkit
 endpoint A <========== virtual link =========> endpoint B
                         link_id
```

A/B are pairing labels, not server/client roles.

### Distributed UDP

Two independent applications can exchange the same logical SpaceWire events over VSPW-TP/UDP:

```text
Application A                         Application B
     |                                    |
 libspwkit                            libspwkit
     |                                    |
 SPW_BACKEND_UDP                    SPW_BACKEND_UDP
     |                                    |
     +--------- VSPW-TP / UDP ------------+
```

UDP remains an internal carrier. Logical packet boundaries, EOP/EEP, time codes, retry/session behavior, and SpaceWire-side timing/fault semantics are handled by SpWKit rather than being equated with UDP datagrams.

### Linux virtual device

```text
Application
    |
spw_port_*
    |
SPW_BACKEND_DEVICE
    |
   VSPD
    |
  vspwd
    |
virtual port 0 <----> virtual port 1
```

`spwctl` provides non-owning management and `spwmon` provides passive observation. The optional `spwcuse` presenter maps VSPD ports to real CUSE character devices while keeping FUSE types outside the public library API.

## Build

Typical hosted build:

```bash
cmake -S . -B build \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The ordinary runtime can be built as static or shared with standard `BUILD_SHARED_LIBS`.

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

SpWKit installs CMake package metadata:

```cmake
find_package(SpWKit 0.6 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE spwkit::spwkit)
```

The C++ convenience target is optional:

```cmake
find_package(SpWKit 0.6 CONFIG REQUIRED)
target_link_libraries(my_cpp_app PRIVATE spwkit::cpp)
```

Standalone installed-package examples live under `examples/installed*` and distributed peers under `examples/distributed*`.

## Binary releases

Stable releases publish architecture-specific Debian packages plus SHA-256 sidecars. `v0.5.0` contains:

```text
spwkit_0.5.0-1_amd64.deb
spwkit_0.5.0-1_arm64.deb
spwkit_0.5.0-1_armhf.deb
spwkit_0.5.0-1_riscv64.deb
```

The matching GHCR runtime image is published for:

```text
linux/amd64
linux/arm64
linux/arm/v7
linux/riscv64
```

See [binary release artifacts](docs/binary-packages.md).

## Development and release flow

`develop` is the integration branch. Every push runs the consolidated CI matrix. Release work is merged from `develop` into `main`; publishing begins only when a `vX.Y.Z` tag is pushed and the Release workflow validates that the tag matches the project/API version, dated changelog entry, and `main` history.

```text
develop -> CI -> PR to main -> merge -> vX.Y.Z tag -> Release
```

Physical HIL remains a separate manual workflow until appropriate SpaceWire/FPGA hardware exists.

## Documentation

- [Getting started](docs/getting-started.md)
- [Public API](docs/api.md)
- [Architecture](docs/architecture.md)
- [Backend contract](docs/backend-contract.md)
- [Platform support](docs/platform-support.md)
- [Memory and portability](docs/memory.md)
- [Zero-copy buffers](docs/buffers.md)
- [Simulator](docs/simulator.md)
- [VSPW-TP](docs/vspw-tp.md)
- [Linux VSPD device protocol](docs/vspw-device-protocol.md)
- [vspwd](docs/vspwd.md)
- [CUSE presenter](docs/cuse.md)
- [Testing](docs/testing.md)
- [Roadmap](docs/roadmap.md)

## Scope of compliance claims

SpWKit models and transports software-visible SpaceWire packet/link semantics. Automated simulation, transport, RTOS, architecture-package, and compile/link evidence is not a substitute for physical electrical interoperability or qualification evidence. No claim of real FPGA SpaceWire HIL is made until matching hardware exists and the HIL suite is executed against it.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
