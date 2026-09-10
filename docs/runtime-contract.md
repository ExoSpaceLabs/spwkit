# Runtime behavior contract

This document defines the portable runtime behavior that SpWKit intends to carry into the stable 1.x software contract. Backend-specific documents may describe stronger guarantees, but they must not weaken or contradict the rules below for behavior exposed through the common `spw_port_*` and `spw_buffer_*` APIs.

The contract is deliberately about application-visible behavior. Socket ownership, RTOS primitives, DMA descriptors, FPGA registers, interrupt models and provider-specific diagnostics remain implementation details.

## Port lifetime and threading

A `spw_port_t` is an opaque runtime object with explicit application-managed lifetime.

### Independent ports

Distinct `spw_port_t` handles may be operated concurrently. SpWKit does not impose a process-wide or library-wide serialization requirement.

Two handles may still refer to a shared implementation resource. For example, two DRIVER ports may use provider contexts that ultimately share one controller. In that case the provider is responsible for synchronizing the shared native resource, or the application must impose the provider-specific serialization required by that resource.

### One port handle

The portable 1.x contract does **not** permit overlapping public operations on the same `spw_port_t` handle. The application must serialize calls that use one port handle, including:

- copied TX and RX;
- time-code TX and RX;
- readiness waits;
- statistics and capability queries;
- lifecycle operations;
- zero-copy acquire, submit, reclaim and release operations.

A backend may internally be able to tolerate more concurrency, but applications that depend on such backend-specific behavior are outside the portable contract.

This rule does not change SpaceWire's full-duplex link semantics. Opposite physical directions remain independent. It defines the synchronization contract of one software handle. Applications may use non-blocking/finite-timeout event loops, or distinct handles where the selected topology/provider exposes them, without assuming undocumented same-handle reentrancy.

### Lifetime operations

`spw_port_close()` requires exclusive ownership of the handle. No other call may be in flight and no new call may begin after close starts. After close returns, the handle, its in-place workspace object, and every buffer/view originating from it are no longer valid application objects.

`spw_port_stop()` and `spw_port_reset()` are lifecycle operations, not asynchronous cancellation primitives. They must not race with another call on the same handle. A caller that needs bounded cancellation must use finite-timeout operations and coordinate lifecycle changes after the operation returns.

## DRIVER callback synchronization and reentrancy

SpWKit calls `spw_driver_ops_t` synchronously from the public operation that triggered the callback.

A provider must obey these rules:

- callbacks for one SpWKit port are invoked under the same application-side serialization rule as that port;
- a callback must not re-enter a public SpWKit operation on the **same** `spw_port_t`;
- a provider may call unrelated SpWKit ports if those ports satisfy their own normal lifetime/threading rules;
- if multiple DRIVER ports share one native controller/context, synchronization of that shared native state is the provider's responsibility;
- a callback must not retain pointers to caller-owned copied-I/O structures beyond the callback unless a separate provider contract explicitly owns the referenced storage;
- timeout-aware callbacks must treat the supplied timeout as the remaining public-operation budget, not as permission to restart a fresh independent wait indefinitely.

The portable DRIVER layer does not introduce hidden locks around provider callbacks. This keeps the callback boundary usable on bare-metal and RTOS targets and prevents lock-order policy from leaking into the public ABI.

## Zero-copy cross-thread ownership

A zero-copy `spw_buffer_t` is an ownership token, not a concurrently shared object.

An application-owned buffer may be handed from one application thread/task to another if the application provides a proper synchronization/happens-before handoff. After the handoff, only the receiving thread/task may access its view or perform an ownership transition until ownership changes again.

The following remain invalid:

- concurrent access to the same buffer/view from multiple threads/tasks;
- simultaneous ownership operations using the same buffer handle;
- use while the backend owns the buffer;
- use after the originating port is closed;
- use of a pre-reset handle after a successful `spw_port_reset()`.

