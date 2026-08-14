# Public API Contract

SpWKit uses a C ABI as the portability baseline. The C++ interface is layered above that ABI and must not require backends to expose C++ implementation details.

This document defines the v0.1 API contract boundary. Concrete packet layouts, result codes, link-state values, capability flags, statistics fields, time-code representation, and optional zero-copy buffer semantics are completed by the core-types and buffer-API work.

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

`spw_result_t` uses a fixed-width signed 32-bit representation. Public result constants will therefore not depend on compiler enum width.

`spw_timeout_us_t` is an unsigned 64-bit timeout expressed in microseconds. The API intentionally does not expose `timespec`, operating-system ticks, or scheduler-specific timeout types.

`spw_port_t` is opaque. Applications must not depend on its size or fields.

## Port lifecycle

The mandatory lifecycle operations are:

```text
spw_port_open
spw_port_close
spw_port_start
spw_port_stop
spw_port_reset
```

`open` creates or attaches to one configured SpaceWire port implementation. The configuration object selects or supplies a backend without exposing backend-specific types through the common operations.

`close` releases resources owned by the port instance. The concrete memory/allocation policy is backend-dependent, but the portable core must provide a path that does not require heap allocation.

`start`, `stop`, and `reset` represent software-visible SpaceWire link control. A backend may internally translate them to simulator state changes, driver calls, MMIO writes, or vendor API operations.

## Link state

`spw_port_get_link_state` reports the software-visible link state through `spw_link_state_t`.

The concrete state representation will be defined separately, but all backends must map their implementation state into the common SpaceWire-oriented model rather than return operating-system or hardware-vendor states directly.

## Capabilities

`spw_port_get_capabilities` allows a backend to declare optional functionality.

This prevents the public API from requiring every backend to fake support for features it cannot provide. Contract tests use capabilities to determine which optional behaviours are applicable.

Examples of capability areas include:

- time-code support;
- EEP support;
- link-state control;
- configurable rate;
- statistics/counters;
- deterministic fault injection;
- zero-copy operation.

Capability constants and representation are defined by the core-types layer.

## Packet transfer

The mandatory packet operations are:

```text
spw_port_send
spw_port_receive
```

A packet is a complete software-visible SpaceWire packet. The public packet representation must carry, directly or indirectly:

- caller-provided data storage;
- payload length/capacity as applicable;
- packet terminator (`EOP` or `EEP`).

Packet ownership remains with the caller for the copied packet API. A conforming backend must not silently convert EEP to EOP or merge/split software-visible packet boundaries.

The simulator may fragment a packet internally, and a hardware backend may use one or more DMA descriptors internally, but those details remain below the public boundary.

## Optional zero-copy buffer path

High-throughput implementations may provide an optional buffer-oriented path in addition to copied `send`/`receive`.

The public contract must model **buffer ownership transitions**, not DMA implementation details. Conceptually the supported lifecycle is expected to be:

```text
TX: acquire -> application fills -> submit -> backend owns -> reclaim
RX: backend fills -> acquire -> application reads -> release
```

A future DMA-capable backend may map those buffers to pinned/coherent/DMA-capable memory. The simulator backend must be able to emulate the same ownership semantics using ordinary host memory so applications and contract tests can exercise the interface before physical hardware exists.

The public ABI must not expose physical addresses, DMA descriptor types, AXI addresses, Linux `dma_addr_t`, or vendor handles.

The copied packet API remains the mandatory baseline. Zero-copy is capability-gated and optional.

## Simulator backend

For v0.1, the simulator is the primary runtime reference backend.

Applications still invoke only `libspwkit` operations. The selected simulator backend translates those calls to virtual link state, queues, packet transfer, time codes, faults, and optional zero-copy ownership behavior.

This distinction is important:

```text
wrong:
Application -> simulator API

correct:
Application -> libspwkit -> simulator backend
```

The same application-facing operations should later target a Linux device, RTOS implementation, or physical DMA-capable backend without changing the application's SpaceWire logic.

## Time codes

The optional time-code operations are:

```text
spw_port_send_time_code
spw_port_receive_time_code
```

Backends that do not support time codes report the corresponding unsupported-operation result. Backends must advertise time-code capability consistently with their behaviour.

## Statistics

The API exposes:

```text
spw_port_get_statistics
spw_port_clear_statistics
```

Statistics are backend-independent counters intended for diagnostics and verification. Backend-specific counters may later be available through extension APIs, but they must not contaminate the common structure.

## Blocking and timeouts

Packet and time-code operations accept a timeout expressed in microseconds.

The API does not require POSIX blocking semantics. Implementations may use polling, interrupts, RTOS events, Linux wait queues, simulator scheduling, or vendor mechanisms.

The core-types work will define common timeout constants for immediate/non-blocking operation and, if retained, an explicit infinite wait value.

## Error model

Every public operation returns `spw_result_t`.

The concrete result set must distinguish at least:

- success;
- invalid argument/configuration;
- invalid state;
- timeout/would-block;
- unsupported operation;
- queue/resource exhaustion;
- link unavailable/disconnected;
- packet/storage too small or invalid;
- backend/internal error.

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

Every mandatory backend is expected to run the shared contract suite. At minimum the suite will verify:

| API area | Required behaviour |
|---|---|
| lifecycle | open/close and valid start/stop/reset transitions |
| link state | state can be queried and reflects lifecycle changes |
| capabilities | stable and internally consistent capability reporting |
| packets | bidirectional transfer with boundaries preserved |
| terminators | EOP/EEP preserved where applicable |
| timeout | bounded waiting/non-blocking behaviour is deterministic |
| time codes | transfer or explicit unsupported result |
| statistics | counters update consistently where supported |
| recovery | backend returns to a defined state after reset/disconnect scenarios |
| zero-copy | ownership/acquire/submit/reclaim semantics where capability is advertised |

For v0.1, these tests must run through `libspwkit` against loopback and the local simulator backend. The same behavioural tests should later execute against Ethernet, embedded, `/dev/spwX`, and HIL backends where the capability profile permits.

## Versioning

The public ABI carries explicit major/minor/patch version macros.

Before v1.0, incompatible API changes are permitted but must update documentation and tests together. After v1.0, ABI-breaking changes require a major version change.

The package version and API/ABI version may eventually be managed separately if backend/tool releases need to evolve without changing the public ABI.
