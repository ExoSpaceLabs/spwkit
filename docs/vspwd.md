# `vspwd` userspace virtual SpaceWire service

`vspwd` is the Linux userspace service being introduced in v0.4 beneath the normal SpWKit application API.

The first implementation establishes and tests the daemon/process boundary before the public Linux-device backend is added:

```text
raw VSPD test client 0                 raw VSPD test client 1
          |                                      |
          +---------- AF_UNIX/SOCK_SEQPACKET ----+
                              |
                            vspwd
                              |
                    virtual port 0 <-> 1
```

Once the client backend lands, the raw clients disappear from normal application use:

```text
C application -> spw_port_* -> Linux device backend -> VSPD -> vspwd
```

Applications must never use VSPD or daemon-private state directly.

## Build

`vspwd` is opt-in:

```sh
cmake -S . -B build-device \
  -DSPWKIT_BUILD_VSPWD=ON
cmake --build build-device --target vspwd
```

`SPWKIT_BUILD_VSPWD` defaults to `OFF`. This prevents library-only, embedded-oriented, or non-Linux builds from acquiring a hosted daemon merely because the build machine happens to be Linux.

The initial daemon target is Linux-only.

## Run

```sh
./build-device/vspwd
```

Default endpoint:

```text
/tmp/spwkit-vspwd.sock
```

Override it explicitly:

```sh
./build-device/vspwd --socket /tmp/my-mission-vspwd.sock
```

The socket is created as a Unix-domain `SOCK_SEQPACKET` endpoint with user read/write permissions. `SIGINT` or `SIGTERM` stops the daemon and removes the socket path.

The `/tmp` default is intended for development/testing. A packaged/system service can choose a runtime-directory path through `--socket`; no public application ABI depends on the default path.

## Initial topology

The first v0.4 daemon owns exactly two virtual ports:

```text
port 0 <================> port 1
```

They are equal SpaceWire peers. There is no client/server direction at the SpaceWire layer.

Initial constraints:

- one attached application client per virtual port;
- HELLO must succeed before ATTACH;
- a second simultaneous ATTACH to an occupied port is rejected;
- both attached ports must be started before the link is `RUN`;
- a started port with a peer that has not yet appeared is `CONNECTING`;
- after an established peer disconnects, the surviving started port enters `ERROR_WAIT`;
- a fresh client can attach/start on the missing port and the surviving peer can recover to `RUN` without reconnecting its daemon socket.

This topology is intentionally small and deterministic. Router/topology management belongs in later daemon-management work, not in the first application data path.

## Data path

VSPD DATA_TX fragments are reassembled inside `vspwd` before one logical packet is accepted.

Current bounds:

```text
VSPD record payload      32 KiB
logical packet maximum    1 MiB
queued logical packets    2 per destination port
queued time codes         8 per destination port
client slots              4
```

Packet queueing is bounded. If the peer's logical packet queue is full, DATA_TX completes with resource exhaustion rather than creating an unbounded daemon buffer.

The daemon preserves:

- packet boundaries;
- EOP versus EEP;
- zero-length packets;
- time-code values;
- logical packet size independently from VSPD record fragmentation.

One fragmented DATA_TX receives one final response after the complete logical packet has been validated and accepted into the destination queue. Individual fragments are never reported as SpaceWire packets.

DATA_RX is emitted asynchronously and may itself span multiple VSPD records. The future library backend will reassemble the complete packet before exposing it through `spw_port_receive()`.

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

State changes are delivered as coalesced asynchronous VSPD link-state events. A slow client therefore receives the latest state rather than causing an unbounded transition queue.

## Disconnect and restart

Client disconnect performs deterministic cleanup:

- releases the port attachment;
- clears that port's queued inbound packets/time codes;
- clears the disconnecting client's partial DATA_TX reassembly;
- updates the surviving peer state;
- does not require the surviving peer process to reconnect.

The integration test keeps port 0 alive while port 1 exits, requires port 0 to observe `ERROR_WAIT`, then starts a new port-1 process and requires the link to return to `RUN`.

This is deliberately stronger than simply restarting both clients and declaring recovery successful, a popular testing technique among software that prefers not to learn anything.

## Statistics

The daemon maintains the normal `spw_statistics_t` semantics per virtual port for:

- TX/RX logical packets;
- TX/RX payload bytes;
- TX/RX time codes;
- transmitted EEP packets;
- link errors;
- dropped/resource-exhausted traffic.

GET_STATISTICS and CLEAR_STATISTICS are implemented through VSPD fixed-width encodings. The edge integration test verifies transfer counters and clear behavior.

## Malformed clients

VSPD frame validation occurs before daemon state mutation.

Protocol violations such as invalid magic/version/header/reserved fields, impossible fragmentation metadata, event frames sent in the client-to-daemon direction, or conflicting in-progress DATA metadata cause the offending client connection to be closed. The daemon does not attempt to continue a connection whose record stream can no longer be trusted.

Ordinary valid requests that fail because of current service/link state receive explicit VSPD status responses instead.

## Memory/resource model

The first daemon is single-threaded and `poll()` driven.

Resource storage is fixed-size after initialization:

- two virtual-port state objects;
- bounded packet/time-code queues;
- one fixed DATA_TX reassembly arena per accepted client;
- one small pending synchronous response per client.

The server aggregate is allocated once at daemon startup because the fixed reassembly/packet arenas are several MiB and do not belong on the process stack. No per-packet dynamic allocation is required in the data path.

`SPWKIT_ENABLE_HEAP=OFF` continues to govern the library's hosted `spw_port_open()` convenience path; it does not prohibit an independently hosted daemon executable from allocating its bounded server object at initialization.

## CI evidence

The dedicated **Virtual device** workflow builds the VSPD codec and `vspwd` with both GCC and Clang using:

```text
CXX=/bin/false
-Wall -Wextra -Werror
```

It runs:

- VSPD golden/malformed codec tests;
- Unix seqpacket/poll/disconnect tests;
- a real daemon + survivor + peer restart process scenario;
- a real daemon edge scenario covering duplicate ATTACH, statistics/clear and malformed-client disconnect;
- daemon CLI smoke coverage.

A separate ASan+UBSan device job runs the protocol and daemon process tests with sanitizers enabled.

The portable `libspwkit.a` remains checked for accidental socket/C++ runtime references. `vspwd` is a separate hosted executable, so adding the daemon does not contaminate the portable C library core.

## What is not implemented yet

This daemon slice does **not** yet provide:

- public `SPW_BACKEND_DEVICE` client integration;
- `/dev/vspwX`/CUSE presentation;
- `spwctl`;
- `spwmon`;
- VSPW-TP/UDP bridging inside the daemon;
- router/topology configuration;
- physical SpaceWire hardware.

The next v0.4 slice is the C Linux-device backend that speaks VSPD internally while preserving the normal application-facing `spw_port_*` contract.
