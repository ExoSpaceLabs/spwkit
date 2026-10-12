# Physical three-device benchmark analysis, 2026-10-11

## Provenance and qualification status

- Source: user-supplied full physical evidence archive `results-20261011T164753Z.zip`.
- Original controller revision: `b47ca0a30797af9e8f8dc8319db28ba70816e08a`.
- **Historical DAS source commit: `4b768ef86b43652c94cc91b1c77e247fa37ebd8a` (pre-release).** These results were collected *before* DAS `v0.1.0` was selected as the new qualification reference (release commit `8553d903a615fe99e2a3a434f876b51f9542fd46`). Do not relabel them as v0.1.0 measurements; after instrumenting issue #266, repeat hardware qualification against the tagged release.
- Controller profile: `benchmark`. Verified 16 MiB echo with 4096-byte logical payload at each clock, plus RTT sweeps (128 measured iterations, 16 warm-ups each payload).
- Transport: VSPW-TP over RAW Ethernet; stock NUCLEO-H755ZI-Q, STM32 CM7 at **64/200/300/400 MHz**, 100 Mbps full duplex; DAS polling backend.
- Completed topologies: PC↔Pi UDP/RAW; PC↔STM32 RAW; Pi↔STM32 RAW. PC↔Pi measured summary is preserved separately at `../physical-2026-10-11-pc-pi/`.
- For **all eight STM32 runs**: host `HOST_RESULT: PASS`, board `RESULT: PASS`, `phase=0x0000700d`, `result=0`, measured core clock equals requested profile, `das_tx_failures=0`, `das_rx_errors=0`, processor fault registers zero, `echoed_packets=5536`. Both profiles transferred **16,777,216 verified application bytes in 4,096 echo packets per clock**.
- All results below are **one bulk trial per frequency and host**. The RTT sweep has 128 iterations per payload, but bulk rate has no between-run error bars. Full raw evidence and executable/build logs remain in the user-supplied archive; this repository file contains an aggregate analysis without local user/device identifiers.

