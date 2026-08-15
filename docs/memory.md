# Memory model and no-heap operation

SpWKit separates the portable port contract from the mechanism used to store the port/backend objects.

The mandatory portable path does **not** require dynamic allocation.

## Caller-owned workspace

A port can be constructed entirely inside storage supplied by the application:

```c
spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

spw_port_workspace_requirements_t req;
spw_port_workspace_requirements(&config, &req);

/* Example only: real embedded code may use a static pool/linker section. */
static unsigned char workspace[64 * 1024];

spw_port_t *port = NULL;
spw_port_open_in_place(&config, workspace, sizeof(workspace), &port);
```

The actual caller storage must satisfy both values returned by the requirements query:

- `size`: minimum number of bytes;
- `alignment`: required base-address alignment.

The application owns that storage for the complete lifetime of the port. `spw_port_close()` destroys the objects constructed in it but does not free or retain the workspace. The same memory can be reused immediately after close.

## Why requirements are queried

Backend object size is intentionally not part of the public ABI.

A fixed public structure such as `spw_port_storage_t bytes[NNN]` would couple the ABI to the largest current backend and would either waste embedded memory or require an ABI break when a later backend became larger.

Instead:

```text
configuration
     |
     v
spw_port_workspace_requirements()
     |
     +--> required size
     +--> required alignment
     |
caller-owned region
     |
     v
spw_port_open_in_place()
     |
     +--> opaque spw_port_t
     +--> selected backend object
```

The workspace may therefore come from:

- static/global storage;
- a board-specific fixed memory pool;
- an RTOS memory region;
- a linker-defined section;
- stack storage when backend size and stack budget make that appropriate;
- externally managed shared/coherent memory in a future platform adapter.

SpWKit does not require any particular allocator for this path.

## Hosted convenience allocation

`spw_port_open()` remains available as a convenience for hosted applications. With the default build it allocates a correctly sized/aligned workspace and then delegates construction to the same in-place implementation.

```text
spw_port_open()
     |
 allocate workspace
     |
     v
spw_port_open_in_place()
```

When configured with:

```text
-DSPWKIT_ENABLE_HEAP=OFF
```

`spw_port_open()` remains present for ABI consistency but returns `SPW_ERR_UNSUPPORTED`. Portable code should use the in-place API when it must not depend on a heap.

## Copied packet memory

Copied packet I/O uses caller-owned payload memory:

- TX: `spw_packet_t.data` points to caller-owned bytes for the duration of `spw_port_send()`;
- RX: the caller supplies writable `data` and `capacity` to `spw_port_receive()`;
- no backend may silently truncate a packet;
- insufficient RX capacity returns `SPW_ERR_BUFFER_TOO_SMALL`, reports the required packet length/terminator, and retains the complete packet for retry.

The loopback and process-local simulator use bounded internal storage. Resource exhaustion is explicit rather than hidden behind unbounded allocation.

The current POSIX UDP backend also uses bounded reassembly storage and advertises a 1 MiB logical packet limit. VSPW-TP fragments are internal transport objects and never become application-owned packet buffers.

## Zero-copy ownership

The portable zero-copy ownership API is implemented and capability-gated by `SPW_CAP_ZERO_COPY`.

```text
TX: acquire -> fill -> submit -> backend owns -> reclaim -> reuse/release
RX: backend receives -> acquire -> inspect -> release
```

The API intentionally models ownership rather than DMA representation. Public handles/views do not expose physical addresses, DMA descriptors, AXI objects, file descriptors or vendor handles.

The v0.1 local simulator implements zero-copy ownership using fixed aligned host-memory buffers. It may copy internally while preserving application-visible ownership and completion semantics.

A future DMA backend can map the same lifecycle onto coherent/pinned buffers and descriptor rings without changing application source.

Scatter/gather is deferred beyond v0.1; a current zero-copy buffer represents one contiguous packet payload.

## v0.1 no-heap guarantee

The v0.1 allocation-free guarantee applies to the portable core plus the loopback/reference path when built with:

```text
SPWKIT_ENABLE_HEAP=OFF
SPWKIT_BUILD_SIMULATOR=OFF
```

The process-local simulator and POSIX UDP backend are hosted verification/runtime backends. They may rely on hosted synchronization/network facilities and are not the bare-metal portability reference, even though backend objects are constructed through the same workspace mechanism where supported.

Future bare-metal, HardRT, FreeRTOS, RTEMS, Linux-device and hardware adapters must document their own workspace/resource requirements without altering common ownership semantics.

## CI verification

The `Linux no-heap core` GitHub Actions job:

1. configures with `SPWKIT_ENABLE_HEAP=OFF` and simulator disabled;
2. compiles the C++ core with exceptions and RTTI disabled;
3. runs the `noheap` CTest profile;
4. replaces global C++ allocation functions in the test executable with counters;
5. opens, starts, transfers packets/time-codes, queries statistics, closes, and reuses the same workspace;
6. fails if any dynamic allocation occurs while those mandatory operations execute.

This is a behavioral portability baseline, not a claim that every optional hosted or future hardware backend is allocation-free internally.
