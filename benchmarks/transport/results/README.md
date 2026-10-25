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

The automated physical PC/Pi campaign writes source/sink JSON, error logs,
environment metadata, `samples.json`, `results.csv`, and `README.md` in
`build/physical-pi/results-<timestamp>/` on the PC. No per-payload endpoint
commands are required. After verifying the data, archive/import this directory
into the physical result registry.

The autonomous STM32 HIL clock campaign writes separate evidence at all four
frequency profiles under `build/das-raw-eth-campaign/` plus an aggregate
`results.csv` and Markdown/Mermaid report. Both physical campaigns are
user-initiated and cannot be qualified by GitHub-hosted CI alone.

| Topology | Transport | Required measurements | Status |
|---|---|---|---|
| Native PC <-> Raspberry Pi 5 | UDP | both directions, duplex, payload sweep, sustained transfer | COMPLETE (2026-10-11) |
| Native PC <-> Raspberry Pi 5 | RAW Ethernet | both directions, duplex, payload sweep, sustained transfer | COMPLETE (2026-10-11) |
| Native PC <-> STM32H755 | RAW Ethernet | 64/200/300/400 MHz, RTT sweep, 16 MiB verified echo per clock, cycle evidence | COMPLETE (2026-10-11), performance anomalies under investigation |
| Native Pi 5 <-> STM32H755 | RAW Ethernet | 64/200/300/400 MHz, RTT sweep, 16 MiB verified echo per clock, cycle evidence | COMPLETE (2026-10-11), performance anomalies under investigation |
| Docker <-> Raspberry Pi 5 | UDP | selected native-vs-container deployment comparison | OPTIONAL |
| Docker <-> Raspberry Pi 5 | RAW Ethernet | selected native-vs-container deployment comparison | OPTIONAL |
| Docker <-> STM32H755 | RAW Ethernet | 400 MHz deployment comparison first | OPTIONAL |

For each physical campaign, record at minimum the SpWKit commit, DAS commit where
applicable, endpoint hardware/OS, CPU or MCU clock state, NIC/link configuration,
compiler/toolchain, build type, payload sizes, transfer sizes, direction/duplex
mode, validation errors and the raw result files.

## Completed physical PC–Pi evidence (2026-10-11)

Archived human-operated run: [CSV](physical-2026-10-11-pc-pi/results.csv) and
[Markdown report](physical-2026-10-11-pc-pi/README.md).
These are the **actual measured summary results** uploaded after the successful
PC↔Pi run, not CI data. Source revision: d2941e2e3e970bf942710a5a62ab8936bd3780b8.
The uploaded material contained the aggregate report and CSV, **not the
individual endpoint JSON or full environment files**; those must remain
locally archived for later deep analysis. Do not claim this Git copy alone
contains all raw measurement evidence.

Preferred future entry point: `python3 scripts/physical_transport_campaign.py`
from the PC, which can reuse a complete local PC↔Pi baseline and proceeds to
PC↔STM32 and Pi↔STM32 after prompting for the physical cable changes.

## Completed physical three-device campaign (2026-10-11)

[Analysis and actionable follow-up](physical-2026-10-11-three-device/analysis.md)
documents the completed PC↔Pi, PC↔STM32 and Pi↔STM32 runs, including
all eight STM32 16 MiB transfers, four CM7 clock profiles, timing and
MAC/driver counters. Both STM32 topologies passed protocol correctness and
board-side fault checks. However, non-monotonic throughput and repeated
carrier frames in several host/clock cases require **ACK/retry and scheduling
investigation** before reporting CPU-scaling conclusions.

The full evidence archive was supplied separately, including per-clock
board evidence, host JSONL, OpenOCD/GDB/build logs and original PC↔Pi samples.
It was **not** bulk-committed here: it contains compiled toolchain artifacts,
local device addresses/host paths, and files not suitable for a public source
repository. The analysis is a sanitized aggregation. Preserve the original
archive independently for reproducing diagnostics.
