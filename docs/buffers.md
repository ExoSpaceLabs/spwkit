# Zero-copy buffer ownership

SpWKit provides an optional ownership-oriented packet path for backends that can avoid application-side packet copies or naturally expose reusable transfer buffers.

The copied `spw_port_send()` / `spw_port_receive()` path remains mandatory for every v0.1 backend. Zero-copy is advertised only through `SPW_CAP_ZERO_COPY`.

## Design boundary

The public API describes **who owns a buffer and when it may be accessed**. It deliberately does not describe how a backend implements the buffer.

```text
Application-visible contract

TX: acquire -> fill -> submit -> backend owns -> reclaim -> reuse/release
RX: backend receives -> acquire -> inspect -> release
```

A simulator may implement those buffers with ordinary process memory. A Linux/FPGA backend may map them to coherent memory, pinned pages, descriptor rings, vendor SDK buffers, or another DMA-capable resource.

None of the following are part of the portable ABI:

- physical addresses;
- `dma_addr_t`;
- AXI addresses or descriptors;
- Linux file descriptors or ioctls;
- vendor DMA handles;
- FPGA-specific descriptor layouts.

## Public objects

`spw_buffer_t` is opaque. Applications inspect an application-owned buffer using `spw_buffer_get_view()`.

`spw_buffer_view_t` contains:

- `data`: application-visible byte pointer;
- `length`: current packet payload length;
- `capacity`: writable/storage capacity;
- `terminator`: EOP or EEP.

The view is valid only while the application owns the associated `spw_buffer_t`.

For TX, `data` is writable. For RX, packet bytes are read-only by contract until the buffer is released. The C ABI uses one pointer type for the common view; applications must not modify RX data.

## TX lifecycle

```text
spw_port_acquire_tx_buffer()
        |
        | application owns buffer
        v
spw_buffer_get_view()
        |
        | fill view.data
        v
spw_buffer_set_packet(length, terminator)
        |
        v
spw_port_submit_tx_buffer()
        |
        | success sets application pointer to NULL
        | backend owns buffer
        v
transmit/completion
        |
        v
spw_port_reclaim_tx_buffer()
        |
        | application owns buffer again
        +----------> refill + submit again
        |
        v
spw_port_release_tx_buffer()
        |
        | success sets pointer to NULL
        v
backend pool
```

A failed submit does not transfer ownership and leaves the application pointer unchanged.

Reclaim returns a completed buffer into application ownership. It may then be reused directly or returned to the backend pool with `spw_port_release_tx_buffer()`.

## RX lifecycle

```text
packet becomes available
        |
        v
spw_port_acquire_rx_buffer()
        |
        | application owns buffer/view
        v
inspect packet bytes + EOP/EEP
        |
        v
spw_port_release_rx_buffer()
        |
        | success sets pointer to NULL
        v
backend owns/recycles buffer
```

Only one application may own a particular buffer at a time. A buffer acquired from one port cannot be submitted or released through another port.

## Capacity and alignment

Applications must query `spw_port_get_capabilities()`.

- `max_packet_size` is the maximum transferable packet size.
- `buffer_alignment` is the alignment guaranteed/required by the backend's zero-copy buffers.
- queue-depth fields describe bounded backend resources and may be used for deterministic pool sizing.

`spw_port_acquire_tx_buffer()` accepts a minimum required capacity. A request larger than the backend can satisfy fails explicitly.

## Exhaustion and timeouts

Zero-copy pools are bounded resources. Exhaustion is not hidden by allocating more memory.

Immediate acquisition on an exhausted pool returns `SPW_ERR_RESOURCE_EXHAUSTED`. Finite waits may return `SPW_ERR_TIMEOUT`. Backends may wake blocked operations when buffers are reclaimed/released.

This mirrors the deterministic resource model established for the portable core.

## Simulator implementation

The v0.1 local simulator advertises `SPW_CAP_ZERO_COPY` and implements the same public ownership contract using fixed host-memory buffers.

The simulator is allowed to copy internally between its ownership buffers and its packet engine. The purpose of the simulator implementation is to reproduce the **application-visible ownership, ordering, capacity, alignment, timeout, and completion semantics** before physical DMA hardware is available.

That distinction is intentional:

```text
Application
    |
    | same zero-copy API
    v
libspwkit
    |
    +--> simulator: fixed host-memory buffers, internal emulation
    |
    +--> future FPGA backend: DMA-capable buffers/descriptors internally
```

The application does not change when the backend changes.

## Scatter/gather policy for v0.1

Scatter/gather packet buffers are **deferred** for v0.1.

A v0.1 zero-copy buffer represents one contiguous SpaceWire packet payload. This keeps packet boundaries and ownership rules unambiguous while the first DMA-capable physical backend does not yet exist.

A future scatter/gather extension should be capability-gated and add explicit segment ownership rather than reinterpret the current contiguous-buffer ABI.

## Interaction with copied I/O

Advertising zero-copy does not remove the copied API. Applications may mix copied and ownership-oriented operations on the same port subject to normal queue/resource behavior.

The shared contract suite verifies that copied send/receive still works after zero-copy transfers.

## Future DMA backend mapping

A physical backend can implement the current API without changing application-visible semantics:

```text
acquire TX
    -> reserve DMA-capable buffer / descriptor
submit TX
    -> publish descriptor to DMA engine
reclaim TX
    -> return completed descriptor buffer
release TX
    -> return unused/reclaimed buffer to pool

acquire RX
    -> expose completed RX DMA buffer
release RX
    -> recycle descriptor/buffer to RX ring
```

Cache maintenance, address translation, descriptor chaining, interrupts, IOMMU handling, and device synchronization remain backend responsibilities.
