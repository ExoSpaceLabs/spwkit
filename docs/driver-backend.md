# Portable hardware-driver backend

`SPW_BACKEND_DRIVER` is the v0.6 software boundary between the ordinary SpWKit application API and a platform/vendor hardware driver.

The application still uses `spw_port_*` and `spw_buffer_*`; native controller details stay below the driver callback table.

```mermaid
flowchart TB
    APP[Application] --> API[spw_port_* / spw_buffer_*]
    API --> DB[SPW_BACKEND_DRIVER]
    DB --> OPS[spw_driver_ops_t + caller context]
    OPS --> REF[Deterministic host reference driver]
    OPS --> MCU[MCU / RTOS driver]
    OPS --> VENDOR[Vendor SDK adapter]
    OPS --> FPGA[FPGA / DMA driver]
    OPS --> PCIE[Discrete host adapter]
```

## Public configuration

`<spwkit/driver.h>` defines the versioned driver configuration and callback contract. The callback table and driver context remain caller-owned for the lifetime of the SpWKit port.

The application-facing `spw_port_t` remains opaque. Driver callback/context pointers are backend-specific configuration, not generic packet/link types.

## Core callbacks

A driver maps controller behavior into the common operations, including lifecycle, observable link state, capabilities, copied DATA and optional time-code/statistics/readiness operations.

SpWKit validates required callbacks against the capabilities the driver advertises. A driver cannot claim a public optional capability and omit the callbacks needed to implement it.

## Provider model

`spw_driver_ops_t` is a semantic boundary, not a prescribed physical datapath.

A conforming provider may be implemented over:

- direct MMIO/PIO hardware;
- a streaming FPGA datapath with system/vendor DMA;
- an FPGA with integrated descriptor DMA;
- a vendor board that already exposes a SpaceWire SDK or kernel/device API;
- a bare-metal or RTOS controller driver;
- a discrete PCIe or similar host adapter;
- another implementation that can preserve the documented SpWKit packet/link semantics.

SpWKit does not require these providers to share a register map, bus, descriptor format, interrupt model, DMA engine, or internal software layering.

Applications therefore depend on the public SpaceWire contract rather than the controller implementation.

## Threading and callback reentrancy

The canonical rules are defined in [`runtime-contract.md`](runtime-contract.md). For DRIVER providers, the important obligations are explicit:

- SpWKit invokes a provider callback synchronously from the public operation that caused it;
- the portable application contract serializes overlapping calls on one `spw_port_t`, so a provider is not required to make one port context internally reentrant;
- a callback must not call back into the same SpWKit port;
- different SpWKit port handles may be operated concurrently;
- when multiple DRIVER ports share one native controller, DMA engine, interrupt source, SDK object or provider context, synchronization of that shared native resource belongs to the provider;
- SpWKit does not place a hidden global/provider mutex around callbacks;
- timeout-aware callbacks receive the remaining budget of the public operation and must not restart a fresh full timeout indefinitely;
- `stop`, `reset` and `close` are not concurrent cancellation primitives and must not race another public call on the same handle.

This deliberately avoids imposing POSIX locking on bare-metal/RTOS providers while still defining what portable applications may assume.

## Efficiency model

Portability must not require a lowest-common-denominator datapath.

Provider selection and capability validation happen when the driver-backed port is configured/opened. A provider is free to bind its efficient implementation once and use direct native operations on the hot path.

The public ABI does **not** require another per-packet runtime dispatch layer beneath `spw_driver_ops_t`.

### DMA-capable providers

When the provider can support stable buffer ownership, the DMA/zero-copy callback set is the intended fast path. An advertised zero-copy path must not hide payload copies behind the ownership API.

### Copied operations

Copied `send` and `receive` remain mandatory so all hardware can satisfy the common backend contract.

A DMA-capable provider may implement copied operations on top of the same native buffer/queue engine, requiring only the unavoidable copy between caller storage and the provider-owned buffer rather than maintaining a separate packet transport.

A PIO-only provider may implement copied I/O directly and simply omit the DMA/zero-copy capability.

### Vendor hardware

A vendor adapter should map to the most efficient native mechanism that the platform exposes. It does not need to emulate an unrelated FPGA hardware interface merely to use SpWKit.

If the vendor API exposes only copied packet transfer, copied operations are sufficient. If it exposes stable DMA/zero-copy ownership, the optional ownership callbacks can expose that capability through the same public API.

## DMA / zero-copy mapping

Driver ABI v2 can provide an all-or-nothing DMA ownership callback set.

