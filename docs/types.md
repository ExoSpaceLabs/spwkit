# Core public types

This document defines the v0.1 software-visible value types used by `libspwkit`.

The types are intentionally independent from simulator internals, operating systems, DMA engines, RTOS objects, and vendor SDKs.

## Result values

`spw_result_t` is a signed 32-bit integer. `SPW_OK` is zero and failures are negative.

The v0.1 common results are:

| Result | Meaning |
|---|---|
| `SPW_OK` | operation completed successfully |
| `SPW_ERR_INVALID_ARGUMENT` | invalid pointer, value, or configuration |
| `SPW_ERR_INVALID_STATE` | operation is not legal in the current port/link state |
| `SPW_ERR_TIMEOUT` | operation did not complete inside the requested timeout |
| `SPW_ERR_UNSUPPORTED` | optional capability is not provided by the backend |
| `SPW_ERR_RESOURCE_EXHAUSTED` | queue, pool, or other bounded resource is exhausted |
| `SPW_ERR_LINK_UNAVAILABLE` | peer/link is unavailable or disconnected |
| `SPW_ERR_BUFFER_TOO_SMALL` | receive storage cannot hold the complete packet |
| `SPW_ERR_INVALID_PACKET` | packet metadata or terminator is invalid |
| `SPW_ERR_BACKEND` | backend-specific failure that cannot be represented more precisely |

Backends may retain additional diagnostics internally, but portable application control flow must be possible using these values.

## Timeouts

Timeouts use `spw_timeout_us_t`, an unsigned 64-bit number of microseconds.

```text
SPW_TIMEOUT_IMMEDIATE = 0
SPW_TIMEOUT_INFINITE  = UINT64_MAX
```

This avoids POSIX `timespec`, RTOS ticks, or simulator-specific time units in the public ABI.

## Packet terminator

A complete SpaceWire packet terminates with either:

```text
SPW_TERMINATOR_EOP
SPW_TERMINATOR_EEP
```

EOP is normal packet termination. EEP indicates an error end-of-packet.

A backend must preserve the software-visible terminator. It must not silently convert EEP to EOP.

## Packet type

Copied packet I/O uses:

```c
spw_packet_t {
    uint8_t* data;
    size_t length;
    size_t capacity;
    spw_terminator_t terminator;
}
```

### Send

For `spw_port_send`:

- `data` points to caller-owned bytes;
- `length` is the number of bytes to transmit;
- `capacity` is not used to extend the transmitted packet and shall be at least `length` when non-zero;
- `terminator` selects EOP or EEP;
- ownership remains with the caller for the duration of the call.

### Receive

For `spw_port_receive`:

- `data` points to caller-provided writable storage;
- `capacity` is the available storage size;
- on success, `length` is the complete received payload length;
- `terminator` is set to the received packet terminator.

A backend must never silently truncate a packet. If the next complete packet is larger than `capacity`:

1. the backend returns `SPW_ERR_BUFFER_TOO_SMALL`;
2. `length` reports the complete required payload size;
3. `terminator` reports the packet's EOP/EEP value;
4. caller payload storage is not partially overwritten;
5. the complete packet remains queued and may be retried with adequate storage.

If a non-empty packet is queued and the caller provides `data == NULL`, receive returns `SPW_ERR_INVALID_ARGUMENT` and retains the packet for a valid retry.

Exact-fit, maximum-size, one-byte-oversize, zero-length, undersized-buffer retention, null-storage retention and queue-boundary behavior are part of the v0.1 automated edge suite.

Zero-length packets are valid at the API level and still carry an EOP or EEP terminator.

## Link state

The public link-state type provides the ECSS-oriented SpaceWire exchange-state vocabulary:

```text
SPW_LINK_ERROR_RESET = 0
SPW_LINK_ERROR_WAIT  = 1
SPW_LINK_READY       = 2
SPW_LINK_STARTED     = 3
SPW_LINK_CONNECTING  = 4
SPW_LINK_RUN         = 5
```

The values are fixed-width and explicitly numbered because they are part of the C ABI.

A backend maps its observable state into this common vocabulary. It is **not required to manufacture intermediate states that its software model cannot meaningfully observe**. For example, the v0.1 process-local simulator exposes these stable application-visible transitions:

```text
open/reset              -> ERROR_RESET
stop                    -> READY
start without peer      -> CONNECTING
both peers started      -> RUN
peer stop/reset/close   -> surviving started peer CONNECTING
peer restart/reopen     -> RUN
```

`ERROR_WAIT` and `STARTED` remain valid common states for backends whose controller/driver can observe those ECSS exchange-state phases. The process-local simulator does not synthesize transient timing states merely to make every enum value appear.

Hardware/vendor drivers with a coarser or differently named internal state machine must map to the closest semantically correct public value and must not expose vendor-specific state identifiers through the common API.

The v0.1 simulator edge suite verifies repeated start/stop/reset, missing-peer behavior, duplicate endpoint rejection, peer stop/reset/close, reconnect/reopen and preservation of the surviving port handle.

## Time code

`spw_time_code_t` contains:

```c
uint8_t time_count;
uint8_t control_flags;
```

`time_count` is constrained to 0..63.

SpaceWire time-codes carry a six-bit time count in the least-significant bits and two control bits in the most-significant bits. For ordinary v0.1 time-code operation, `control_flags` must be zero. Broader ECSS-E-ST-50-12C Rev.1 broadcast-code functionality is intentionally reserved for a later extension.

The simulator preserves the same time-code values seen by the application. It does not invent host timestamps as a substitute for SpaceWire time-code semantics.

## Capabilities

Optional functionality is represented by `spw_capabilities_t`.

The initial capability flags are:

```text
SPW_CAP_EEP
SPW_CAP_TIME_CODE
SPW_CAP_LINK_CONTROL
SPW_CAP_STATISTICS
SPW_CAP_RATE_CONTROL
SPW_CAP_FAULT_INJECTION
SPW_CAP_ZERO_COPY
```

Capabilities also report implementation/resource constraints:

- maximum packet size;
- TX queue depth;
- RX queue depth;
- required buffer alignment.

A zero value for a resource limit means "not specified/unbounded by the common layer" unless a backend contract defines a tighter interpretation.

A backend advertises only capabilities it actually implements. The shared contract test treats advertised optional behavior as mandatory for that backend, including zero-copy ownership semantics.

## Statistics

The v0.1 common statistics baseline contains:

- transmitted packets and bytes;
- received packets and bytes;
- transmitted/received time codes;
- EEP packet count;
- link error count;
- dropped packet count.

The counters are diagnostic and verification-facing. Additional hardware-specific counters belong in backend extension APIs rather than the portable structure.

## Zero-copy relationship

The copied `spw_packet_t` API is mandatory.

Optional zero-copy operation is a second transfer path that uses backend-managed buffers with explicit ownership transitions. It does not change the SpaceWire packet, terminator, link-state, or time-code semantics defined here.

In v0.1 the simulator emulates those ownership transitions using ordinary aligned host memory. A future hardware backend may map the same operations onto DMA-capable memory without exposing DMA descriptors or physical addresses to applications.
