# STM32H755 DMA/cache integration evidence

This integration validates the SpWKit v0.6 public hardware-driver DMA/zero-copy contract on a real STM32H755 Cortex-M7 target.

It is deliberately **not** a SpaceWire PHY test. The firmware uses STM32 DMA2 memory-to-memory transfers to prove real MCU ownership transitions, DMA-visible memory placement, and Cortex-M7 D-cache clean/invalidate ordering through the same `spw_driver_ops_t` contract that a future physical SpaceWire controller driver uses.

## Target

Primary board:

- ST NUCLEO-H755ZI-Q;
- Cortex-M7 core;
- DMA2 Stream 0;
- D2 SRAM for DMA-owned buffers;
- AXI SRAM for ordinary program state/workspace.

The integration is standalone and does not require STM32Cube HAL. It uses only the CMSIS core/device headers plus SpWKit.

## Pinned STM32 dependency

For reproducible builds, CI uses:

- `STMicroelectronics/STM32CubeH7` tag `v1.12.0`;
- release commit `f5c0b7a2b1f6eb26fde150f72edb2d7deb647066`;
- the `Drivers/CMSIS/Device/ST/STM32H7xx` submodule revision pinned by that release.

Do not copy these vendor headers into SpWKit. Point the integration at the pinned checkout through `STM32_CUBE_H7_DIR`.

A minimal checkout can be prepared with:

```bash
git clone --filter=blob:none --no-checkout https://github.com/STMicroelectronics/STM32CubeH7.git /tmp/STM32CubeH7
git -C /tmp/STM32CubeH7 sparse-checkout init --cone
git -C /tmp/STM32CubeH7 sparse-checkout set Drivers/CMSIS/Include Drivers/CMSIS/Device/ST/STM32H7xx
git -C /tmp/STM32CubeH7 checkout f5c0b7a2b1f6eb26fde150f72edb2d7deb647066
git -C /tmp/STM32CubeH7 submodule update --init --depth 1 Drivers/CMSIS/Device/ST/STM32H7xx
```

## Build

Install an Arm GNU embedded toolchain providing `arm-none-eabi-gcc` and Newlib.

First cross-build/install SpWKit:

```bash
cmake -S . -B build/stm32-spwkit \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi-cortex-m7.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/stm32-install" \
  -DSPWKIT_ENABLE_HEAP=OFF \
  -DSPWKIT_BUILD_TESTS=OFF \
  -DSPWKIT_BUILD_CPP_TESTS=OFF \
  -DSPWKIT_BUILD_EXAMPLES=OFF \
  -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
  -DSPWKIT_BUILD_SIMULATOR=OFF \
  -DSPWKIT_BUILD_UDP=OFF \
  -DSPWKIT_BUILD_DEVICE=OFF
cmake --build build/stm32-spwkit
cmake --install build/stm32-spwkit
```

Then build the evidence firmware:

```bash
cmake -S integrations/stm32h755_dma -B build/stm32h755-dma \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
  -DCMAKE_PREFIX_PATH="$PWD/build/stm32-install" \
  -DSTM32_CUBE_H7_DIR=/tmp/STM32CubeH7
cmake --build build/stm32h755-dma
```

The output is:

```text
build/stm32h755-dma/spwkit_stm32h755_dma.elf
```

CI verifies that this ELF links and contains the public debugger evidence symbol. CI cannot claim the runtime result because GitHub-hosted runners have developed no ability to physically reach through the internet and press an ST-LINK reset button.

## What the firmware exercises

The firmware runs entirely from reset and exercises:

- `spw_port_open_in_place` with caller-owned workspace;
- start/stop/close lifecycle through the public driver backend;
- copied send/receive with EEP preservation;
- zero-copy TX acquire/submit/reclaim/release;
- zero-copy RX acquire/release;
- real DMA2 memory-to-memory packet movement;
- D-cache clean before device ownership;
- D-cache invalidate before CPU ownership;
- DMA-visible buffers in D2 SRAM;
- statistics and ownership/completion accounting.

The DMA test uses the public SpWKit API. It does not bypass the driver backend to make the evidence easier.

## Runtime evidence

The firmware leaves a stable symbol in memory for debugger inspection:

```c
volatile stm32_evidence_t g_stm32h755_spwkit_evidence;
```

Successful completion is:

```text
magic            = 0x53505736   # "SPW6"
phase            = 0x0000600d
result           = 0x00000000
sync_to_device   > 0
sync_from_device > 0
dma_transfers    >= 2
tx_packets       >= 2
rx_packets       >= 2
```

Failure uses:

```text
result >= 0x100
phase = 0xdead0000 | result
```

The result value identifies the failing contract phase in `firmware.c`.

## Flash and inspect

Use an ST-LINK/OpenOCD/GDB setup appropriate to the NUCLEO-H755ZI-Q and target the Cortex-M7 image at the linker-defined flash address (`0x08000000`).

A typical debugger flow is conceptually:

```text
connect to CM7
load spwkit_stm32h755_dma.elf
reset/run
halt after the firmware reaches its terminal loop
inspect g_stm32h755_spwkit_evidence
```

The exact OpenOCD invocation depends on the installed OpenOCD/ST-LINK version and board configuration, so the acceptance record should include the command/tool versions actually used rather than pretending one magic command is universal.

## Acceptance record for #119

When run on the board, record in issue #119:

- NUCLEO-H755ZI-Q board/revision if known;
- ST-LINK/OpenOCD/GDB versions;
- SpWKit commit SHA;
- compiler version;
- the complete `g_stm32h755_spwkit_evidence` values;
- pass/fail and any observed errata.

A successful run closes the MCU DMA/cache evidence requirement. It must **not** be described as SpaceWire electrical or physical-link HIL; that remains a separate future evidence layer described in `docs/hardware-acceptance.md`.
