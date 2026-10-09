# Transport performance result registry

This directory is the long-lived index for end-to-end SpWKit transport evidence.

## CI campaign

The GitHub Actions workflow `Transport performance campaign` generates a report
for every relevant change. Each run performs repeated UDP and RAW Ethernet
Docker/veth transfers over multiple logical SpWKit payload sizes, then publishes:

- `samples.json`: every individual run;
- `summary.json`: aggregate statistics;
- `results.csv`: aggregate machine-readable table;
- `README.md`: rendered interpretation with tables and Mermaid chart;
- original UDP and RAW Docker logs.

CI values are intentionally treated as **runner-relative evidence**. GitHub-hosted
machines are not controlled benchmark hardware, so absolute values from unrelated
workflow runs must not be presented as qualified performance numbers. The useful
CI signals are payload-size trends, UDP-vs-RAW behavior within the same run,
fragmentation cost, packet-rate pressure, repeat variance, correctness and gross
regressions.

## Physical-device evidence

Physical results belong here once measured. Preserve the original machine-readable
evidence beside any rendered summary.

| Topology | Transport | Required measurements | Status |
|---|---|---|---|
| Native PC <-> Raspberry Pi 5 | UDP | both directions, duplex, payload sweep, sustained transfer | PENDING |
| Native PC <-> Raspberry Pi 5 | RAW Ethernet | both directions, duplex, payload sweep, sustained transfer | PENDING |
| Native PC <-> STM32H755 | RAW Ethernet | 64/200/300/400 MHz, payload sweep, 1 GiB verified transfer, cycle evidence | PENDING |
| Docker <-> Raspberry Pi 5 | UDP | selected native-vs-container deployment comparison | OPTIONAL |
| Docker <-> Raspberry Pi 5 | RAW Ethernet | selected native-vs-container deployment comparison | OPTIONAL |
| Docker <-> STM32H755 | RAW Ethernet | 400 MHz deployment comparison first | OPTIONAL |

For each physical campaign, record at minimum the SpWKit commit, DAS commit where
applicable, endpoint hardware/OS, CPU or MCU clock state, NIC/link configuration,
compiler/toolchain, build type, payload sizes, transfer sizes, direction/duplex
mode, validation errors and the raw result files.