A successful reset establishes a new runtime ownership epoch. Pre-reset application handles and pending completions are stale and must not be accepted for later submit/release/reclaim ownership transitions.

`spw_port_stop()` does not by itself define a new ownership epoch. Backend/provider-specific restrictions may still make an operation invalid while stopped, but stop is not specified as a blanket buffer-destruction operation.

## Timeout model

Timed operations use `spw_timeout_us_t`, measured in microseconds at the public API boundary.

### `SPW_TIMEOUT_IMMEDIATE`

`SPW_TIMEOUT_IMMEDIATE` (`0`) requests non-blocking behavior. The implementation may perform work that is immediately available, but it must not deliberately wait for a future event.

An immediate call can therefore return:

- `SPW_OK` if it can complete immediately;
- `SPW_ERR_TIMEOUT` when the requested receive/readiness/completion event is not ready;
- `SPW_ERR_RESOURCE_EXHAUSTED` when a bounded resource cannot immediately accept a new item;
- a terminal state/link/argument/backend error when that condition is already known.

### Finite timeout

A finite non-zero timeout is the maximum public wait budget for that call. Success or a terminal error may return earlier.

The timeout applies to the **whole public operation**, including internal retries, polling, fragmentation, acknowledgements and provider waits. An implementation must not restart the full caller timeout for each internal retry.

Operating-system, scheduler and hardware timer granularity may cause small measurement/rounding differences. The API does not promise hard-real-time deadline precision unless a platform/provider separately makes that guarantee.

On `SPW_ERR_TIMEOUT`, an operation must not silently report success, partially consume a logical receive item, or steal application ownership of a zero-copy handle.

### `SPW_TIMEOUT_INFINITE`

`SPW_TIMEOUT_INFINITE` requests an unbounded wait for an otherwise waitable condition. It does not suppress terminal errors: invalid local state, peer/link loss, unsupported functionality or backend failure may still return immediately or whenever detected.

Because same-handle operations are serialized by the application, an infinite wait must not be used as an implicit cancellation mechanism. The application must choose finite waits when another lifecycle action must eventually take control of that handle.

### Lifecycle/query operations without timeout arguments

Operations without a timeout parameter do not acquire a caller-defined deadline merely because other API calls use `spw_timeout_us_t`. A backend/provider may need bounded or unbounded internal coordination to complete lifecycle or query work. Applications must not infer `SPW_TIMEOUT_IMMEDIATE` semantics for such calls.

## Portable result-code meanings

`SPW_OK` is successful completion. Negative results have the following portable meanings.

| Result | Portable meaning |
|---|---|
| `SPW_ERR_INVALID_ARGUMENT` | A caller-supplied pointer, value, configuration field or argument combination is malformed independently of runtime object state. |
| `SPW_ERR_INVALID_STATE` | The object/handle is valid, but its **local** lifecycle or ownership state does not permit the requested operation. |
| `SPW_ERR_TIMEOUT` | A waitable condition did not become ready before the requested wait budget expired, including an immediate receive/readiness/completion probe with no available event. |
| `SPW_ERR_UNSUPPORTED` | The requested backend, version, build feature or optional operation is not implemented/supported. This is not a transient resource condition. |
| `SPW_ERR_RESOURCE_EXHAUSTED` | A bounded local/provider/service resource cannot currently represent or accept the requested work without waiting or increasing capacity. |
| `SPW_ERR_LINK_UNAVAILABLE` | The local endpoint has been started or is in an active connection/recovery state, but the peer/carrier/link needed for the operation is absent, lost or unusable. |
| `SPW_ERR_BUFFER_TOO_SMALL` | Supplied/requested storage capacity is insufficient. For copied RX, the complete required length and terminator are reported and the logical packet remains available for retry. |
| `SPW_ERR_INVALID_PACKET` | Packet metadata/shape is invalid, such as an invalid terminator, inconsistent length/capacity or a logical payload larger than the backend can represent. |
| `SPW_ERR_BACKEND` | The backend/provider/protocol failed in a way that cannot be represented by a more specific portable result. It is not ordinary flow control. |

