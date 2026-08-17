# `vspwd` userspace virtual SpaceWire service

`vspwd` is the Linux userspace service introduced in v0.4 beneath the normal SpWKit application API.

```text
C application -----------------------------+
                                           |
C++ application -> optional spwkit::cpp ---+
                                           v
                                      spw_port_*
                                           |
                                  SPW_BACKEND_DEVICE
                                           |
                                  VSPD / SOCK_SEQPACKET
                                           |
                                         vspwd
                                           |
                               virtual port 0 <-> port 1
```

Applications never need VSPD headers, Unix socket descriptors, or daemon-private state. The daemon protocol remains an implementation boundary underneath the authoritative public C API.

## Build

The hosted Linux device backend is enabled with `SPWKIT_BUILD_DEVICE=ON` (default ON where supported). `vspwd` itself remains opt-in:

```sh
cmake -S . -B build-device \
  -DSPWKIT_BUILD_DEVICE=ON \
  -DSPWKIT_BUILD_VSPWD=ON
cmake --build build-device
```

`SPWKIT_BUILD_VSPWD` defaults to `OFF`. This prevents library-only, embedded-oriented, or non-Linux builds from acquiring a hosted daemon merely because the build machine happens to be Linux.

Both the first device backend and daemon target are Linux-only. The installed package exports `SpWKit_DEVICE_RUNTIME_SUPPORTED` so consumers can distinguish an installed package that contains the runtime backend while keeping `SPW_BACKEND_DEVICE` and `spw_device_config_t` source-visible on every platform.

## Public C configuration

The public configuration deliberately contains no Unix-native types:

```c
spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(0u);
spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);

config.backend_config = &device;
config.backend_config_size = sizeof(device);

spw_port_t* port = NULL;
spw_result_t result = spw_port_open(&config, &port);
```

`spw_device_config_t` contains only:

- version/size fields;
- daemon `port_id`;
- a bounded endpoint path string.

The default development endpoint is `/tmp/spwkit-vspwd.sock`. Tests and multi-instance applications should override it explicitly. File descriptors, `sockaddr_un`, VSPD headers and daemon internals remain private.

## Run

```sh
./build-device/vspwd
```

Override the endpoint explicitly:

```sh
./build-device/vspwd --socket /tmp/my-mission-vspwd.sock
```

The socket is a Unix-domain `SOCK_SEQPACKET` endpoint. `SIGINT` or `SIGTERM` stops the daemon and removes the socket path.

The `/tmp` default is intended for development/testing. A packaged/system service can choose a runtime-directory path through `--socket`; no public application ABI depends on the default path.

## Management with `spwctl`

Build the optional pure-C tools with `SPWKIT_BUILD_TOOLS=ON`. `spwctl` uses a HELLO-only VSPD management connection and never ATTACHes to an application port.

```sh
spwctl list
spwctl show 0
spwctl stats 0
spwctl clear-stats 0
spwctl --socket /tmp/my-mission-vspwd.sock list
```

`list`/`show` expose attachment, started/reset state, link state and bounded queue occupancy. Statistics inspection and clearing operate on daemon counters without consuming DATA/TIME_CODE events. This slice intentionally does not let `spwctl` START/STOP/RESET an attached application-owned port; ownership semantics remain unambiguous until an explicit administrative override model is designed.

## Initial topology

The first v0.4 daemon owns exactly two virtual ports:

```text
port 0 <================> port 1
```

They are equal SpaceWire peers. There is no client/server direction at the SpaceWire layer.

Initial constraints:

- one attached application client per virtual port;
- the library backend performs VSPD HELLO then ATTACH internally;
- a second simultaneous ATTACH to an occupied port is rejected;
- both attached ports must be started before the link is `RUN`;
- a started port with a peer that has not yet appeared is `CONNECTING`;
- after an established peer disconnects, the surviving started port enters `ERROR_WAIT`;
- a fresh client can attach/start on the missing port and the surviving peer can recover to `RUN` without recreating its public `spw_port_t` handle.

