# C and C++ integration

SpWKit has one runtime implementation and one semantic contract: the public C API.

Starting with the v0.3 development line, `libspwkit` itself is implemented in C11. C++ is optional and exists only as a convenience layer above the same C API.

```text
C application -----------------------------+
                                           |
C++ application -> optional SpWKit::cpp ---+-> public C API
                                                |
                                                v
                                         C11 libspwkit
                                                |
                                     backend vtable/context
```

There is no separate C++ backend implementation. Enabling the wrapper cannot change packet, link, timeout, ownership, VSPW-TP or error semantics.

## Language contract

| Property | C API | Optional C++ wrapper |
|---|---|---|
| Language baseline | C11 | C++17 |
| CMake target | `SpWKit::spwkit` | `SpWKit::cpp` |
| Build option | always available | `SPWKIT_ENABLE_CPP=ON` |
| Runtime implementation | C11 | same C11 runtime |
| Backend configuration | public C structs | same public C structs |
| Errors | `spw_result_t` | same `spw_result_t` |
| Exceptions | none | none |
| Port ownership | explicit `spw_port_close()` | move-only RAII `spwkit::Port` |
| Packet representation | `spw_packet_t` | same `spw_packet_t` plus convenience overloads |
| Zero-copy API | authoritative C API | use the same C zero-copy operations/native handle |
| Bare-metal suitability | yes | only when a C++17 toolchain is available |

The C API is authoritative. The C++ wrapper is intentionally small; features that do not yet have a convenience wrapper remain available through `Port::native_handle()` and the normal C functions.

## C-only consumer

A C project does not enable C++:

```cmake
cmake_minimum_required(VERSION 3.20)
project(flight LANGUAGES C)

find_package(SpWKit 0.3 CONFIG REQUIRED)

add_executable(flight main.c)
target_link_libraries(flight PRIVATE SpWKit::spwkit)
```

This configuration is tested with `CXX=/bin/false`. The installed static archive is also scanned in CI for C++ ABI/runtime references.

## Optional C++ wrapper

Build/install SpWKit with:

```sh
cmake -S . -B build \
  -DSPWKIT_ENABLE_CPP=ON
```

Then a C++ application can use:

```cmake
project(app LANGUAGES CXX)
find_package(SpWKit 0.3 CONFIG REQUIRED)

target_link_libraries(app PRIVATE SpWKit::cpp)
```

Example:

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

`spwkit::Port` is move-only and closes its underlying `spw_port_t` in its destructor. It does not throw and it does not hide `spw_result_t`.

If `SPWKIT_ENABLE_CPP=OFF`, `SpWKit::cpp` and `spwkit/spwkit.hpp` are not installed. `SpWKit::spwkit` is unaffected. Installed package metadata exposes this decision as `SpWKit_CPP_WRAPPER_AVAILABLE`, so consumers do not need to infer wrapper support from filesystem state.

## Embedded static profile

A minimal C-only/no-heap build suitable as the software baseline for bare-metal or RTOS integration is:

```sh
CC=arm-none-eabi-gcc CXX=/bin/false cmake -S . -B build-embedded \
  -DBUILD_SHARED_LIBS=OFF \
  -DSPWKIT_BUILD_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=OFF \
  -DSPWKIT_BUILD_SIMULATOR=OFF \
  -DSPWKIT_BUILD_UDP=OFF \
  -DSPWKIT_BUILD_TOOLS=OFF \
  -DSPWKIT_ENABLE_HEAP=OFF \
  -DSPWKIT_ENABLE_CPP=OFF
```

A real cross toolchain normally supplies a CMake toolchain file as well; the example above emphasizes the language/dependency boundary rather than pretending every MCU is configured identically.

The portable core does not require hosted threads, sockets or a filesystem. Individual future embedded backends may add platform dependencies behind the backend contract.

## Hosted static and shared libraries

Static is the default CMake behavior:

```sh
cmake -S . -B build-static -DBUILD_SHARED_LIBS=OFF
```

Hosted Unix builds may request a shared library through the standard CMake switch:

```sh
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON
```

The Linux CI gate builds, installs and consumes `libspwkit.so` from a C-only project with `CXX=/bin/false`. Static and shared builds expose the same `SpWKit::spwkit` target and public C ABI.

Windows remains fully supported for the portable C core and simulator. Native Winsock UDP is tracked separately; the v0.3 language refactor does not change that backend-platform policy.

## Hosted simulator dependency

The process-local simulator is implemented in C but uses native hosted synchronization internally: pthreads on POSIX and native Windows synchronization primitives on Windows.

That dependency is private to the simulator. CMake package metadata recreates the thread dependency where a static hosted simulator archive requires it. Core-only and embedded builds with the simulator disabled remain thread-free.

## Design rule for future code

Runtime/backend functionality belongs in C11 beneath the public C API. A C++ feature is acceptable only when it is a wrapper or development-side tool that can be removed without changing the C library's behavior or capabilities.
