# End-to-end transport performance

This directory contains the reproducible end-to-end performance harness tracked
by #264. It complements the micro/layer profiling in `benchmarks/`: these
tests move and validate sustained logical SpaceWire payloads across real
transport providers.

## Measurement rules

- Use Release builds.
- Generate payloads deterministically in memory.
- Validate every received byte and discard it; do not benchmark storage.
- Keep CSV/JSON/log output as canonical evidence and generate charts from it.
- Record the exact SpWKit commit, topology, interface/link configuration and
  host details beside physical results.
- Establish a baseline before changing an implementation.
- For full-duplex saturation, use two independent SpWKit links concurrently so
  each port still follows the same-handle serialization contract.

## Three-device physical qualification: ONE PC command

The preferred operator entry point is **one command on the Ubuntu PC**. Do not
manually clone or install SpWKit on the Pi. The controller provisions a pinned,
isolated Pi checkout via its existing **Wi-Fi SSH** service, detects the wired
interfaces, builds both Linux peers, flashes the STM32 through PC ST-LINK USB,
reuses the common STM32 firmware/clock-sweep methodology, and consolidates all
three physical topologies.

```bash
python3 scripts/physical_transport_campaign.py
```

You will be prompted for the Pi's Wi-Fi SSH destination
(e.g. `exn-cam@192.168.1.36`) and, when necessary, your SSH/sudo password.
The PC Ethernet interface is selected automatically if unambiguous. You can
also supply `--pi-ssh USER@HOST --pc-interface NIC` noninteractively for the
parameters. Do **not** use `--yes` unless you have physically confirmed each
requested cable change.

This is a **test-harness-specific external dependency**, not a
SpWKit library or transport-provider requirement. The generic SpWKit library,
its UDP/RAW Ethernet host tests, and non-STM32 targets build independently of
DAS. The optional STM32 integration owns its pin in
[`das_release.env`](../../integrations/das_stm32h755_raw_eth/das_release.env).

