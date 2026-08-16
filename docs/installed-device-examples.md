# Installed-package Linux device examples

The `examples/installed_device` and `examples/installed_device_cpp` directories are standalone consumer projects. They do not participate in the SpWKit source-tree build. Each project discovers an already installed package with `find_package(SpWKit CONFIG REQUIRED)` and links only exported targets.

This is the intended integration model for applications using the Linux virtual-device backend.

## 1. Build and install SpWKit

Pure C runtime and daemon:

```sh
CC=gcc CXX=/bin/false cmake -S . -B build-device-package \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=OFF \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_SIMULATOR=OFF \
  -DSPWKIT_BUILD_UDP=OFF \
  -DSPWKIT_BUILD_DEVICE=ON \
  -DSPWKIT_BUILD_VSPWD=ON \
  -DSPWKIT_BUILD_TOOLS=ON \
  -DSPWKIT_ENABLE_HEAP=ON \
  -DSPWKIT_ENABLE_CPP=OFF
cmake --build build-device-package --parallel
cmake --install build-device-package --prefix "$PWD/install-device"
```

For the optional C++17 wrapper, configure the package with `SPWKIT_ENABLE_CPP=ON` instead.

The generated package exports runtime metadata. The standalone device examples reject an installed package unless `SpWKit_DEVICE_RUNTIME_SUPPORTED` is true and `SpWKit_DEVICE_RUNTIME_SCOPE` is `Linux`.

## 2. Build the standalone C consumer

```sh
CC=gcc CXX=/bin/false cmake \
  -S examples/installed_device \
  -B build-installed-device-c \
  -DCMAKE_PREFIX_PATH="$PWD/install-device"
cmake --build build-installed-device-c --parallel
```

The project enables only the C language and links `spwkit::spwkit`. A C++ compiler or runtime is not required.

## 3. Run two C peers through the installed daemon

```sh
SOCKET=/tmp/spwkit-example.sock
./install-device/bin/vspwd --socket "$SOCKET" &
VSPWD_PID=$!

./build-installed-device-c/spwkit_installed_device_c "$SOCKET" 0 &
PEER0_PID=$!
./build-installed-device-c/spwkit_installed_device_c "$SOCKET" 1
wait "$PEER0_PID"

kill -TERM "$VSPWD_PID"
wait "$VSPWD_PID"
```

The exchange demonstrates the installed public API rather than private VSPD calls:

```text
port 0                               port 1
  |                                    |
  |---- "hello" + EOP --------------->|
  |---- time code 7 ------------------->|
  |<--- "world" + EEP ----------------|
```

Both peers also use `spw_port_wait()` before receiving, so the example covers backend-neutral readiness without exposing the daemon socket.

## 4. Build the optional C++ wrapper consumer

Install SpWKit with `SPWKIT_ENABLE_CPP=ON`, then:

```sh
cmake \
  -S examples/installed_device_cpp \
  -B build-installed-device-cpp \
  -DCMAKE_PREFIX_PATH="$PWD/install-device"
cmake --build build-installed-device-cpp --parallel
```

The project links only the exported header-only `spwkit::cpp` target. `spwkit::Port` delegates to the same C runtime and uses the same `SPW_BACKEND_DEVICE` configuration.

Run it exactly like the C peer:

```sh
./build-installed-device-cpp/spwkit_installed_device_cpp "$SOCKET" 0 &
PEER0_PID=$!
./build-installed-device-cpp/spwkit_installed_device_cpp "$SOCKET" 1
wait "$PEER0_PID"
```

C and C++ peers are interchangeable because there is only one runtime and one public SpaceWire contract.

## CI contract

The installed-device workflow installs SpWKit to a temporary prefix, configures these directories as independent projects, and runs them only against the installed `vspwd` executable. Pure-C consumers are exercised with GCC and Clang while `CXX=/bin/false`; the wrapper job validates both C++↔C++ and mixed C↔C++ exchanges.
