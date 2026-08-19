# Device-to-device tests

This directory contains distributed virtual SpaceWire verification. The D2D gate has complementary public-contract, transport-specific, process-isolation and network-isolation layers.

## Simulation boundaries

SpWKit deliberately has two different simulation boundaries:

- `SPW_BACKEND_SIMULATOR` is **process-local**. Both virtual endpoints live in one process. It is ideal for deterministic contract, edge-case and zero-copy testing, but it is not a cross-process transport.
- `SPW_BACKEND_UDP` is the distributed simulation backend. Independent processes or hosts communicate through VSPW-TP/UDP while applications continue to use the public `spw_port_*` API or the optional `spwkit::Port` C++ wrapper.

The Linux device/service path is separate again: `SPW_BACKEND_DEVICE -> VSPD -> vspwd`. Its installed-device CI already covers C/C, C++/C++, C/C++ and C++/C process pairs through the daemon.

## Public and transport coverage

The Linux D2D workflow builds SpWKit and runs all tests carrying the `d2d` label, including:

- reusable `backend_contract_udp` public-API behavior;
- VSPW-TP/UDP packet fragmentation and reassembly;
- arbitrary fragment ordering and duplicate/overlap handling;
- EOP/EEP preservation and no-truncation retry;
- reverse/full-duplex traffic and time codes;
- ACK/retransmission, duplicate suppression and peer/session recovery;
- configurable virtual rate/latency behavior;
- deterministic transport and SpaceWire fault scenarios.

## Installed-package process isolation

The workflow installs SpWKit and separately builds both public peer forms:

```text
examples/distributed      C11 public API       -> spwkit::spwkit
examples/distributed_cpp  C++17 wrapper        -> spwkit::cpp
```

Both use:

```cmake
find_package(SpWKit 0.4 CONFIG REQUIRED)
```

The C consumer is configured with `CXX=/bin/false`. Neither application can reach source-private VSPW-TP/backend targets.

`run_multi_process.sh` retains the original focused C-only regression. `run_cross_language.sh` executes the complete installed-package matrix:

```text
C API    <-> C API
C++ API  <-> C++ API
C API    <-> C++ API
C++ API  <-> C API
```

For every combination, A starts first and waits in the public connecting state. The peers exchange 8 KiB packets and time codes in both directions, preserving A/EOP and B/EEP. B exits, A must observe `SPW_LINK_ERROR_WAIT`, then a fresh B process/session starts and both complete a second exchange.

## Linux network-namespace isolation

`run_netns.sh` repeats the C scenario with A and B in separate Linux network namespaces connected by a veth pair:

```text
process A                 process B
    |                         |
 libspwkit                  libspwkit
    |                         |
 netns A                    netns B
10.231.0.1                 10.231.0.2
    |                         |
    +---- veth / MTU 1500 ----+
```

The application packet is 8 KiB while the veth MTU remains 1500 bytes, so successful delivery demonstrates VSPW-TP fragmentation/reassembly across isolated IP interfaces rather than shared process memory.

## Docker Compose host topology

`compose.yml` and `run_compose.sh` add a deployment-style two-host simulation. A single container image is built from the repository, but SpWKit is first installed to `/opt/spwkit`; only then are the standalone C and C++ peers configured through `find_package(SpWKit 0.4 CONFIG REQUIRED)`.

Compose runs two containers on an isolated bridge network with distinct IPv4 addresses. Each container has its own process and network namespace. The same four language combinations are exercised:

```text
container A             container B
-----------             -----------
C            <------->  C
C++          <------->  C++
C            <------->  C++
C++          <------->  C
```

The B-side container deliberately runs an initial peer process, terminates it, remains absent long enough for A to observe peer loss, then starts a fresh process/session and completes the recovery round.

Run the Compose matrix manually with:

```bash
bash tests/d2d/run_compose.sh
```

Docker/network namespaces are **software host-isolation evidence**. They are not physical SpaceWire HIL or electrical-interoperability evidence.

## Requirements

The ordinary process scripts need a POSIX host and the built peer executables.

The namespace script additionally requires:

- Linux network namespaces / `iproute2`;
- privileges to create namespaces and veth interfaces (`root` or `CAP_NET_ADMIN`).

The Compose matrix requires Docker Engine and Docker Compose v2.

On GitHub-hosted Ubuntu runners these gates are active. Container startup, socket bind success, or process creation alone are not accepted as proof: the tests require verified SpaceWire-visible packet, terminator, time-code, loss and recovery behavior through the public API.
