# Internal VSPW-TP diagnostic counters

Post-v0.7 diagnostic groundwork for #266. Only the private
`spw_vspw_engine_t` changes. Public `spw_port_*` and
`spw_statistics_t`, wire framing, timeout/retry defaults, and DAS boundaries
are unchanged. Counters are fixed-size, per-engine, cumulative from init,
non-atomic under same-handle serialization, and contain no dynamic allocation.

`tx_*_frames` counts successful provider submissions by message type;
`rx_*_frames` counts decoded accepted peer/session frames, not wire
transmissions. `retry_attempts` counts whole-message retry invocations;
`retransmitted_frames` counts successfully submitted fragments/frames
in those invocations. `ack_timeouts` counts serviced expired deadlines;
`ack_matched`, `ack_unmatched`, and `ack_wrong_session` classify
received ACKs. `ack_age_us_*` measures time from the **most recent**
complete pending-message send until ACK **processing**, inclusive of
application pumping delays; it is not pure wire latency. Eight latency
buckets use bounds <=100, <=1000, <=5000, <=10000, <=20000, <=50000,
<=100000, and >100000 microseconds.

The byte-copy counters cover selected engine memcpys only and are not a
total memory-bandwidth metric. Explicit invalid, duplicate and conflict
counters distinguish invalid input from recovered retries.

`tests/vspw_engine.c` checks frame counts and deterministically drops a
queued ACK to verify retry diagnostics without network or board hardware.

Next: benchmark-side private snapshots plus timing decomposition for
provider wait/poll, encode/decode and fragment assembly; verify the
instrumentation's own overhead with paired builds before treating timing
figures as a performance baseline. Repeat physical runs using DAS v0.1.0.

## Host-only layered profiler (post-#267)

`spwkit_vspw_engine_profile` is a C11 private-engine benchmark using the
bounded in-memory transport (no network, hardware, root or simulator daemon).
It validates application bytes, exact DATA/ACK frame accounting, retry-free
operation, and two ACK-pumping modes. `immediate` pumps the ACK after each
receive; `deferred` forces the next send to service the outstanding ACK and
thereby exercises TX-slot waiting. The input ceiling is 8192 bytes with the
default 1400-byte fragment profile and an eight-frame fixture queue.

Reported CPU time uses the process `clock()` around the whole repeated
send/receive/ACK transaction. It includes in-memory copying, protocol logic,
and application test-fixture overhead. It is **not** a VSPW-only CPU counter,
physical throughput, carrier throughput, or a hardware throughput promise.
The engine separately tracks provider elapsed wait time (which can include
blocking) and TX-slot elapsed wait time in its runtime-clock domain.
No threshold is placed on variable hosted CPU timings.

Run it with:
```bash
cmake -S . -B build/vspw-profile -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BUILD_EXAMPLES=OFF -DSPWKIT_BUILD_CPP_TESTS=OFF
cmake --build build/vspw-profile --target spwkit_vspw_engine_profile
./build/vspw-profile/tests/spwkit_vspw_engine_profile --payload 1401 --iterations 3000 --ack-pump deferred
ctest --test-dir build/vspw-profile -R 'vspw_(engine_in_memory|profile_)' --output-on-failure
```

The next step is integrating private counter snapshots with the `#265`
host and STM32 harness, with an instrumentation-overhead control.