The STM32 dependency is always the verified official DAS
[`v0.1.0` release](https://github.com/Inczert/device-abstraction-stack/releases/tag/v0.1.0)
(immutable commit `8553d903a615fe99e2a3a434f876b51f9542fd46`), built from its source
for the CM7 integration. Individual measurements record this identity. The
October 11 results were collected on the previous DAS commit and remain
historical evidence, **not** qualification evidence for the new release.

The controller orchestrates:

1. **PC ↔ Pi**: Pi Wi-Fi remains available for SSH; the Ethernet cable runs
   from PC to Pi. Run all UDP/RAW bidirectional/duplex, repeated payload sweeps
   and sustained transfers. A previous **complete** physical PC↔Pi dataset
   under `build/physical-pi/results-*/` is reused by default, preserving
   its exact measured commit; use `--fresh-pc-pi` to force a fresh run.
2. **PC ↔ STM32H755**: move the Ethernet cable to the NUCLEO CN14; keep the
   NUCLEO ST-LINK USB on the PC. PC automatically builds, flashes and verifies
   all **64/200/300/400 MHz** clock profiles, RTT/cycle measurements and
   16 MiB verified echoed transfers by default. Use `--profile stress` to
   opt into 1 GiB long-duration validation.
3. **Pi ↔ STM32H755**: move the Ethernet cable from the PC to Pi `eth0`,
   leaving the NUCLEO ST-LINK USB on the PC. The **PC still flashes and reads
   the MCU using OpenOCD/GDB**; the Pi executes the native Linux RAW Ethernet
   host peer via SSH. Repeats the same complete four-clock STM32 campaign.

The only operator actions are **initial PC command, SSH/sudo authentication,
and confirming physical reconnections**. A program cannot reconnect the cable
or move the ST-LINK USB. The Pi staging checkout is automatically cleaned
after full success; failure preserves evidence for diagnosis and does not
delete unrelated Pi work or system installations.

Artifacts: `build/three-device-campaign/results-<UTC>/` with
`pc-pi/`, `pc-stm32/`, `pi-stm32/`, phase logs and an overall README.

**Measurement boundary:** PC↔Pi reports sender goodput (duplex is summed
separately timed streams). STM32 HIL reports echoed RTT, bulk echo rate and
board/DAS execution-cycle/polling evidence. These quantities must not be
misrepresented as identical one-way carrier capacity metrics.

This unified script is **hardware qualification**, not executable on hosted
CI without the attached Pi and NUCLEO. CI covers syntax and orchestration
contracts only. Physical Pi↔STM32 must be run to establish actual results.

## Physical PC <-> Raspberry Pi 5 (standalone lower-level runner): automated two-mode campaign

You should **not** install benchmark binaries manually or launch dozens of
sender/sink pairs by hand. The physical campaign is controlled from the PC over
SSH using one preparation step on the Pi.

### Before starting

- **Two separate connections:** keep the Pi connected to Wi-Fi for SSH
  management. Also connect Pi `eth0` directly to the PC's **wired**
  Ethernet interface, or through a dedicated Ethernet switch. Only the wired
  link carries benchmark traffic. The PC may keep Wi-Fi for internet/SSH.
- Use the Pi's **existing Wi-Fi IP address or hostname** for SSH, for example
  `ssh youruser@192.168.1.42`. Do **not** mistake `10.77.77.2`
  (the benchmark Ethernet address) for the Wi-Fi SSH destination. OpenSSH
  can ask for the usual password or key once; the campaign reuses its
  connection.
- Both checkouts must be at the **same exact commit**. The script checks this
  and refuses to mix versions. Use the PR branch during qualification.
- The Pi script can configure `10.77.77.2/24` on `eth0`; the PC script
  configures `10.77.77.1/24` on its detected physical Ethernet interface.
  Existing IPs and default routes are not cleared or replaced. You can use
  `--interface` when there is more than one suitable wired NIC.
- The scripts install missing Debian/Ubuntu dependencies via `apt-get`, if
  necessary, after a normal sudo authentication. Builds and staged executables
  remain exclusively under `build/physical-pi/`. No global SpWKit install is
  modified. A per-user SSH endpoint wrapper is placed at
  `~/.local/bin/spwkit-pi-endpoint` on the Pi.
- `cap_net_raw` is applied to the campaign-local RAW executable; launching
  every RAW endpoint using sudo is not required.

### On the Pi (once, from its checkout)

```sh
python3 benchmarks/transport/physical_pi_campaign.py --mode pi
```

The Pi mode cleans old **campaign-owned** binaries, builds Release peers,
stages them, sets the necessary RAW socket capability, brings up Ethernet,
installs the fixed-purpose SSH endpoint wrapper and prints `PASS`.

### On the PC (from its checkout)

```sh
python3 benchmarks/transport/physical_pi_campaign.py --mode pc \
  --pi-host <PI_SSH_USER>@<PI_WIFI_IP>
```

The PC mode does the same clean local setup, verifies the Pi commit over SSH,
and **probes both hosts' UDP socket binds before starting any benchmark**.
It chooses a free base-port pair automatically when the default ports are
occupied and stops immediately with the OS error if the test IP is unavailable
or socket creation is denied. The selected ports are saved in the environment
evidence. If a port-open error still occurs, the UDP benchmark prints a
socket/bind diagnostic instead of only SpWKit's generic backend code.
It then discovers local/Pi MACs, runs a **short UDP and RAW Ethernet
physical smoke** to establish end-to-end packet exchange, and only after that
orchestrates the full suite:

- UDP and RAW Ethernet;
- logical payloads 64/256/1024/1400/4096/16384/65536 bytes;
- PC→Pi, Pi→PC and concurrent duplex modes;
- three independent 8 MiB verified runs for each case;
- sustained 1 GiB runs at 4096 and 65536 bytes in each direction, for each
  transport;
- deterministic payload verification at each receiving endpoint, sender/sink
  byte and packet count reconciliation, and nonzero error/drop rejection;
- reports including raw endpoint logs, per-run JSON, aggregate CSV,
  mean/median/stddev/min/max throughput, and exact environment/revision data.

All results are saved under `build/physical-pi/results-<UTC timestamp>/`.
Failure preserves partial evidence. The PC script **does not** run a physical
PC↔PC baseline or Docker tests: those are covered by CI.

The SSH endpoint only launches the fixed benchmark binaries with validated
arguments; there is no remotely exposed benchmark control daemon. Sudo
permissions are requested during local preparation, not by each SSH test.

**Interpretation:** Duplex throughput is the sum of separately timed source
streams, not a synchronized line-capacity measurement. Physically measured
results are distinct from GitHub-hosted container results.

The `--mode pi` command must be re-run after the Pi's checkout changes or
after reboot if the temporary Ethernet address is lost. The PC refuses
revision mismatches. The scripts deliberately do not remove preexisting system
packages or network configuration; destructive global cleanup would be both
unnecessary and unsafe.

### Troubleshooting

Use `--interface <NIC>` on either host when interface detection is
ambiguous. If SSH is not reachable through the Pi's existing Wi-Fi address,
fix that management connection first. The PC runner independently verifies
the wired Ethernet carrier before traffic starts. Pi preparation may finish
even while the PC's wired NIC is still administratively down; PC mode
brings that NIC up automatically.
The PC reports the failing transport/mode/payload and retains the logs.

## Docker container-to-container UDP

Run both unidirectional and concurrent bidirectional campaigns with:

```sh
bash benchmarks/transport/run_udp_docker.sh \
  --mode both \
  --payloads "64 256 1024 1400 4096 16384 65536 262144 1048576" \
  --total-bytes 1073741824
```

The duplex profile starts two independent VSPW/UDP links between the same two
containers, one in each direction. This measures shared container/bridge/CPU
contention without making concurrent calls on one SpWKit port.

## Docker container-to-container RAW Ethernet

Run verified RAW Ethernet across Docker's bridge/veth Layer-2 path:

```sh
bash benchmarks/transport/run_raw_docker.sh \
  --mode both \
  --payloads "64 256 1024 1400 4096 16384 65536 262144 1048576" \
  --total-bytes 1073741824
```

The RAW containers use fixed locally administered MAC addresses and only
`CAP_NET_RAW`; privileged containers are not required. The Docker bridge is
backed by veth pairs, so this exercises AF_PACKET, RAW Ethernet framing,
fragmentation/reassembly and concurrent opposite-direction traffic through a
real Linux Layer-2 virtual path.

CI runs a repeated UDP and RAW Ethernet campaign over this topology rather than
a single fixed-size smoke. The current CI sweep covers multiple logical SpWKit
payload sizes in both unidirectional and duplex modes and repeats each case
within one workflow run.

The workflow generates per-run JSON, aggregate JSON/CSV, a Markdown statistics
report and a Mermaid payload-size trend. Median throughput is the primary
comparison value; mean, standard deviation, range, packet rate and RAW
carrier-frames-per-logical-packet are retained to make host noise and transport
overhead visible. Carrier frame counts include VSPW-TP control/ACK/keepalive
traffic where present; the frame ratio is not a pure data-fragment count.

Treat CI timings as runner-relative correctness/regression evidence, not as
authoritative hardware throughput. GitHub-hosted machines are not controlled
benchmark platforms. Comparisons within one workflow run are useful; absolute
values across unrelated runners may move for reasons outside SpWKit.

The long-lived result registry, including placeholders for physical Pi and
STM32 evidence, is in `benchmarks/transport/results/README.md`.

## Physical-device qualification

Manual testing is reserved for cases where the external device changes the
answer:

- native PC <-> Raspberry Pi 5: UDP and RAW Ethernet;
- native PC <-> STM32H755: RAW Ethernet only;
- optional Docker <-> Raspberry Pi 5: UDP and RAW Ethernet deployment overhead;
- optional Docker <-> STM32H755: RAW Ethernet deployment overhead, initially at
  400 MHz only.

Physical PC <-> PC Ethernet is intentionally not part of the campaign. The
Docker/veth CI topology covers virtual Linux transport behavior more
reproducibly, while the Raspberry Pi provides the useful physical Linux
endpoint.

## STM32H755 + DAS RAW Ethernet

The embedded campaign is documented in
`integrations/das_stm32h755_raw_eth/README.md`.

The stock NUCLEO-H755ZI-Q DAS backend advertises exactly:

- 64 MHz
- 200 MHz
- 300 MHz
- 400 MHz

Run the complete **64/200/300/400 MHz** performance clock sweep with a
16 MiB verified bulk echo at each frequency (default):

```bash
bash scripts/stm32h755_das_raw_eth_campaign.sh --profile benchmark
```

For **optional endurance/stress testing** use:

```bash
bash scripts/stm32h755_das_raw_eth_campaign.sh --profile stress
```

The opt-in stress profile defaults to 400 MHz and 1 GiB. Select additional
clock profiles explicitly with `--clocks "64000000 ..."` when required.

The one-command controller also accepts `--profile benchmark|stress`.
The current benchmark does not claim statistical confidence for long-term
failure rates: long transfers are deliberately separate.

```sh
bash scripts/stm32h755_das_raw_eth_campaign.sh --interface <NIC>
```

The campaign generates `results.csv` plus Markdown/Mermaid graphs under
`build/das-raw-eth-campaign/`.

## STM32 transport scope

STM32H755 benchmarking is intentionally **RAW Ethernet only**.

The current DAS integration exposes the Ethernet MAC/DMA at Layer 2, which is
also the embedded transport path this campaign is intended to qualify. Adding
an IP/UDP stack such as lwIP solely to create a comparison benchmark is outside
the scope of #264.

UDP performance evidence is limited to Linux-class endpoints such as PC,
Raspberry Pi 5 and Docker containers. PC↔STM32 UDP is not a deferred benchmark
requirement and should not be treated as a future TODO for this campaign.

## Result interpretation

Throughput alone is not sufficient. Compare it with packet rate, carrier-frame
counts, CPU utilization and, on STM32, DWT/DAS cycle evidence. A throughput
plateau that stops scaling with CM7 frequency is evidence that the bottleneck
has moved away from raw CPU execution and should be correlated with DMA,
memory/copy, polling, fragmentation/reassembly and carrier behavior before any
optimization is accepted.
