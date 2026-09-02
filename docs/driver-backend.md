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
    OPS --> FPGA[Future FPGA / DMA driver]
```

## Public configuration

`<spwkit/driver.h>` defines the versioned driver configuration and callback contract. The callback table and driver context remain caller-owned for the lifetime of the SpWKit port.

The application-facing `spw_port_t` remains opaque. Driver callback/context pointers are backend-specific configuration, not generic packet/link types.

## Core callbacks

A driver maps controller behavior into the common operations, including lifecycle, observable link state, capabilities, copied DATA and optional time-code/statistics/readiness operations.

SpWKit validates required callbacks against the capabilities the driver advertises. A driver cannot claim a public optional capability and omit the callbacks needed to implement it.

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

### Cache synchronization

Optional driver cache callbacks can prepare a buffer for device access or CPU readback. Their implementation is platform-specific and may be a no-op on coherent systems.

The public contract intentionally says **when ownership changes**, not which cache-maintenance instruction or MPU/cache policy a platform must use.

## Error/result mapping

The driver maps native completion/errors into `spw_result_t`. Hardware-specific diagnostic detail may remain in the driver/vendor layer, but common application control flow must remain possible through portable result values and statistics.

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

## STM32H755 evidence boundary

The STM32H755 task is intended to validate the public driver/DMA boundary on real Cortex-M7 silicon, including actual memory-to-memory DMA and explicit cache/coherency handling. It is **not** yet counted as runtime evidence until the board/test architecture is agreed and executed.

The STM32 test is not a SpaceWire PHY test. It proves driver ownership/cache behavior relevant to a future controller integration.

## FPGA/public stop line

This public repository documents only the software obligations of a future hardware driver. It does not publish or guess:

- proprietary RTL architecture;
- register/address maps;
- DMA descriptor layouts;
- internal bus topology;
- clock/reset/interrupt structure;
- specific IP-core selection.

A future FPGA/SpaceWire implementation can satisfy the same public driver contract while keeping its hardware design independent.
