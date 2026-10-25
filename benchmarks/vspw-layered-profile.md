# VSPW-TP in-memory CPU profile

The **first layer** in issue #266 is a deterministic in-memory VSPW-TP
engine profile, compiled as a private C11 test target and smoke-tested in CI.

It is deliberately **not** a physical Ethernet or STM32 throughput test.
It measures process CPU time for a repeated packet transaction over a
bounded in-memory fixture, *including* both engines and their fixture costs.

The test asserts byte-for-byte correctness and exact DATA/ACK accounting,
with no retransmissions. Compare `--ack-pump immediate` versus
`--ack-pump deferred`: the latter forces the next send operation to service
the previous message's ACK, quantifying how the current TX-slot constraint
affects an application's call pattern. The engine diagnostic `tx_slot_wait_calls`
counts those transitions; the runtime-clock elapsed counters separately
measure slot/provider waiting in a real-clock provider.

## Build and run

```bash
cmake -S . -B build/vspw-profile -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_CPP_TESTS=OFF -DSPWKIT_BUILD_EXAMPLES=OFF
cmake --build build/vspw-profile --target spwkit_vspw_engine_profile
ctest --test-dir build/vspw-profile \
  -R 'vspw_(engine_in_memory|profile_)' --output-on-failure

for size in 0 64 1400 1401 4096 8192; do
  for mode in immediate deferred; do
    ./build/vspw-profile/tests/spwkit_vspw_engine_profile \
      --payload "$size" --fragment 1400 --iterations 2000 --ack-pump "$mode"
  done
done
```

Output is JSONL, one record per run. Capture multiple independent runs
on the **same host and build** before drawing conclusions. Never compare
absolute CPU time from different architectures as though CPU frequencies,
caches and memory were equivalent. This phase deliberately does not publish
a hard throughput pass/fail threshold: correctness and accounting are the CI
gates, performance is diagnostic evidence.

For later layer attribution use the separate carrier benchmarks in #265
(UDP, AF_PACKET and physical DAS RAW Ethernet), with the same payload,
timeout, fragment size and workload. The in-memory queue depth is eight
and its maximum frame size is 2048 bytes. These are fixture limits, not
VSPW wire-format limitations. It supports no more than eight queued
fragments per logical packet; `--fragment` accepts up to 2008 bytes.

Hardware operator action is **not required** for this profiling stage.

## CI evidence: repeatable protocol-only CPU comparison

The `Transport performance campaign` workflow now builds the same private
`spwkit_vspw_engine_profile` test target and executes three independent
1500-transaction measurements for each payload (0, 64, 1400, 1401,
4096 and 8192 bytes), in immediate-ACK and deferred-ACK modes.
`build/vspw-cpu-profile/samples.jsonl` preserves individual measurements;
`summary.md` reports per-mode CPU time, DATA frames, and TX-slot waits.
CI asserts zero retries and timeouts on the clean in-memory fixture but
**does not** gate on hosted CPU speed or treat it as physical throughput.
