# HardRT backend

HardRT integration for deterministic SpaceWire application and backend execution.

Expected integration points include:

- ISR-safe notification;
- bounded queues;
- semaphores/events;
- timing services;
- deterministic task execution;
- virtual Ethernet and physical FPGA backends.

HardRT support should remain an adapter around the portable core rather than introduce HardRT dependencies into generic SpWKit APIs.
