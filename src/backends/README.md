# Backends

Backends translate the portable SpWKit API into a concrete implementation.

Current layout:

```text
src/backends/
├── loopback/      deterministic in-process reference backend
├── virtual/       implemented process-local two-peer simulator
├── ethernet/      implemented VSPW-TP codec + POSIX UDP backend
├── linux/         planned Linux device and vendor-device adapters
├── baremetal/     planned portable embedded hardware adapter layer
└── hardrt/        planned HardRT integration
```

Backend-specific concepts must remain below the common API boundary. Native UDP socket handles, MAC addresses, file descriptors, AXI register maps, DMA descriptors, and RTOS primitives are implementation details rather than SpaceWire API concepts.

Backend-specific public configuration may expose portable descriptive values required to select/configure an implementation, such as numeric IP address strings, UDP ports, `link_id`, or sizing limits, while platform-native handles remain internal.
