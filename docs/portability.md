# Portability contract

SpWKit's runtime portability baseline is **C11**. C++ is optional and sits above the C ABI as a header-only convenience layer.

## Language/toolchain policy

`spwkit::spwkit` must not require:

- a C++ compiler or linker;
- libstdc++, libc++ or a C++ ABI runtime;
- exceptions or RTTI;
- STL containers/allocators;
- platform-native handles in common operation signatures.

CI includes pure-C profiles with `CXX=/bin/false`. The optional `spwkit::cpp` target requires C++17, uses result codes, and delegates to the C API.

## Heap policy

Heap allocation is optional.

- `spw_port_open()` is a hosted convenience path when heap support is enabled;
- `spw_port_workspace_requirements()` reports private storage requirements;
- `spw_port_open_in_place()` constructs a port in caller-owned storage;
- C and C++ no-heap fixtures exercise the caller-owned path.

## Public ABI restrictions

Common API signatures must not expose:

- POSIX file descriptors, sockets, `pollfd`, or `sockaddr`;
- Winsock `SOCKET` values;
- CUSE/FUSE handles;
- RTOS task/semaphore/event handles;
- DMA descriptors or physical addresses;
- AXI/MMIO register structures;
- vendor SDK handles;
- C++ exceptions, classes, STL containers or ABI-dependent types.

Backend-specific configuration may describe an implementation with portable values or opaque caller-owned callback/context pointers, but application packet/link semantics remain common C operations.

## Process-local simulator

The simulator is C11 but uses private hosted synchronization to provide blocking waits and concurrent equal-peer behavior:

- pthread mutex/condition variables on POSIX hosts;
- native SRW locks/condition variables on Windows.

Disabling the simulator removes that hosted synchronization dependency from the core build.

## Distributed UDP

`SPW_BACKEND_UDP` is implemented on:

- POSIX sockets for supported Unix-like hosts;
- native Winsock for Windows.

The VSPW-TP codec, reassembly, timing and deterministic fault logic remain shared C code. Socket startup, readiness, error translation and native handle types stay private to the platform runtime.

Future embedded network transports such as lwIP/raw Ethernet may reuse the same VSPW-TP framing/public semantics without changing the application API.

## Linux virtual-device stack

`SPW_BACKEND_DEVICE`, VSPD and `vspwd` are Linux hosted components. Unix-domain sockets remain private. v0.5 `spwcuse` is a separate optional presenter that uses libfuse3 without adding a FUSE dependency to `libspwkit`.

Applications can therefore choose either the linked DEVICE API or a `/dev/vspwX` presentation without changing the portable core ABI.

## Portable driver boundary

v0.6 `SPW_BACKEND_DRIVER` accepts a versioned `spw_driver_ops_t` callback table and caller-owned driver context. This supports host reference drivers, bare metal, RTOS devices, vendor SDKs and future FPGA controllers while keeping platform-native mechanism types below the application API.

The driver/DMA callback layer may handle cache synchronization, descriptor submission and completion internally. The application still sees opaque `spw_buffer_t` ownership transitions.

## Static and shared builds

`BUILD_SHARED_LIBS=OFF` is the normal static/embedded-friendly build. Hosted CI also validates shared-library installation and independent consumers.

Build artifact type does not change the public API.

## Embedded evidence

Current evidence distinguishes:

```mermaid
flowchart LR
    C[Freestanding C / no heap] --> H[HardRT Cortex-M7 compile/link]
    H --> D[Portable driver/DMA contract]
    D --> STM[STM32H755 runtime evidence<br/>pending]
    STM --> PHY[Physical SpaceWire HIL<br/>future]
```

The HardRT baseline is release `0.4.0`. Cortex-M7 compile/link evidence is not a claim of execution on STM32H755, and the driver/DMA host tests are not a claim of cache/coherency correctness on physical silicon.

## Verification rule

A backend is compatible only when it satisfies the shared public contract for every capability it advertises. Compiling is necessary but not sufficient. Physical/hardware-specific claims require corresponding runtime/HIL evidence rather than being inferred from hosted CI.
