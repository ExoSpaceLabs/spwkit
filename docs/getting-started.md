# Getting started with SpWKit

SpWKit exposes one authoritative C11 runtime/API. C applications use it directly. C++ applications may either call the same C API or opt into the header-only `spwkit::cpp` convenience wrapper. There is no separate C++ backend implementation.

The v0.4 release candidate contains the v0.1 portable core, v0.2 VSPW-TP/UDP distributed backend, v0.3 C11 runtime conversion, and the Linux virtual-device/userspace-service layer tracked by #54. Production CUSE `/dev/vspwX`, native Winsock UDP and physical HIL remain separately tracked beyond this release boundary.

## Pure-C build from source

This profile builds real tests and examples without enabling C++:

```sh
CC=gcc CXX=/bin/false cmake -S . -B build-c \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_CPP_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_SIMULATOR=ON \
  -DSPWKIT_BUILD_UDP=ON \
  -DSPWKIT_ENABLE_CPP=OFF
cmake --build build-c --parallel
ctest --test-dir build-c --output-on-failure
```

This is not a header-only compatibility check. The C-only CI profile executes public API behavior, loopback I/O, two-peer simulator EOP/EEP/time-code behavior, C examples and no-heap caller-owned construction.

## C plus optional C++ wrapper

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_CPP_TESTS=ON \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_CPP_EXAMPLES=ON \
  -DSPWKIT_BUILD_SIMULATOR=ON \
  -DSPWKIT_BUILD_UDP=ON \
  -DSPWKIT_ENABLE_CPP=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The wrapper remains exception-free and delegates to the same C functions.

## Install and consume with CMake

```sh
cmake --install build-c --prefix /path/to/spwkit-install
```

Pure-C consumer:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_spw_application LANGUAGES C)

find_package(SpWKit 0.4 CONFIG REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE spwkit::spwkit)
```

No C++ linker/runtime is required. CI verifies this with `CXX=/bin/false` for both static and Linux shared installed packages.

Optional C++ wrapper consumer, when the package was built with `SPWKIT_ENABLE_CPP=ON`:

```cmake
project(my_cpp_spw_application LANGUAGES CXX)
find_package(SpWKit 0.4 CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE spwkit::cpp)
```

`find_package(SpWKit)` keeps the package spelling; imported target namespaces are lowercase.

## Open a port

Every application starts from `spw_port_config_t` and a `spw_port_t` handle.

Hosted convenience path:

```c
spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
spw_port_t* port = NULL;

if (spw_port_open(&config, &port) != SPW_OK) {
    /* handle error */
}
```

`spw_port_open()` may allocate when `SPWKIT_ENABLE_HEAP=ON`.

For deterministic/no-heap environments, query the required workspace and construct the port in caller-owned memory:

```c
spw_port_workspace_requirements_t req;
spw_port_workspace_requirements(&config, &req);

/* Provide req.size bytes aligned to req.alignment. */
spw_port_open_in_place(&config, workspace, workspace_size, &port);
```

`spw_port_close()` destroys an in-place port but does not free caller-owned workspace. The workspace can be reused immediately.

## Optional C++ port wrapper

```cpp
#include <spwkit/spwkit.hpp>

spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
spwkit::Port port;

if (spwkit::Port::open(config, port) != SPW_OK) {
    return 1;
}
if (port.start() != SPW_OK) {
    return 2;
}
```

`spwkit::Port` is move-only RAII convenience around `spw_port_t`. It does not change backend, timeout, error or ownership semantics.

## Link lifecycle

```c
spw_port_start(port);
spw_port_stop(port);
spw_port_reset(port);
```

Use `spw_port_get_link_state()` rather than assuming the state after an operation. A process-local simulator endpoint may remain `SPW_LINK_CONNECTING` until its peer is started.

## Copied packet I/O

```c
uint8_t payload[] = {1, 2, 3};
spw_packet_t tx = {
    payload,
    sizeof(payload),
    sizeof(payload),
    SPW_TERMINATOR_EOP
};

spw_result_t result =
    spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE);
```

Receive storage is caller-owned:

```c
uint8_t storage[256];
spw_packet_t rx = {
    storage,
    0,
    sizeof(storage),
    SPW_TERMINATOR_EOP
};

result = spw_port_receive(port, &rx, 1000);
```

SpWKit never silently truncates packets. If the next complete packet is larger than `capacity`, receive returns `SPW_ERR_BUFFER_TOO_SMALL`, reports the required length/terminator, and leaves the packet available for retry.

Zero-length packets are valid.

## EOP and EEP

A software-visible packet terminates with either:

- `SPW_TERMINATOR_EOP` for normal end-of-packet;
- `SPW_TERMINATOR_EEP` for error end-of-packet.

Backends that advertise `SPW_CAP_EEP` preserve EEP without silently converting it to EOP.

## Time codes

```c
spw_time_code_t time_code = {37, 0};
spw_port_send_time_code(port, &time_code, SPW_TIMEOUT_IMMEDIATE);
```

Check `SPW_CAP_TIME_CODE` before relying on time-code support.

## Capabilities

```c
spw_capabilities_t caps;
spw_port_get_capabilities(port, &caps);
```

Capabilities describe optional behavior and resource constraints such as maximum packet size, queue depths, buffer alignment and zero-copy support. Do not assume every backend has identical optional capabilities or limits.

## Zero-copy ownership API

When `SPW_CAP_ZERO_COPY` is advertised, applications may use backend-owned buffers.

TX lifecycle:

```text
acquire -> application fills -> submit -> backend owns
        -> reclaim -> application owns -> reuse or release
