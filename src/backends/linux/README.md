# Linux backend

Linux integration for virtual and physical SpaceWire devices.

Target device naming:

```text
/dev/vspw0    virtual SpaceWire
/dev/spw0     physical SpaceWire
```

The implementation may use a user-space service, CUSE, a kernel driver, or a vendor adapter as appropriate. Those mechanisms must not change the public SpaceWire-facing API.
