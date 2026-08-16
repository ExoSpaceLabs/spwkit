# Getting started with SpWKit

SpWKit exposes one portable C ABI. C++ applications can use the same public headers directly; an optional higher-level C++ wrapper can remain layered above the ABI without changing backend implementations.

The v0.2.0 release contains the v0.1 portable-core baseline plus the VSPW-TP/UDP distributed backend, reliability/liveness, deterministic timing and fault injection, distributed contract coverage, process/network isolation examples, and capture tooling.

## Build from source

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=ON \
  -DSPWKIT_BUILD_EXAMPLES=ON \
  -DSPWKIT_BUILD_SIMULATOR=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The SpWKit library itself is compiled without C++ exceptions and RTTI.

## Install and consume with CMake

```sh
cmake --install build --prefix /path/to/spwkit-install
```

Consumer project:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_spw_application LANGUAGES C CXX)

find_package(SpWKit 0.2 CONFIG REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE SpWKit::spwkit)
set_target_properties(my_app PROPERTIES LINKER_LANGUAGE CXX)
```

Application source can remain pure C and uses only the C ABI. The current library is a **static archive implemented in C++**, so the final executable must be linked by a C++-capable toolchain to supply the implementation runtime. This does not expose C++ types in the public ABI.

CI builds `examples/installed` against an installed package to ensure consumers do not accidentally depend on source-private headers or unpublished CMake state.

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

SpWKit never silently truncates packets. If the next complete packet is larger than `capacity`, receive returns `SPW_ERR_BUFFER_TOO_SMALL`, sets `length` to the required size, preserves the EOP/EEP terminator, and leaves the packet available for retry.

Zero-length packets are valid.

## EOP and EEP

A software-visible packet terminates with either:

- `SPW_TERMINATOR_EOP` for normal end-of-packet;
- `SPW_TERMINATOR_EEP` for error end-of-packet.

Backends that advertise `SPW_CAP_EEP` preserve EEP without silently converting it to EOP.

## Time codes

The v0.1 time-code type uses a six-bit count (`0..63`) and reserved control flags. Ordinary time codes currently require `control_flags == 0`.

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

Capabilities describe optional behavior and resource constraints such as maximum packet size, queue depths, buffer alignment and zero-copy support.

Do not assume that every backend has the same optional capabilities or limits.

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

Scatter/gather is not part of v0.1. One buffer represents one contiguous packet payload.

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

A and B are equal SpaceWire peers, not server/client roles.

## Distributed UDP peers in v0.2.0

The v0.2.0 backend uses the same public port API. Only backend configuration changes:

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

The initializer uses numeric localhost by default. The current backend uses numeric IPv4 addresses, bounded VSPW-TP fragmentation/reassembly, a default fragment payload of 1200 bytes, and a 1 MiB logical packet limit.

Applications still call `spw_port_start`, `spw_port_send`, `spw_port_receive`, time-code operations and statistics exactly as they do with other backends.

The v0.2.0 backend includes logical-message ACK/retransmission, duplicate suppression, peer session/keepalive/disconnect detection and restart recovery, configurable virtual latency/rate, deterministic transport fault injection, and explicit SpaceWire-side EEP injection. Linux is the primary fully exercised distributed host and macOS is a supported POSIX host; Windows retains the public API/package but returns `SPW_ERR_UNSUPPORTED` for the UDP runtime in v0.2.0.

## Errors and timeouts

All public operations return `spw_result_t`. Applications should handle results explicitly rather than rely on exceptions.

Useful timeout constants are:

- `SPW_TIMEOUT_IMMEDIATE` for non-blocking behavior;
- `SPW_TIMEOUT_INFINITE` for an unbounded wait when the backend supports waiting.

Finite timeout values are expressed in microseconds.

## Examples

- `examples/c_loopback.c`: hosted C API, capabilities, copied packets, EEP and time codes;
- `examples/cpp_no_heap.cpp`: C++ consumer using caller-owned workspace;
- `examples/c_simulator_zero_copy.c`: paired simulator endpoints and the zero-copy ownership lifecycle;
- `examples/installed`: standalone `find_package(SpWKit)` consumer used by CI;
- `examples/distributed`: installed-package equal-peer UDP application used for two-process and Linux network-namespace scenarios.

The D2D workflow builds the distributed example against the installed package, exercises full-duplex >MTU packets and time codes, then verifies peer loss and new-session restart recovery.
