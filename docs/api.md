# Public API Contract

SpWKit uses a C ABI as the portability baseline. C++ applications may use the same ABI directly and a higher-level wrapper can remain layered above it without requiring backends to expose C++ implementation details.

This document defines the v0.2.0 application-facing contract while preserving the v0.1 portable-core baseline.

## Design rule

Applications interact with **SpWKit**, not directly with a simulator, Linux device, Ethernet transport, FPGA, DMA engine, RTOS primitive, or vendor SDK.

The library translates the same public operations into the selected backend implementation:

```text
Application
    |
    v
+-----------------------------+
|        libspwkit API        |
| spw_port_* / packet / state |
+--------------+--------------+
               |
               | backend contract
               |
   +-----------+-----------+----------------+----------------+
   |                       |                |                |
   v                       v                v                v
Simulator              VSPW-TP/UDP      Embedded        Vendor/HW
backend                backend          backend         backend
   |                       |                |                |
local virtual link      IP network       MMIO/DMA       vendor API
```

The simulator and UDP transport are therefore implementations of the same SpaceWire-facing contract, not alternate application-facing APIs.

## ABI primitives

`spw_result_t` uses a fixed-width signed 32-bit representation. Public result constants therefore do not depend on compiler enum width.

`spw_timeout_us_t` is an unsigned 64-bit timeout expressed in microseconds. The API intentionally does not expose `timespec`, operating-system ticks, or scheduler-specific timeout types.

`spw_port_t` and `spw_buffer_t` are opaque. Applications must not depend on their size or fields.

## Port lifecycle

The public lifecycle operations are:

```text
spw_port_open
spw_port_open_in_place
spw_port_close
spw_port_start
spw_port_stop
spw_port_reset
```

`open` creates or attaches to one configured SpaceWire port implementation. `spw_port_open_in_place()` provides the allocation-free construction path using caller-owned workspace. The hosted `spw_port_open()` convenience path may allocate and can be disabled at build time.

`start`, `stop`, and `reset` represent software-visible SpaceWire link control. A backend may internally translate them to simulator state changes, socket/backend state, driver calls, MMIO writes, or vendor API operations.

## Backend selection

Backends are selected by `spw_port_config_t` plus an optional backend-specific configuration object.

Implemented backend identifiers currently include:

- `SPW_BACKEND_LOOPBACK`;
- `SPW_BACKEND_SIMULATOR`;
- `SPW_BACKEND_UDP` on supported POSIX hosts.

The common port operations do not change when the selected backend changes.

The UDP backend configuration carries portable descriptive values such as numeric IPv4 addresses, UDP ports, virtual `link_id`, fragment payload size, virtual timing controls and fixed deterministic fault rules. Native socket handles and platform socket structures remain internal.

## Link state

`spw_port_get_link_state` reports software-visible link state through `spw_link_state_t`.

All backends map implementation state into the common SpaceWire-oriented model rather than returning operating-system or hardware-vendor states directly.

A backend is not required to fabricate transient ECSS states it cannot meaningfully observe.

## Capabilities

`spw_port_get_capabilities` declares optional functionality and bounded resources. Contract tests use capabilities to determine which optional behaviours are applicable.

Capability areas include:

- time-code support;
- EEP support;
- link-state control;
- configurable rate;
- statistics/counters;
- deterministic fault injection;
- zero-copy ownership-oriented packet transfer.

`max_packet_size`, queue-depth values, and `buffer_alignment` describe backend limits relevant to packet and zero-copy operation.

## Copied packet transfer

The mandatory packet operations are:

```text
spw_port_send
spw_port_receive
```

A packet is a complete software-visible SpaceWire packet carrying caller storage, payload length/capacity, and EOP/EEP termination.

Packet ownership remains with the caller for the copied API. A conforming backend must not silently convert EEP to EOP or expose internal fragmentation as multiple application packets.

A simulator may copy internally, the UDP backend may fragment/reassemble over VSPW-TP, and a hardware backend may use one or more DMA descriptors internally. Those details remain below the public boundary.

## Receive-capacity rule

SpWKit does not silently truncate packets.

If the next complete packet is larger than caller receive capacity:

- `SPW_ERR_BUFFER_TOO_SMALL` is returned;
- the required complete payload length is reported;
- the EOP/EEP terminator is reported;
- caller payload storage is not partially overwritten;
- the complete packet remains available for retry.

This rule applies to the local reference backends and to completed packets reassembled by the distributed UDP backend.

## Optional zero-copy buffer path

Backends advertising `SPW_CAP_ZERO_COPY` expose the ownership-oriented operations declared in `spwkit/buffer.h`.

TX lifecycle:

```text
acquire -> application fills -> submit -> backend owns -> reclaim
                                                ^             |
                                                |             v
                                                +--- reuse/release
```

RX lifecycle:

```text
backend receives -> acquire -> application inspects -> release
```

The concrete public operation family is:

```text
spw_port_acquire_tx_buffer
spw_buffer_get_view
spw_buffer_set_packet
spw_port_submit_tx_buffer
spw_port_reclaim_tx_buffer
spw_port_release_tx_buffer

spw_port_acquire_rx_buffer
spw_buffer_get_view
spw_port_release_rx_buffer
```

Successful submit/release operations clear the caller's buffer pointer to make ownership transfer explicit. Failed operations do not steal application ownership.