```mermaid
flowchart LR
    ACQ[Public acquire TX] --> D_ACQ[driver acquire TX token]
    D_ACQ --> APP[CPU-visible view]
    APP --> SUB[Public submit]
    SUB --> D_SUB[driver submit]
    D_SUB --> DMA[DMA/controller owns]
    DMA --> D_REC[driver reclaim completion]
    D_REC --> REC[Public reclaim]
```

The driver returns an opaque token plus CPU-visible pointer/capacity/alignment. SpWKit wraps that information in bounded opaque `spw_buffer_t` slots held inside the port workspace.

Native physical addresses, descriptor layouts and vendor handles never become public application fields.

A successful `spw_port_reset()` begins a new public ownership epoch. Any application-visible zero-copy handle or pending completion from before the reset is stale afterward. Providers must reset or otherwise invalidate their corresponding native ownership state so fresh acquisitions can recover the advertised capacity.

### Cache synchronization

Optional driver cache callbacks can prepare a buffer for device access or CPU readback. Their implementation is platform-specific and may be a no-op on coherent systems.

The public contract intentionally says **when ownership changes**, not which cache-maintenance instruction or MPU/cache policy a platform must use.

## Receive atomicity

All provider types must preserve the common receive contract.

If the next complete packet is larger than the caller's copied receive capacity, the provider must return `SPW_ERR_BUFFER_TOO_SMALL`, report the required complete length and terminator, and leave that packet available for retry.

This can be implemented differently underneath the ABI:

- retain a complete packet in a FIFO/packet RAM;
- leave a completed DMA descriptor/buffer queued;
- retain a vendor-native packet object in adapter state;
- expose the complete packet directly through zero-copy acquisition.

The mechanism is private; the observable behavior is not.

## Error/result mapping

The driver maps native completion/errors into `spw_result_t`. Hardware-specific diagnostic detail may remain in the driver/vendor layer, but common application control flow must remain possible through portable result values and statistics.

In particular, providers must preserve the portable distinction between local lifecycle misuse (`SPW_ERR_INVALID_STATE`), an active endpoint whose required link/peer is unavailable (`SPW_ERR_LINK_UNAVAILABLE`), exhausted bounded capacity (`SPW_ERR_RESOURCE_EXHAUSTED`), and expiration of a wait budget (`SPW_ERR_TIMEOUT`). Provider-specific error values must not leak through the public API.

## Reference-driver evidence

`tests/reference_driver` provides a deterministic host-side driver implementation that exercises the public driver backend and callback contract without claiming physical hardware.

The main v0.6 CI also covers:

- copied driver lifecycle/I/O;
- capability validation;
- zero-copy/DMA ownership transitions;
- cache-hook ordering;
- stale/foreign handle rejection;
- bounded wrapper resources;
- no-heap/freestanding compatibility.

The v0.7 contract-hardening work additionally runs the deterministic DRIVER through the same executable result, timeout, lifecycle, resource and reset-ownership rules used by the reusable backend contract.

## STM32H755 evidence boundary

The physical NUCLEO-H755ZI-Q qualification has completed successfully. It validates the public driver/DMA boundary on real Cortex-M7 silicon using DMA2 memory-to-memory transfers, DMA-visible D2 SRAM, explicit D-cache clean/invalidate synchronization, copied and zero-copy paths, and reset-time stale-buffer invalidation.

The accepted phase-7 record reports `magic=0x53505736`, `phase=0x0000700d`, `result=0`, two DMA transfers, two TX packets, two RX packets, synchronization in both directions, and `reset_stale_invalidated=1` (`RESULT: PASS`).

This is physical MCU driver/DMA/cache evidence. It is **not** SpaceWire controller, codec, Data-Strobe, PHY, cable, or electrical interoperability evidence.

## Profiling evidence

Post-v0.6 profiling has also characterized the driver boundary on both controlled x86 and the NUCLEO-H755ZI-Q. The copied DRIVER abstraction is thin relative to provider/copy costs, while zero-copy crossover depends on payload size and the ownership lifecycle being measured. See [`profiling-results.md`](profiling-results.md) for the accepted reference data and its interpretation limits.

## FPGA/public stop line

This public repository documents only the software obligations of a hardware driver. It does not publish or guess:

- proprietary RTL architecture;
- register/address maps;
- DMA descriptor layouts;
- internal bus topology;
- clock/reset/interrupt structure;
- specific IP-core selection.

A private, vendor, open-source, or otherwise independent FPGA/SpaceWire implementation can satisfy the same public driver contract while keeping its hardware design independent.
