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
the CM7 image, establishes VSPW RUN over physical Ethernet, exercises a
configurable logical-payload RTT sweep, optionally performs a sustained
verified bulk echo, records host distributions/throughput, captures STM32 DWT
cycle evidence, validates the board evidence structure through GDB, and emits
a Markdown performance summary.

The board clock is selectable through DAS using only the stock-board profiles
advertised by the NUCLEO-H755ZI-Q backend: 64, 200, 300 and 400 MHz. The runner
checks the live clock readback against the requested profile.

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

## Clock-scaling throughput campaign

Run the full physical scaling campaign with:

```sh
bash scripts/stm32h755_das_raw_eth_campaign.sh \
  --interface enp0s31f6
```

The default campaign executes all four DAS-supported frequencies:

```text
64 MHz
200 MHz
300 MHz
400 MHz
```

For each frequency it runs the RTT payload sweep
`0,16,64,128,256,512,1024,1200,1400,4096` and then transfers 1 GiB of
deterministically generated 4096-byte logical packets. Every packet is echoed
by the STM32 and validated by the host before being discarded.

The aggregate output is written below
`build/das-raw-eth-campaign/`:

- `results.csv`: canonical machine-readable cross-clock results;
- `README.md`: generated tables and Mermaid `xychart-beta` plots;
- one `clock-<N>mhz/` directory containing the complete raw evidence for
  each clock profile.

The STM32 compact profile deliberately limits logical packets to 4096 bytes.
Larger logical payloads belong to the host/Pi transport campaign; the 1 GiB
STM32 test is a sustained transfer composed of many verified packets rather
than an attempt to allocate a 1 MiB MCU packet buffer.

The stock board backend intentionally rejects 480 MHz. DAS uses the
NUCLEO-H755ZI-Q direct-SMPS policy and caps that configuration at 400 MHz;
480 MHz would require a different LDO power-path/hardware configuration.
