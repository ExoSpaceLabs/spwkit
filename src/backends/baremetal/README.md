# Bare-metal backend

Portable embedded integration without an operating-system dependency.

Design targets:

- static/user-provided memory;
- polling and interrupt-driven operation;
- MMIO control paths;
- DMA packet transfer;
- optional virtual Ethernet transport;
- no mandatory heap, filesystem, POSIX, exceptions, or RTTI.

Platform-specific register and DMA implementations should sit below the portable backend contract.
