# HardRT Cortex-M7 integration

This fixture is the first v0.5 bare-metal integration contract for SpWKit. It deliberately targets a precise architecture profile rather than claiming generic Cortex-M compatibility.

## Target profile

```text
target triple : arm-none-eabi
CPU           : Cortex-M7 / ARMv7E-M
ISA           : Thumb
float ABI     : soft
SpWKit heap   : disabled
hosted backends: disabled
```

The profile matches the architectural class used by STM32H7 devices, including the STM32H755, but the hosted CI job is **not** STM32H7 HIL or electrical SpaceWire validation.

## Dependency model

HardRT remains an external integration dependency. CI pins HardRT commit:

```text
1b861393cd7967ce5d0b3ac6f45928828a2d63aa
```

Both HardRT and SpWKit are cross-built and installed/staged separately, then this firmware consumes their exported CMake packages. No HardRT header/type becomes part of the SpWKit public ABI.

## Firmware evidence

The integration firmware links:

- the HardRT `cortex_m` scheduler/port, including PendSV assembly and SysTick support;
- a no-heap SpWKit loopback port using caller-owned aligned workspace;
- EEP DATA, a time code and statistics through the public SpWKit API.

The fixture provides a small Cortex-M7 linker contract with explicit FLASH/RAM regions, `__RAM_START__`, `__RAM_END__`, `__StackTop` and `SystemCoreClock` symbols required by the current HardRT Cortex-M port.

CI verifies the final ELF and map file and rejects hosted socket/thread/C++ ABI leakage.

## Evidence boundary

This is **compile/link/ABI evidence**. It does not claim that SysTick/PendSV scheduling was executed on a faithful STM32H7 model. Runtime validation should be added when either:

- a faithful Cortex-M7 target/emulator is selected and its limitations are documented; or
- physical STM32H7 HIL is available.

Physical SpaceWire electrical interoperability remains a separate hardware-backend milestone.
