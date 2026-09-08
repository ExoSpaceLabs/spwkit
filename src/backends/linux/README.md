# Linux integration

Linux has two distinct SpWKit integration layers:

1. the shipped virtual-device stack (`SPW_BACKEND_DEVICE` -> VSPD -> `vspwd`);
2. future physical/vendor device drivers below the portable `SPW_BACKEND_DRIVER` boundary.

## Virtual SpaceWire

v0.4 introduced the Linux DEVICE/VSPD service path and v0.5 added the production CUSE presentation.

```mermaid
flowchart TB
    APP[Application using SpWKit API] --> DEV[SPW_BACKEND_DEVICE]
    DEV --> D[vspwd]
    RAW[Application using character device] --> NODE[/dev/vspw0]
    NODE --> CUSE[spwcuse]
    CUSE --> DEV2[SPW_BACKEND_DEVICE]
    DEV2 --> D
```

`/dev/vspwX` is therefore a real optional v0.5 virtual-device presentation, not merely a target naming convention.

## Physical SpaceWire

A future Linux hardware integration may use a kernel driver, userspace vendor SDK, UIO/VFIO-like mechanism or another platform-specific approach below `SPW_BACKEND_DRIVER`.

A physical device may eventually be presented with a platform-specific name such as `/dev/spw0`, but no such device node is part of the current released runtime contract.

The mechanism must not change the public SpaceWire-facing API or expose native descriptor/register types through common application operations.
