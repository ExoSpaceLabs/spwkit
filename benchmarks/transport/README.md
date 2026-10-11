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

## Physical PC <-> Raspberry Pi 5: automated two-mode campaign

You should **not** install benchmark binaries manually or launch dozens of
sender/sink pairs by hand. The physical campaign is controlled from the PC over
SSH using one preparation step on the Pi.

### Before starting

- Connect the PC's benchmark Ethernet interface to Pi 5 \`eth0\` (direct
  cable or isolated switch). Do not use the PC's internet/Wi-Fi interface.
- Ensure the Pi is accessible from the PC over SSH, for example
  \`ssh youruser@10.77.77.2\`. OpenSSH can ask for the usual password or key
  once; the campaign reuses its connection.
- Both checkouts must be at the **same exact commit**. The script checks this
  and refuses to mix versions. Use the PR branch during qualification.
- The Pi script can configure \`10.77.77.2/24\` on \`eth0\`; the PC script
  configures \`10.77.77.1/24\` on its detected physical Ethernet interface.
  Existing IPs and default routes are not cleared or replaced. You can use
  \`--interface\` when there is more than one suitable wired NIC.
- The scripts install missing Debian/Ubuntu dependencies via \`apt-get\`, if
  necessary, after a normal sudo authentication. Builds and staged executables
  remain exclusively under \`build/physical-pi/\`. No global SpWKit install is
  modified. A per-user SSH endpoint wrapper is placed at
  \`~/.local/bin/spwkit-pi-endpoint\` on the Pi.
- \`cap_net_raw\` is applied to the campaign-local RAW executable; launching
  every RAW endpoint using sudo is not required.

### On the Pi (once, from its checkout)

\`\`\`sh
python3 benchmarks/transport/physical_pi_campaign.py --mode pi
\`\`\`

The Pi mode cleans old **campaign-owned** binaries, builds Release peers,
stages them, sets the necessary RAW socket capability, brings up Ethernet,
installs the fixed-purpose SSH endpoint wrapper and prints \`PASS\`.

### On the PC (from its checkout)

\`\`\`sh
python3 benchmarks/transport/physical_pi_campaign.py --mode pc \
  --pi-host <PI_SSH_USER>@10.77.77.2
\`\`\`

The PC mode does the same clean local setup, verifies the Pi commit over SSH,
discovers local/Pi MACs, and orchestrates the entire suite:

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

All results are saved under \`build/physical-pi/results-<UTC timestamp>/\`.
Failure preserves partial evidence. The PC script **does not** run a physical
PC↔PC baseline or Docker tests: those are covered by CI.

The SSH endpoint only launches the fixed benchmark binaries with validated
arguments; there is no remotely exposed benchmark control daemon. Sudo
permissions are requested during local preparation, not by each SSH test.

**Interpretation:** Duplex throughput is the sum of separately timed source
streams, not a synchronized line-capacity measurement. Physically measured
results are distinct from GitHub-hosted container results.

The \`--mode pi\` command must be re-run after the Pi's checkout changes or
after reboot if the temporary Ethernet address is lost. The PC refuses
revision mismatches. The scripts deliberately do not remove preexisting system
packages or network configuration; destructive global cleanup would be both
unnecessary and unsafe.

### Troubleshooting

Use \`--interface <NIC>\` on either host when interface detection is
ambiguous. If SSH is not reachable at \`10.77.77.2\`, verify the Pi is running
an SSH server and has a working physical link before starting the PC command.
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

Run the complete clock sweep and 1 GiB verified echo at each frequency:

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
