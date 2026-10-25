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