This topology is intentionally small and deterministic. Router/topology management belongs in later daemon-management work, not in the first application data path.

## Public backend behavior

`SPW_BACKEND_DEVICE` maps the normal backend contract onto VSPD:

- `spw_port_start/stop/reset()` -> daemon lifecycle requests;
- `spw_port_get_link_state()` -> current daemon link state;
- `spw_port_send/receive()` -> logical DATA with internal VSPD fragmentation/reassembly;
- `spw_port_send_time_code/receive_time_code()` -> VSPD time-code events;
- `spw_port_wait()` -> level-triggered, non-consuming packet/time-code readiness;
- statistics -> daemon per-port statistics;
- ordinary EOP/EEP and zero-length packet semantics are preserved;
- zero-copy is currently unsupported by the hosted device backend and remains capability-gated.

The backend is cooperative and has no mandatory worker thread. Synchronous requests may receive asynchronous DATA/TIME_CODE/LINK_STATE events first; those are serviced internally until the requested operation can complete. Readiness uses the same bounded event service path and existing packet/time-code caches: `spw_port_wait()` reports only requested receive events, leaves them available to the normal receive APIs, and keeps Linux `poll()`/socket descriptors private to the backend.

If the daemon connection disappears, the backend reports link/service unavailability, preserves the public handle, and attempts reconnect/HELLO/ATTACH during subsequent normal API calls. A port that had been started before daemon loss requests START again after reattachment.

## Data path

VSPD DATA_TX fragments are reassembled inside `vspwd` before one logical packet is accepted. DATA_RX fragments are reassembled inside the library backend before one packet is returned to the application.

Current bounds:

```text
VSPD record payload      32 KiB
logical packet maximum    1 MiB
queued logical packets    2 per destination port
queued time codes         8 per destination port
client slots              4
```

Packet queueing is bounded. If the peer's logical packet queue is full, DATA_TX completes with resource exhaustion rather than creating an unbounded daemon buffer.

The path preserves:

- packet boundaries;
- EOP versus EEP;
- zero-length packets;
- time-code values;
- logical packet size independently from VSPD record fragmentation.

The library keeps a complete received packet until the application supplies enough storage. `SPW_ERR_BUFFER_TOO_SMALL` reports the required length/terminator without consuming the packet, so retry behavior matches the shared backend contract.

## Link lifecycle

The daemon uses the existing SpWKit link-state model:

```text
ERROR_RESET
ERROR_WAIT
READY
STARTED
CONNECTING
RUN
```

For the initial paired topology, the stable user-visible states are primarily:

```text
attached, not started      READY
started, peer not ready    CONNECTING
both peers started         RUN
established peer lost      ERROR_WAIT
explicit reset             ERROR_RESET
```

State changes are delivered as coalesced asynchronous VSPD events and absorbed by the library backend. A slow client therefore receives the latest state rather than causing an unbounded transition queue.

## Disconnect and restart

Client disconnect performs deterministic daemon cleanup:

- releases the port attachment;
- clears that port's queued inbound packets/time codes;
- clears the disconnecting client's partial DATA_TX reassembly;
- updates the surviving peer state;
- does not require the surviving peer process to reconnect.

The process integration tests keep port 0 alive while port 1 exits, require port 0 to observe `ERROR_WAIT`, then start a new port-1 process and require the link to return to `RUN`.

The public-backend scenario repeats that sequence through `spw_port_*`; the raw VSPD scenario remains protocol/daemon test infrastructure. This gives us independent evidence at both sides of the private protocol boundary instead of letting one helper implementation validate itself.

## Statistics

The daemon maintains the normal `spw_statistics_t` semantics per virtual port for:

- TX/RX logical packets;
- TX/RX payload bytes;
- TX/RX time codes;
- transmitted EEP packets;
- link errors;
- dropped/resource-exhausted traffic.

