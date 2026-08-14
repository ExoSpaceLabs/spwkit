# Portability and no-throw contract

SpWKit's portable library code is designed for bare-metal and RTOS integration even though some v0.1 backends, especially the process-local simulator, are hosted implementations.

## Exception policy

SpWKit library sources are compiled with C++ exceptions disabled on every supported toolchain, not only in a special embedded profile.

- GCC/Clang/AppleClang: `-fno-exceptions -fno-rtti`;
- MSVC: exception handling and RTTI are disabled for the `spwkit` target.

CI also rejects explicit `throw`, `try`, or `catch` syntax in C++ source files. Public APIs return `spw_result_t`; recoverable failures must be represented by result codes rather than exceptions.

The portable backend contract uses `noexcept` operations internally.

## Heap policy

Heap allocation is optional.

- `spw_port_open()` is a hosted convenience API and may allocate when `SPWKIT_ENABLE_HEAP=ON`;
- `spw_port_open_in_place()` constructs the port and backend in caller-owned storage;
- `SPWKIT_ENABLE_HEAP=OFF` disables the hosted allocation path;
- a dedicated CI test traps global C++ allocation and proves zero allocations for the mandatory no-heap loopback path.

Future embedded backends may request larger or differently aligned caller-owned workspace without changing the public ownership model.

## Public ABI restrictions

Mandatory public API signatures must not expose:

- POSIX file descriptors or socket types;
- RTOS task/semaphore handles;
- DMA descriptors or physical addresses;
- AXI/MMIO addresses;
- vendor SDK handles;
- C++ exceptions or RTTI-dependent types.

Backend-specific configuration may describe implementation choices, but common packet/link operations remain portable.

## Hosted simulator versus portable core

The process-local simulator uses hosted synchronization primitives to provide blocking waits and concurrent peer behavior. It is a runtime reference backend, not the bare-metal portability reference.

Bare-metal/RTOS backends are expected to replace hosted synchronization with polling, interrupts, events, scheduler primitives or hardware queues while preserving the same public results, ownership transitions and timeout semantics.

## Verification rule

A backend is not considered compatible merely because it compiles. It must pass the shared public backend contract for every capability it advertises. Optional capabilities such as time codes, EEP and zero-copy are capability-gated, and advertising a capability without the corresponding contract behavior is a test failure.
