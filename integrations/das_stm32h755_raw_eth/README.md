# STM32H755 raw Ethernet over DAS

This integration binds SpWKit's transport-independent raw-Ethernet backend to
the Layer-2 Ethernet API provided by DAS on the NUCLEO-H755ZI-Q.

The dependency direction remains:

```text
application
  -> SpWKit public API
     -> VSPW-TP engine
        -> spw_raw_ethernet_io_ops_t
           -> integration adapter
              -> DAS Ethernet
                 -> STM32H755 ETH MAC/DMA
                    -> RMII / LAN8742A / CN14
```

SpWKit has no direct dependency on DAS. The adapter is integration code only.

## Reproducible baseline

- DAS commit: `4b768ef86b43652c94cc91b1c77e247fa37ebd8a`
- STM32CubeH7 commit: `f5c0b7a2b1f6eb26fde150f72edb2d7deb647066`
- board: NUCLEO-H755ZI-Q, CM7
- board MAC: `02:00:00:00:00:01`
- EtherType: `0x88B5`
- VSPW link ID: `0x44534153`
- logical packet capacity: 4096 bytes
- VSPW carrier storage capacity: 1500 bytes
- fragment payload: 1400 bytes

The compact SpWKit profile requires about 21 KiB of port workspace on the
current implementation and is guarded by a 32 KiB CI ceiling.

## Hardware run

Requirements:

- NUCLEO-H755ZI-Q connected over ST-LINK;
- Ethernet cable on CN14 to a Linux Ethernet interface;
- JP6/JP7 fitted for the board Ethernet route;
- pinned DAS and STM32CubeH7 checkouts;
- OpenOCD, Arm GNU toolchain, CMake, and gdb-multiarch or arm-none-eabi-gdb;
- sudo permission for AF_PACKET raw sockets and bringing the Linux interface up.

Run:

```sh
bash scripts/stm32h755_das_raw_eth_test.sh \
  --interface enp0s31f6
```

By default the runner prepares its pinned public dependencies automatically
under `thirdparty/`:

- `thirdparty/device-abstraction-stack`;
- `thirdparty/STM32CubeH7`.

Missing checkouts are cloned automatically. Existing clean checkouts are
fetched/checked out to the exact pinned revisions before the build, so stale
dependencies cannot silently contaminate HIL evidence. The optional
`--das-root` and `--stm32h7-root` arguments remain available for custom
checkout locations; local modifications are never discarded automatically.

The script performs a clean build of compact Cortex-M7 SpWKit, pinned DAS,
board firmware, native compact SpWKit and the Linux AF_PACKET peer. It flashes
the CM7 image, establishes VSPW RUN over physical Ethernet, exercises 64, 256,
1024 and 4096 byte echo traffic, records host RTT distributions, captures
STM32 DWT cycle evidence, validates the board evidence structure through GDB,
and emits a Markdown performance summary.

Expected final markers:

```text
HOST_RESULT: PASS
RESULT: PASS
STM32H755 DAS raw-Ethernet HIL: PASS
```

The HIL runner keeps the terminal concise by default. Build/configuration
output is written under `build/das-raw-eth/logs/`; the terminal shows phase
transitions, dependency revisions, interface/carrier state, firmware size,
VSPW state transitions, host NIC packet deltas, board phase/fault registers,
and the final evidence paths. If a build or flash phase fails, the runner
prints the tail of the relevant full log automatically.

Evidence files are retained below
`build/das-raw-eth/evidence/` by default:

- `host-rtt.jsonl`: physical PC↔STM32 RTT distributions by logical payload;
- `board-evidence.log`: debugger-readable board state and raw cycle counters;
- `performance-summary.md`: generated combined report suitable for issue
  evidence;
- `openocd.log`: debugger/server log.

The board timing evidence uses the Cortex-M7 DWT cycle counter at the configured
400 MHz core clock. It records:

- successful logical `spw_port_send()` echo cost;
- successful `spw_port_receive()` call cost, explicitly polling-inclusive;
- `das_eth_send()` carrier-call cost;
- all `das_eth_receive()` poll cost plus successful-receive cost;
- empty RX poll count.

## Current measurement boundary

DAS Ethernet is polling-only in this baseline. A call to `das_eth_send()`
includes the board-side copy/cache work, descriptor submission and polling
until the TX DMA descriptor returns to software. `das_eth_receive()` exposes
a completed RX descriptor and performs cache invalidation/copy.

Therefore this integration can provide real MAC/DMA/PHY carrier evidence,
board-side cycle costs, polling burden, and physical PC-to-board-to-PC RTT.
The DAS TX timing includes its copy/cache work, descriptor submission and
polling until completion. Successful DAS RX timing covers completed-descriptor
handling plus cache invalidation/copy.

It must not claim IRQ-to-worker latency: there is no IRQ-driven DAS Ethernet
path yet. That metric is not applicable to this baseline and remains a future
comparison point if DAS gains an interrupt-driven Ethernet path.
