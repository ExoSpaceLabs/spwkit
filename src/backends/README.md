# Backends

Backends translate the portable SpWKit API into a concrete implementation.

Planned layout:

```text
src/backends/
├── virtual/       in-process/local virtual link
├── ethernet/      distributed virtual SpaceWire transport
├── linux/         Linux device and vendor-device adapters
├── baremetal/     portable embedded hardware adapter layer
└── hardrt/        HardRT integration
```

Backend-specific concepts must remain below the public API boundary. UDP sockets, MAC addresses, file descriptors, AXI register maps, DMA descriptors, and RTOS primitives are implementation details rather than SpaceWire API concepts.
