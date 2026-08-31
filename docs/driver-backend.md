# Portable hardware driver backend

`SPW_BACKEND_DRIVER` is the v0.6 software boundary between the public
SpWKit port API and a platform/vendor SpaceWire driver.

The application continues to use `spw_port_start()`, packet I/O, EOP/EEP,
time codes, readiness, statistics and the normal timeout/result model. The
backend delegates those operations through `spw_driver_ops_t` to a caller-
owned driver context.

## Ownership

SpWKit does not create or destroy the vendor driver object. The
`spw_driver_ops_t` table and `driver_context` referenced by
`spw_driver_config_t` must remain valid until `spw_port_close()` returns.
This allows the context to represent a Linux kernel/user driver object, a
bare-metal register block, an RTOS device object, or a future FPGA/DMA
adapter without exposing any such native type in the SpWKit ABI.

## Required callbacks

A v1 driver provides lifecycle (`start`, `stop`, `reset`), link state,
capability discovery and copied packet `send`/`receive`. Time-code,
statistics and readiness callbacks are optional; if the corresponding
capability is advertised, both sides of that optional contract must be
implemented.

Readiness remains level-triggered and non-consuming. A driver with no
`wait` callback does not expose `SPW_CAP_READINESS`, so polling drivers are
valid without pretending to have an interrupt/event facility.

## DMA boundary

v1 copied I/O intentionally does not expose DMA descriptors, physical
addresses, AXI types or cache operations. DMA-capable buffer ownership is
layered onto the existing opaque `spw_buffer_t` API in #111. That later
mapping will preserve application-facing ownership while keeping hardware
descriptors private to the vendor driver.

## FPGA stop line

This backend is not an HDL definition. Register maps, DMA descriptor
layout, interrupt routing, clock/reset domains and actual SpaceWire RTL are
explicitly deferred to the future FPGA interface specification (#113).