```

RX lifecycle:

```text
backend receives -> acquire -> application inspects -> release
```

Successful submit/release calls set the caller's buffer pointer to `NULL`. Failed operations preserve ownership and leave the pointer unchanged.

The zero-copy API models ownership, not DMA implementation details. The local simulator emulates it using ordinary host memory; a future hardware backend may map the same contract to DMA-capable buffers internally.

## Process-local simulator peers

Two simulator endpoints are paired by `link_id` and opposite endpoint identifiers:

```c
spw_simulator_config_t sim_a = SPW_SIMULATOR_CONFIG_INITIALIZER;
sim_a.link_id = 42;
sim_a.endpoint = SPW_SIMULATOR_ENDPOINT_A;

spw_simulator_config_t sim_b = SPW_SIMULATOR_CONFIG_INITIALIZER;
sim_b.link_id = 42;
sim_b.endpoint = SPW_SIMULATOR_ENDPOINT_B;
```

A and B are equal SpaceWire peers, not server/client roles. The simulator implementation is C and the C-only CI profile performs actual bidirectional packet/time-code behavior with no C++ compiler.

## Distributed UDP peers

The distributed backend uses the same public port API. Only backend configuration changes:

```c
spw_udp_config_t udp_a = SPW_UDP_CONFIG_INITIALIZER(42000, 42001, 42);
spw_port_config_t cfg_a = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
cfg_a.backend_config = &udp_a;
cfg_a.backend_config_size = sizeof(udp_a);

spw_udp_config_t udp_b = SPW_UDP_CONFIG_INITIALIZER(42001, 42000, 42);
spw_port_config_t cfg_b = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
cfg_b.backend_config = &udp_b;
cfg_b.backend_config_size = sizeof(udp_b);
```

The current backend uses numeric IPv4 addresses, bounded VSPW-TP fragmentation/reassembly, a default fragment payload of 1200 bytes, and a 1 MiB logical packet limit.

Applications still call `spw_port_start`, `spw_port_send`, `spw_port_receive`, time-code operations and statistics exactly as they do with other backends.

The distributed backend includes logical-message ACK/retransmission, duplicate suppression, peer session/keepalive/disconnect detection and restart recovery, configurable virtual latency/rate, deterministic transport fault injection, and explicit SpaceWire-side EEP injection.

`examples/distributed` is deliberately a **C-only installed-package consumer**. The D2D workflow builds it with `CXX=/bin/false`, then runs independent-process and Linux network-namespace restart scenarios.

## Linux virtual-device service

Build the public Linux device backend and daemon without enabling C++:

```sh
CC=gcc CXX=/bin/false cmake -S . -B build-device \
  -DSPWKIT_BUILD_CPP_TESTS=OFF \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_DEVICE=ON \
  -DSPWKIT_BUILD_VSPWD=ON
cmake --build build-device --parallel
```

Run `vspwd`, then open one daemon port through the same public API:

```c
spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(0u);
spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
config.backend_config = &device;
config.backend_config_size = sizeof(device);
```

`spw_port_wait()` provides non-consuming packet/time-code readiness when the device backend advertises `SPW_CAP_READINESS`. `spwctl` and `spwmon` are optional installed daemon tools when `SPWKIT_BUILD_TOOLS=ON`; they never become an alternate application data API.

Standalone installed-package device consumers live under `examples/installed_device` and `examples/installed_device_cpp`.

## Errors and timeouts

All public operations return `spw_result_t`. Applications should handle results explicitly rather than rely on exceptions.

Useful timeout constants are:

- `SPW_TIMEOUT_IMMEDIATE` for non-blocking behavior;
- `SPW_TIMEOUT_INFINITE` for an unbounded wait when the backend supports waiting.

Finite timeout values are expressed in microseconds.

## Examples

- `examples/c_loopback.c`: hosted C API, capabilities, copied packets, EEP and time codes;
- `examples/c_simulator_zero_copy.c`: pure-C paired simulator endpoints and zero-copy ownership;
- `examples/cpp_no_heap.cpp`: C++ source using the C API and caller-owned workspace;
- `examples/cpp_wrapper_loopback.cpp`: optional `spwkit::Port` convenience wrapper;
- `examples/installed`: standalone C-only installed consumer;
- `examples/installed_cpp`: standalone optional C++ wrapper consumer;
- `examples/distributed`: C-only installed VSPW-TP/UDP equal-peer process used by D2D CI;
- `examples/c_device_peer.c` / `examples/cpp_device_peer.cpp`: in-tree Linux device peers;
- `examples/installed_device` / `examples/installed_device_cpp`: standalone installed-package Linux device consumers.

All examples use public headers only.