A null payload pointer for a non-empty copied packet is an argument error when the supplied capacity would otherwise be sufficient. It is not a `BUFFER_TOO_SMALL` condition and must not cause a partial receive or crash.

Hardware/vendor diagnostic detail may remain available through provider-specific diagnostics, but the public operation must still map the primary control-flow outcome to the common result set.

## Link lifecycle and observable state

Opening a port constructs a valid endpoint object. The exact initial non-RUN state may be backend-specific; applications must not assume that successful construction means the link is already running.

Portable lifecycle rules are:

- successful `spw_port_reset()` leaves the local endpoint in `SPW_LINK_ERROR_RESET` and begins a new zero-copy ownership epoch;
- successful `spw_port_stop()` leaves the endpoint in a non-RUN state;
- `spw_port_start()` requests startup, but `SPW_OK` does not universally mean that an asynchronous/distributed link has already reached `SPW_LINK_RUN`;
- applications use `spw_port_get_link_state()` and operation results to observe establishment/recovery;
- a transfer attempted while the local endpoint has not been started, or after local stop/reset, reports `SPW_ERR_INVALID_STATE`;
- a transfer from a started/connecting/recovering local endpoint whose required peer/link is unavailable reports `SPW_ERR_LINK_UNAVAILABLE`.

Backends map only states they can meaningfully observe. They are not required to fabricate electrical/link-exchange transient states that do not exist at their abstraction layer.

## Resource model

SpWKit distinguishes API guarantees from values that depend on backend, build and configuration.

### Workspace

`spw_port_workspace_requirements()` is the authoritative source for caller-owned port storage size/alignment. Requirements are specific to the exact backend/configuration/build/version and may change between releases. Applications must query them instead of hard-coding private object sizes.

`spw_port_open_in_place()` is the deterministic no-heap construction path. `spw_port_open()` is a hosted convenience path and returns `SPW_ERR_UNSUPPORTED` when heap-backed construction is disabled.

### Capabilities

`spw_capabilities_t` describes the selected runtime/backend rather than a universal hardware profile.

- `max_packet_size` is the maximum complete logical payload the backend advertises; callers must not assume a larger portable value.
- non-zero `tx_queue_depth`/`rx_queue_depth` describe bounded backend/service capacity. They do **not** universally guarantee that exactly that many immediate sends succeed, because distributed/provider state may impose an earlier waitable/link condition.
- `buffer_alignment` states the backend-relevant alignment guarantee/requirement where applicable; callers must not infer DMA descriptors or physical address properties from it.
- optional capability bits become behavioral obligations when advertised.

### Zero-copy slots

For DRIVER, configured `tx_buffer_slots` and `rx_buffer_slots` bound SpWKit's wrapper bookkeeping. They do not claim to equal the native controller's descriptor/FIFO depth. Provider-native capacity remains private and may create `RESOURCE_EXHAUSTED` or timeout behavior within the common semantics.

### Allocation

No-heap/freestanding support does not mean every backend exists in every build. Build-time backend availability remains explicit. A source-visible backend/operation that is unavailable in the selected build reports `SPW_ERR_UNSUPPORTED` rather than silently allocating or substituting another transport.

## Verification boundary

The reusable backend contract suite verifies the application-visible rules that can be exercised deterministically across LOOPBACK, SIMULATOR, UDP, DEVICE and the reference DRIVER. Dedicated backend/provider tests remain responsible for lower-level transport, DMA, cache, protocol and platform details.

Threading rules that define **forbidden overlapping use** are primarily API preconditions and documentation obligations; tests do not deliberately create C data races merely to demonstrate undefined caller behavior. Executable tests instead verify state/error/timeout/resource and ownership-epoch behavior at the public boundary.

This contract remains software-level evidence. It does not imply physical SpaceWire controller, Data-Strobe/PHY, cable/electrical interoperability or formal ECSS certification.