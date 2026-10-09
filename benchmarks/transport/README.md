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

## UDP host baseline

Build and run a same-host baseline:

```sh
bash benchmarks/transport/run_udp_pair.sh \
  --payloads "64 256 1024 1400 4096 16384 65536 262144 1048576" \
  --total-bytes 1073741824
```

The runner writes JSONL evidence under `build/transport-udp/`.

For a physical PC↔Raspberry Pi 5 test, build the same executable on each host:

```sh
cmake -S benchmarks/transport -B build/transport -DCMAKE_BUILD_TYPE=Release
cmake --build build/transport --parallel --target spwkit_udp_perf
```

Example Pi sink:

```sh
build/transport/spwkit_udp_perf \
  --role sink \
  --local-address 192.0.2.20 --remote-address 192.0.2.10 \
  --local-port 43001 --remote-port 43000 --link-id 264 \
  --payload-size 4096 --total-bytes 1073741824
```

Matching PC source:

```sh
build/transport/spwkit_udp_perf \
  --role source \
  --local-address 192.0.2.10 --remote-address 192.0.2.20 \
  --local-port 43000 --remote-port 43001 --link-id 264 \
  --payload-size 4096 --total-bytes 1073741824
```

Use real addresses from the isolated benchmark network; the documentation
addresses above are placeholders.

## RAW Ethernet PC↔Pi 5

On Linux, the same transport build also produces
`spwkit_raw_ethernet_perf`. It uses AF_PACKET and therefore normally requires
root or an appropriate `CAP_NET_RAW` setup.

Pi sink:

```sh
sudo build/transport/spwkit_raw_ethernet_perf \
  --role sink --interface eth0 --remote-mac <PC-MAC> \
  --payload-size 4096 --total-bytes 1073741824
```

PC source:

```sh
sudo build/transport/spwkit_raw_ethernet_perf \
  --role source --interface <NIC> --remote-mac <PI-MAC> \
  --payload-size 4096 --total-bytes 1073741824
```

Swap source/sink for the reverse direction. For simultaneous bidirectional
traffic, run a second independent link in the opposite direction with both a
different `--link-id` and a different experimental `--ether-type`. The links
still share the same NIC and physical carrier, but AF_PACKET can demultiplex
them before one benchmark process is forced to drain the other link's traffic.
The Docker duplex campaign uses `0x88B5` and `0x88B6` for this reason.

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