GET_STATISTICS and CLEAR_STATISTICS use VSPD fixed-width encodings and are translated back to the public `spw_statistics_t` shape by the backend.

## Malformed clients

VSPD frame validation occurs before daemon state mutation.

Protocol violations such as invalid magic/version/header/reserved fields, impossible fragmentation metadata, event frames sent in the client-to-daemon direction, or conflicting in-progress DATA metadata cause the offending connection to be closed. Ordinary valid requests that fail because of current service/link state receive explicit VSPD status responses instead.

The public backend treats malformed/unexpected daemon records as backend failure rather than exposing partially decoded VSPD state to applications.

## Memory/resource model

The daemon is single-threaded and `poll()` driven. Its resource storage is fixed-size after initialization.

The Linux client backend also uses a fixed-size context, including a bounded 1 MiB receive-reassembly arena and bounded time-code queue. It introduces no C++ runtime dependency and requires no background thread.

`SPWKIT_ENABLE_HEAP=OFF` still disables the library's hosted `spw_port_open()` convenience function. The backend remains usable with caller-owned storage through `spw_port_workspace_requirements()` and `spw_port_open_in_place()`; hosted examples may enable the heap convenience path independently.

## CI evidence

The dedicated **Virtual device** workflow is intentionally split into two profiles:

1. **daemon/protocol core**: `SPWKIT_BUILD_DEVICE=OFF`, `SPWKIT_ENABLE_HEAP=OFF`, `CXX=/bin/false`; this proves adding `vspwd` does not contaminate the portable library with socket/C++ references;
2. **hosted public device backend**: `SPWKIT_BUILD_DEVICE=ON`, `SPWKIT_BUILD_VSPWD=ON`, `CXX=/bin/false`; this runs the public C API process/restart contract with GCC and Clang.

ASan+UBSan runs the complete hosted backend + daemon path. The raw daemon tests remain separate from the public backend test, so both VSPD protocol correctness and application-facing behavior are exercised.

## Not in this slice

The initial public backend does **not** yet provide:

- `/dev/vspwX`/CUSE presentation;
- `spwctl`;
- `spwmon`;
- VSPW-TP/UDP bridging inside the daemon;
- router/topology configuration;
- physical SpaceWire hardware.

Those remain later v0.4+ layers above the now-testable C application -> device backend -> VSPD -> `vspwd` path.


## VSPW-TP/UDP bridge

`vspwd` can reserve either virtual port as a VSPW-TP/UDP endpoint while the opposite port remains a normal `SPW_BACKEND_DEVICE` application port.

```text
application -> SPW_BACKEND_DEVICE -> VSPD -> vspwd port 0
                                              |
                                              +-> port 1 [bridged]
                                                     |
                                                 VSPW-TP/UDP
                                                     |
                                               remote spw_port_*
```

Example:

```sh
vspwd --socket /tmp/vspwd.sock \
  --bridge-port 1 \
  --udp-local-port 46001 \
  --udp-remote-port 46002 \
  --udp-remote-address 127.0.0.1 \
  --udp-link-id 42 \
  --udp-keepalive-ms 1000 \
  --udp-peer-timeout-ms 3000
```

The bridge reuses the normal `SPW_BACKEND_UDP` implementation internally. `vspwd` does not contain a second VSPW-TP codec/reliability stack. The daemon services that cooperative backend from its event loop, forwards DATA and time codes through the existing bounded per-port queues, and projects the remote UDP link state onto the paired local VSPD port.

The bridged daemon port is topology-owned: ordinary VSPD ATTACH requests for that port are rejected. `spwctl` and `spwmon` expose the `bridged` flag so an operator can distinguish it from an unattached application port.

The v0.4 bridge deliberately supports one bridged endpoint in the two-port reference daemon. Arbitrary routing tables, multi-hop routing and hardware bridging are outside this slice.