The public contract models **buffer ownership transitions**, not DMA implementation details. A future DMA-capable backend may map these handles to pinned/coherent memory or descriptor rings. The process-local simulator emulates the same ownership contract using fixed host memory.

Physical addresses, DMA descriptor types, AXI addresses, Linux `dma_addr_t`, file descriptors, and vendor handles are not exposed by the portable ABI.

The copied packet API remains the mandatory baseline regardless of zero-copy capability.

See `docs/buffers.md` for the complete ownership, alignment, exhaustion, simulator-emulation, and future-DMA mapping rules.

## Scatter/gather

Scatter/gather packet buffers are deferred beyond v0.1. One `spw_buffer_t` represents one contiguous packet payload. A future scatter/gather extension must be capability-gated and define segment ownership explicitly rather than changing the meaning of the v0.1 buffer object.

## Process-local simulator backend

The process-local simulator introduced in v0.1 remains the primary local runtime reference backend.

Applications invoke only `libspwkit` operations. The selected simulator backend translates those calls to local virtual-link state, queues, packet transfer, time codes, and zero-copy ownership emulation.

```text
wrong:
Application -> simulator API

correct:
Application -> libspwkit -> simulator backend
```

## Distributed UDP backend

v0.2.0 adds the distributed VSPW-TP/UDP backend:

```text
Application A                         Application B
    |                                    |
libspwkit                            libspwkit
    |                                    |
SPW_BACKEND_UDP                     SPW_BACKEND_UDP
    |                                    |
    +----------- VSPW-TP/UDP ------------+
```

The application API remains the same. VSPW-TP framing and UDP sockets are backend internals.

The v0.2.0 backend implements:

- IPv4 UDP on supported POSIX hosts;
- versioned VSPW-TP v1 framing;
- bounded fragmentation/reassembly;
- EOP/EEP preservation;
- time-code transfer;
- timeout/statistics support;
- a 1 MiB backend logical packet/reassembly limit;
- default 1200-byte transport fragments;
- session-bound ACK/retransmission and duplicate suppression;
- peer liveness/disconnect/restart recovery;
- bounded arbitrary-order fragment reassembly;
- deterministic virtual SpaceWire rate/latency timing;
- deterministic seeded transport drop/duplicate/reorder/delay injection;
- explicit SpaceWire-side EEP injection with separate fault-domain diagnostics.

The UDP backend runs the reusable shared public contract, process and Linux network-namespace integration, deterministic timing/fault scenarios, and VSPW-TP capture/Wireshark validation. The hosted runtime is supported on POSIX hosts according to `docs/platform-support.md`; native Winsock transport is deferred beyond v0.2.0.

## Time codes

The optional time-code operations are:

```text
spw_port_send_time_code
spw_port_receive_time_code
```

Backends that do not support time codes report the corresponding unsupported-operation result and must not advertise the capability.

VSPW-TP transports time-code events separately from DATA packets.

## Statistics

The API exposes:

```text
spw_port_get_statistics
spw_port_clear_statistics
```

Statistics are backend-independent counters intended for diagnostics and verification.

Fault-capable backends additionally expose:

```text
spw_port_get_fault_statistics
spw_port_clear_fault_statistics
```

`spw_fault_statistics_t` deliberately separates VSPW-TP transport drop/duplicate/reorder/delay counters from SpaceWire-visible EEP injection. Backends without `SPW_CAP_FAULT_INJECTION` return `SPW_ERR_UNSUPPORTED` for these operations. The existing `spw_statistics_t` layout remains unchanged.

## Blocking and timeouts

Packet, time-code, and buffer acquisition/completion operations use timeouts expressed in microseconds.

The API does not require POSIX blocking semantics. Implementations may use polling, interrupts, RTOS events, Linux wait queues, socket polling, simulator scheduling, condition variables, or vendor mechanisms.

`SPW_TIMEOUT_IMMEDIATE` requests non-blocking behaviour and `SPW_TIMEOUT_INFINITE` requests an unbounded wait where supported.

## Error model

Every public operation returns `spw_result_t`. The result set distinguishes success, invalid arguments/state, timeout, unsupported operation, resource exhaustion, link unavailability, buffer-size errors, invalid packets, and backend/internal failures.

Backends may retain additional diagnostic information internally, but common application behaviour must be expressible through the portable result model.

## Backend independence

The following must never appear in mandatory common operation signatures:

- POSIX file descriptors;
- native socket structures/handles;
- Linux `ioctl` request types;
- AXI/MMIO addresses;
- DMA descriptor or physical-address types;
- RTOS task/semaphore handles;
- vendor SDK handles.

Such values belong to backend-specific configuration/extension APIs or remain internal.

## Contract-test mapping

Every backend is expected to satisfy the shared application-visible contract for the capabilities it advertises.

The shared suite runs through `libspwkit` against loopback, the local simulator and the v0.2 UDP backend. Distributed-specific extensions cover peer loss/restart, while D2D tests cover framing, fragmentation/reordering, reliability, timing, deterministic faults and process/network isolation. Future embedded, `/dev/spwX`, and HIL backends should reuse the same capability-driven contract where applicable.

## Versioning

The public ABI carries explicit major/minor/patch version macros.

Before v1.0, incompatible API changes are permitted but must update documentation and tests together. After v1.0, ABI-breaking changes require a major version change.

Release tags identify coherent milestones. Development on `main` may contain post-release functionality before the project/package version is advanced for the next release cycle; documentation should distinguish the released baseline from current development state.
