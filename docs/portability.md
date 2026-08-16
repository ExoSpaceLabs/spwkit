# Portability contract

SpWKit's runtime portability baseline is **C11**. The library core and current runtime backends are implemented in C; C++ is optional and sits above the public C API as a convenience wrapper.

## Language/toolchain policy

`spwkit::spwkit` must not require:

- a C++ compiler;
- a C++ linker;
- libstdc++, libc++ or the C++ ABI runtime;
- exceptions or RTTI;
- STL containers or allocators.

CI configures the complete simulator + UDP runtime with `CXX=/bin/false`, executes pure-C behavior, and scans the resulting static archive for C++ ABI/runtime references.

The optional `spwkit::cpp` target requires C++17, but it is header-only and delegates to the same public C API. It does not contain a second backend implementation and does not use exceptions for recoverable errors.

## Heap policy

Heap allocation is optional.

- `spw_port_open()` is a hosted convenience API and may allocate when `SPWKIT_ENABLE_HEAP=ON`;
- `spw_port_open_in_place()` constructs the port and backend in caller-owned storage;
- `SPWKIT_ENABLE_HEAP=OFF` disables the hosted allocation path;
- dedicated C and C++ no-heap tests verify the mandatory caller-owned core path.

Future embedded backends may request larger or differently aligned caller-owned workspace without changing the public ownership model.

## Public ABI restrictions

Mandatory public API signatures must not expose:

- POSIX file descriptors or socket types;
- RTOS task/semaphore handles;
- DMA descriptors or physical addresses;
- AXI/MMIO addresses;
- vendor SDK handles;
- C++ classes, exceptions or RTTI-dependent types.

Backend-specific configuration may describe implementation choices, but common packet/link operations remain portable C types and functions.

## Hosted simulator versus portable core

The process-local simulator is implemented in C11 but uses hosted synchronization internally to provide blocking waits and concurrent peer behavior:

- pthread mutexes/condition variables on POSIX hosts;
- native SRW locks/condition variables on Windows.

Those primitives are private to the simulator backend. A core-only or embedded build with the simulator disabled has no hosted-thread dependency.

Bare-metal/RTOS backends are expected to use polling, interrupts, events, scheduler primitives or hardware queues behind the same backend vtable/context contract.

## Distributed UDP backend

The current hosted UDP runtime is C11 and POSIX-specific. Its sockets, `poll()` calls and timing primitives are private implementation details; no POSIX socket type enters the public ABI.

The VSPW-TP codec, reassembly logic, virtual timing and deterministic fault engine are C modules separated from the application API. Future lwIP/raw-Ethernet or native Winsock transports can reuse the same public semantics without requiring C++.

## Linux virtual-device direction

The v0.4 Linux virtual-device/service layer must obey the same rule. Unix-domain socket, CUSE and device-node details stay private to the backend/daemon implementation. A C application still sees only `spw_port_*`, and optional C++ applications still execute through that same C API.

## Static and shared builds

`BUILD_SHARED_LIBS=OFF` is the normal static/embedded-friendly build. Linux CI also validates `BUILD_SHARED_LIBS=ON`, installs `libspwkit.so`, and links a separate C-only consumer against the installed package.

The build artifact type does not change the C API. Backend availability is reported through capabilities and installed package metadata rather than through a different application surface.

## Verification rule

A backend is not considered compatible merely because it compiles. It must pass the shared public backend contract for every capability it advertises. Optional capabilities such as time codes, EEP and zero-copy are capability-gated, and advertising a capability without the corresponding contract behavior is a test failure.

For language portability, the stronger rule is also enforced: a valid C-only profile must configure, build, execute its C tests/examples, install and link without discovering or invoking a C++ compiler at all.
