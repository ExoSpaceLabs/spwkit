# HardRT integration notes

HardRT is an external RTOS/scheduler integration for deterministic SpaceWire application/backend execution. It is **not** a dependency of the generic SpWKit runtime or public ABI.

The validated baseline is HardRT release `0.4.0` at commit `1b861393cd7967ce5d0b3ac6f45928828a2d63aa`.

Current repository evidence includes:

- executed installed-package HardRT POSIX + SpWKit integration;
- Cortex-M7/ARMv7E-M no-heap compile/link integration with the HardRT `cortex_m` port;
- separation of HardRT task/scheduler types from the SpWKit public API.

```mermaid
flowchart LR
    TASK[HardRT task] --> API[SpWKit public API]
    API --> PORT[task-owned port]
    HRT[HardRT 0.4.0] -. scheduling / timing .-> TASK
```

Future physical driver integration may use HardRT events/semaphores/ISR notification below `SPW_BACKEND_DRIVER`, but those mechanisms must remain driver/platform details.

The Cortex-M7 integration is compile/link evidence; STM32H755 runtime DMA/cache evidence is tracked separately under v0.6 #119.
