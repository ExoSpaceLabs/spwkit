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
- stack storage when the backend size and stack budget make that appropriate;
- externally managed shared/coherent memory in a future platform adapter.

SpWKit does not require any particular allocator for this path.

## Hosted convenience allocation

`spw_port_open()` remains available as a convenience for hosted applications. With the default build it allocates a correctly sized/aligned workspace and then delegates construction to the same in-place implementation.

This keeps one initialization path:

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

## Packet memory

Copied packet I/O already uses caller-owned payload memory:

- TX: `spw_packet_t.data` points to caller-owned bytes for the duration of `spw_port_send()`;
- RX: the caller supplies writable `data` and `capacity` to `spw_port_receive()`;
- no backend may silently truncate a packet;
- insufficient RX capacity returns `SPW_ERR_BUFFER_TOO_SMALL` and reports the required packet length.

The current loopback backend stores pending packets in fixed-capacity internal arrays. Exhaustion is explicit and deterministic through `SPW_ERR_RESOURCE_EXHAUSTED`/timeout semantics rather than hidden allocation.

The portable zero-copy ownership API is tracked separately by issue #10. That API will build on this memory model: applications/backends exchange ownership of bounded buffers without exposing DMA addresses or requiring heap allocation.

## v0.1 no-heap guarantee

The v0.1 allocation-free guarantee currently applies to the portable core plus the loopback/reference core path when built with:

```text
SPWKIT_ENABLE_HEAP=OFF
SPWKIT_BUILD_SIMULATOR=OFF
```

The process-local simulator is a hosted verification backend. It uses host synchronization facilities and is not the bare-metal portability reference, even though its `SimulatorBackend` object can also be constructed in caller-owned storage.

Future bare-metal, HardRT, FreeRTOS, RTEMS, Linux-device, and hardware adapters must document their own additional workspace/resource requirements and must not alter the common ownership semantics.

## CI verification

The `Linux no-heap core` GitHub Actions job:

1. configures with `SPWKIT_ENABLE_HEAP=OFF` and simulator disabled;
2. compiles the C++ core with exceptions and RTTI disabled;
3. runs the `noheap` CTest profile;
4. replaces global C++ allocation functions in the test executable with counters;
5. opens, starts, transfers packets/time-codes, queries statistics, closes, and reuses the same workspace;
6. fails if any dynamic allocation occurs while those mandatory operations execute.

This is a behavioral portability baseline, not a claim that every future optional backend is allocation-free.
