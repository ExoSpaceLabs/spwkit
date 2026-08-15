# Local virtual SpaceWire simulator

The v0.1 simulator is a process-local implementation of the same backend contract used by `libspwkit` for loopback, the distributed UDP backend, and future physical transports.

Applications do not call simulator-specific transport functions. They select the simulator through `spw_port_config_t` and continue to use the normal `spw_port_*` API.

```text
Application A                         Application B
     |                                    |
     v                                    v
+------------+                       +------------+
| libspwkit  |                       | libspwkit  |
| endpoint A |                       | endpoint B |
+------+-----+                       +------+-----+
       |                                    |
       +----------- virtual link -----------+
                    link_id = N
```

Endpoints A and B are equal SpaceWire peers. The endpoint identifiers exist only to make pairing deterministic. They do not imply client/server, initiator/responder, or master/slave roles.

## Creating a pair

Both ports select `SPW_BACKEND_SIMULATOR`, use the same `link_id`, and select opposite endpoints:

```c
spw_simulator_config_t sim_a = SPW_SIMULATOR_CONFIG_INITIALIZER;
sim_a.link_id = 42;
sim_a.endpoint = SPW_SIMULATOR_ENDPOINT_A;

spw_simulator_config_t sim_b = SPW_SIMULATOR_CONFIG_INITIALIZER;
sim_b.link_id = 42;
sim_b.endpoint = SPW_SIMULATOR_ENDPOINT_B;
```

Each simulator configuration is passed through the ordinary `spw_port_config_t` backend configuration field. `spw_port_open()` copies the identity needed by the backend; the application does not have to retain the configuration objects after the call returns.

A process may host multiple independent virtual links. v0.1 reserves a fixed registry of 16 local links. Opening the same endpoint twice on one link is rejected with `SPW_ERR_RESOURCE_EXHAUSTED`.

## Link lifecycle

New endpoints begin in `SPW_LINK_ERROR_RESET`.

Starting only one endpoint places it in `SPW_LINK_CONNECTING`:

```text
A started, B not started

A: CONNECTING  <---- virtual link ---->  B: ERROR_RESET
```

Once both peers are attached and started, both enter `SPW_LINK_RUN`:

```text
A: RUN         <---- virtual link ---->  B: RUN
```

Stopping, resetting, or closing one peer makes a still-started peer return to `SPW_LINK_CONNECTING`. Packet and time-code transmission from that surviving endpoint returns `SPW_ERR_LINK_UNAVAILABLE` until the opposite peer is available again.

A missing peer can be reopened using the same `link_id` and endpoint identity. Starting it restores both started endpoints to `SPW_LINK_RUN`; the surviving `spw_port_t` handle does not need to be recreated.

This is a software-visible behavioral model. It does not claim to reproduce every intermediate timing state of the ECSS SpaceWire link state machine at character or signal level.

## Packet transfer

Each endpoint owns an independent inbound packet queue. Sending on A writes to B's queue and sending on B writes to A's queue.

```text
                 packet queue B
A TX  --------------------------------->  B RX

                 packet queue A
A RX  <---------------------------------  B TX
```

The v0.1 local simulator preserves:

- complete packet boundaries;
- payload bytes;
- EOP/EEP termination;
- zero-length packets;
- receive-buffer-too-small behavior without consuming the queued packet;
- bidirectional/full-duplex use from independent threads.

The current limits are:

| Resource | v0.1 local simulator |
|---|---:|
| maximum packet payload | 4096 bytes |
| inbound packet queue | 8 packets per endpoint |
| inbound time-code queue | 8 time codes per endpoint |
| local virtual links | 16 |

An immediate send to a full peer queue returns `SPW_ERR_RESOURCE_EXHAUSTED`. A finite or infinite timeout may wait for queue space. Receive operations similarly support immediate, finite, and infinite waits.

## Time codes

Time codes use the same peer direction as packets. A time code sent by A is received by B and vice versa. Ordinary v0.1 time-code validation remains limited to a six-bit count with zero control flags, as defined by the public type contract.

## Disconnect and recovery

Closing or resetting an endpoint notifies its peer. The peer remains a valid application object but is no longer considered connected.

```text
before:
A: RUN  <----------------->  B: RUN

B closes:
A: CONNECTING              B: absent

B reopens + starts:
A: RUN  <----------------->  B: RUN
```

This provides the deterministic disconnect/recovery baseline needed by the simulator contract without inventing Ethernet or operating-system failure semantics.

## Statistics

Statistics are maintained per endpoint. TX counters belong to the transmitting endpoint and RX counters belong to the endpoint that consumes the queued item. Link error counters increase when a running peer disappears or resets.

## Threading

The local virtual link is synchronized internally and supports concurrent A-to-B and B-to-A operations. This permits full-duplex integration tests without requiring a network transport.

The lifetime of a single `spw_port_t` object must still be coordinated by the application. Closing a port concurrently with another thread using that same handle is outside the v0.1 contract.

## Zero-copy ownership

The local simulator advertises `SPW_CAP_ZERO_COPY` and implements the same ownership-oriented API available to applications:

```text
TX: acquire -> fill -> submit -> backend owns -> reclaim -> reuse/release
RX: backend receives -> acquire -> inspect -> release
```

The simulator uses fixed aligned host-memory buffers and may copy internally while preserving the application-visible ownership, capacity, timeout and completion semantics.

This is intentional. A future DMA-capable backend can map the same API to coherent/pinned memory and descriptor rings without exposing hardware addresses or descriptor types to applications.

## Relationship to distributed simulation

The process-local simulator remains the v0.1 behavioral reference. Current `main` additionally contains the first v0.2 VSPW-TP/UDP backend:

```text
Application
    |
libspwkit
    |
    +-- local simulator       <- v0.1
    +-- VSPW-TP / UDP         <- v0.2 in progress
    +-- vspwd / /dev/vspwX   <- later
    +-- RTOS / bare metal     <- later
    +-- FPGA / DMA hardware   <- later
```

The UDP backend is a separate backend, not an extension of the process-local simulator API. Both remain hidden behind `libspwkit` and share the same SpaceWire-facing concepts.

## What this simulator is not

The process-local backend is not `vspwd`, a Linux character device, an electrical/Data-Strobe simulator, or a physical SpaceWire implementation.

It establishes the packet/link behavioral reference before device-driver and physical transports are introduced.
