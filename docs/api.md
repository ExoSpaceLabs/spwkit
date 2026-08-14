# Public API Contract

SpWKit uses a C ABI as the portability baseline. The C++ interface is layered above that ABI and must not require backends to expose C++ implementation details.

This document defines the v0.1 API contract boundary.

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
Simulator              Linux device     Embedded        Vendor/HW
backend                backend          backend         backend
   |                       |                |                |
virtual link           /dev/spwX        MMIO/DMA       vendor API
```

The simulator is therefore an implementation of the same SpaceWire-facing contract, not an alternate application-facing API.

## ABI primitives

`spw_result_t` uses a fixed-width signed 32-bit representation. Public result constants therefore do not depend on compiler enum width.

`spw_timeout_us_t` is an unsigned 64-bit timeout expressed in microseconds. The API intentionally does not expose `timespec`, operating-system ticks, or scheduler-specific timeout types.

`spw_port_t` and `spw_buffer_t` are opaque. Applications must not depend on their size or fields.

## Port lifecycle

The mandatory lifecycle operations are:

```text
spw_port_open
spw_port_open_in_place
spw_port_close
spw_port_start
spw_port_stop
spw_port_reset
```

`open` creates or attaches to one configured SpaceWire port implementation. `spw_port_open_in_place()` provides the mandatory allocation-free construction path using caller-owned workspace. The hosted `spw_port_open()` convenience path may allocate and can be disabled at build time.

`start`, `stop`, and `reset` represent software-visible SpaceWire link control. A backend may internally translate them to simulator state changes, driver calls, MMIO writes, or vendor API operations.

## Link state

`spw_port_get_link_state` reports the software-visible link state through `spw_link_state_t`.

All backends map implementation state into the common SpaceWire-oriented model rather than returning operating-system or hardware-vendor states directly.

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

Packet ownership remains with the caller for the copied API. A conforming backend must not silently convert EEP to EOP or merge/split software-visible packet boundaries.

The simulator may fragment or copy internally, and a hardware backend may use one or more DMA descriptors internally, but those details remain below the public boundary.

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

The public contract models **buffer ownership transitions**, not DMA implementation details. A future DMA-capable backend may map these handles to pinned/coherent memory or descriptor rings. The simulator emulates the same ownership contract using fixed host memory.

Physical addresses, DMA descriptor types, AXI addresses, Linux `dma_addr_t`, file descriptors, and vendor handles are not exposed by the portable ABI.

The copied packet API remains the mandatory baseline regardless of zero-copy capability.

See `docs/buffers.md` for the complete ownership, alignment, exhaustion, simulator-emulation, and future-DMA mapping rules.

## Scatter/gather

Scatter/gather packet buffers are deferred for v0.1. One `spw_buffer_t` represents one contiguous packet payload. A future scatter/gather extension must be capability-gated and define segment ownership explicitly rather than changing the meaning of the v0.1 buffer object.

## Simulator backend

For v0.1, the simulator is the primary runtime reference backend.

Applications invoke only `libspwkit` operations. The selected simulator backend translates those calls to virtual link state, queues, packet transfer, time codes, and zero-copy ownership emulation.

```text
wrong:
Application -> simulator API

correct:
Application -> libspwkit -> simulator backend
```

The same application-facing operations can later target a Linux device, RTOS implementation, or physical DMA-capable backend without changing application SpaceWire logic.

## Time codes

The optional time-code operations are:

```text
spw_port_send_time_code
spw_port_receive_time_code
```

Backends that do not support time codes report the corresponding unsupported-operation result and must not advertise the capability.

## Statistics

The API exposes:

```text
spw_port_get_statistics
spw_port_clear_statistics
```

Statistics are backend-independent counters intended for diagnostics and verification. Backend-specific counters may later be available through extension APIs without contaminating the common structure.

## Blocking and timeouts

Packet, time-code, and buffer acquisition/completion operations use timeouts expressed in microseconds.

The API does not require POSIX blocking semantics. Implementations may use polling, interrupts, RTOS events, Linux wait queues, simulator scheduling, condition variables, or vendor mechanisms.

`SPW_TIMEOUT_IMMEDIATE` requests non-blocking behaviour and `SPW_TIMEOUT_INFINITE` requests an unbounded wait where supported.

## Error model

Every public operation returns `spw_result_t`. The result set distinguishes success, invalid arguments/state, timeout, unsupported operation, resource exhaustion, link unavailability, buffer-size errors, invalid packets, and backend/internal failures.

Backends may retain additional diagnostic information internally, but common application behaviour must be expressible through the portable result model.

## Backend independence

The following must never appear in mandatory common API signatures:

- POSIX file descriptors;
- socket addresses;
- Ethernet MAC/IP addresses;
- Linux `ioctl` request types;
- AXI/MMIO addresses;
- DMA descriptor or physical-address types;
- RTOS task/semaphore handles;
- vendor SDK handles.

Such values belong to backend-specific configuration or extension APIs.

## Contract-test mapping

Every mandatory backend is expected to run the shared contract suite. At minimum it verifies lifecycle, link state, capabilities, copied packet boundaries, EOP/EEP, timeout behaviour, time codes/statistics where supported, reset/recovery, and zero-copy ownership when `SPW_CAP_ZERO_COPY` is advertised.

For v0.1, the suite runs through `libspwkit` against loopback and the local simulator backend. The same behavioural tests should later execute against Ethernet, embedded, `/dev/spwX`, and HIL backends where the capability profile permits.

## Versioning

The public ABI carries explicit major/minor/patch version macros.

Before v1.0, incompatible API changes are permitted but must update documentation and tests together. After v1.0, ABI-breaking changes require a major version change.