The [machine-readable eight-run summary](summary.csv) contains throughput, RTT, host carrier-frame counts and board DAS status. Follow-up tracked in [#266](https://github.com/ExoSpaceLabs/spwkit/issues/266).

## Verified throughput and latency

The rates are **logical bytes per time spent sending and waiting for verified echoes**. They are not unidirectional physical-carrier goodput and must not be compared directly to PC↔Pi sender-throughput results.

| CM7 clock | PC↔STM32 echo-derived logical Mbps | Pi↔STM32 echo-derived logical Mbps | Pi/PC rate | PC median RTT, 4096 B | Pi median RTT, 4096 B |
|---:|---:|---:|---:|---:|---:|
| 64 MHz | 3.463 | 1.028 | 0.297× | 9.363 ms | 31.871 ms |
| 200 MHz | 1.886 | 10.134 | 5.373× | 24.530 ms | 3.233 ms |
| 300 MHz | 6.386 | 8.444 | 1.322× | 4.018 ms | 3.880 ms |
| 400 MHz | 2.108 | 10.909 | 5.174× | 24.263 ms | 3.004 ms |

Bulk elapsed durations for each 16 MiB: PC 38.761/71.158/21.016/63.659 s; Pi 130.529/13.244/15.895/12.303 s, respectively.

The non-monotonic rates are **not** credible evidence that raising CPU frequency itself degrades performance. There are large host-dependent timing discontinuities and extra transmitted carrier frames. Latency at zero-length payload is only ~1.26–1.35 ms at 200/400 MHz on both hosts, while some corresponding 4096 B RTT medians jump to ~24–25 ms.

## Frame-count anomaly and leading investigation hypothesis

Each 4096 B logical packet produces **five transmitted DATA carrier frames in clean 16 MiB runs**, totaling 20,480 host TX frames for 4096 packets. The actual host TX counts during the bulk interval are:

| Host | Clock | TX carrier frames | Frames per 4096 B packet | Increase above 20,480 baseline |
|---|---:|---:|---:|---:|
| PC | 64 MHz | 20,537 | 5.014 | +0.28% |
| PC | 200 MHz | 28,496 | 6.957 | +39.14% |
| PC | 300 MHz | 21,122 | 5.157 | +3.13% |
| PC | 400 MHz | 27,533 | 6.722 | +34.44% |
| Pi | 64 MHz | 32,768 | 8.000 | +60.00% |
| Pi | 200 MHz | 20,480 | 5.000 | 0% |
| Pi | 300 MHz | 20,480 | 5.000 | 0% |
| Pi | 400 MHz | 20,480 | 5.000 | 0% |

The affected clock/host combinations are also those with inflated large-packet RTTs and reduced bulk throughput. STM32 `das_rx_success_count` and `das_rx_empty_polls` increase substantially in several of these configurations. Board DAS reports no TX failures or RX errors, which confirms application-level success but does **not** rule out recovered transport retransmissions.

The benchmark sets `raw.ack_timeout_ms = 20` and `raw.max_retries = 3`. In `src/backends/ethernet/vspw_engine.c`, `service_pending_tx()` re-transmits pending traffic after `ack_timeout_ms`. The approximately 20 ms RTT inflation and elevated TX frame counts are **consistent with ACK timeout/retry behavior**, but this is a hypothesis until explicit retries/ACK timing or packet capture confirms it. Carrier frame counts can also include other protocol/control traffic and must not be naively labeled exact retransmission counts.

## Clock/DAS evidence

Board-side per-carrier DAS operations are remarkably similar between PC and Pi at the **same** clock (as expected, because the STM32 firmware is otherwise identical). Approximate board DAS send-call mean times, using cycles / observed core clock:

| Clock | DAS send mean time (PC host) | DAS send mean time (Pi host) |
|---:|---:|---:|
| 64 MHz | 568 µs | 567 µs |
| 200 MHz | 225 µs | 225 µs |
| 300 MHz | 266 µs | 266 µs |
| 400 MHz | 215 µs | 215 µs |

The differences in bulk throughput cannot be attributed to this board send-call timing alone. The path is polling-only; no IRQ-to-worker latency was measured.

## Interpretation and next validation

**Physical functional qualification: PASS.** All three interconnections and all eight STM32 clock trials successfully transferred verified data without DAS-reported failure or a processor fault.

**Performance characterization: BASELINE COLLECTED, BOTTLENECK ATTRIBUTION PENDING.** Do not use the 1.03–10.91 Mbps variation to infer CPU scaling without resolving the large retransmission/timing anomalies. The 100 Mbps physical link rate is not an appropriate direct baseline for stop-and-wait echoed logical packet throughput.

Follow-up acceptance criteria:

1. Instrument **ACK attempts, retransmitted logical packets/fragment frames, timeouts, and retry distributions** in VSPW-TP, without breaking the public API or introducing embedded heap allocations.
2. Compare ACK timeout behavior at the existing **20 ms** and controlled longer values, e.g. 50/100 ms. Do not silently change retry timing in the baseline runs.
3. Capture source-side RAW Ethernet frames/ACKs (e.g. via `tcpdump`/AF_PACKET on the host) to correlate retries with 4 KiB RTT spikes and excess TX frame ratios. Preserve paired host/board counters.
4. Run the same 16 MiB benchmark **at least three times** per host and clock with identical conditions and record median/spread. Randomize or alternate run order if thermal/network drift may bias results.
5. Investigate polling and ACK processing, per-fragment handling, and Ethernet backpressure/cache/descriptor waits before targeting CPU optimization. Validate a hypothesis with controlled before/after tests.

The summary template currently labels the Pi STM32 RTT section as “Physical PC↔STM32 RTT”; it should use the actual host label on future runs.
