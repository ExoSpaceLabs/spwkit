# Parser fuzz targets

These targets provide bounded sanitizer-backed fuzz coverage for private wire
parsers that consume externally influenced bytes. They are robustness evidence,
not public API and not physical SpaceWire qualification.

## Targets

- `vspw_tp_fuzz`: VSPW-TP header and ACK decoding, with valid decoded headers
  exercised through the encoder as a round-trip safety path.
- `vspd_frame_fuzz`: VSPD header decoding and complete-record validation, with
  valid decoded headers exercised through the encoder.
- `fragment_reassembler_fuzz`: bounded fragment streams covering ordering,
  duplicates, overlap/conflict handling, completion boundaries and reset safety.

The ordinary CI workflow runs a short deterministic smoke using libFuzzer,
ASan and UBSan. Longer campaigns can reuse the same binaries with a larger
`-max_total_time` and a persistent corpus directory.

Example:

```sh
clang -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
  -Isrc tests/fuzz/vspw_tp_fuzz.c src/backends/ethernet/vspw_tp.c \
  -o vspw_tp_fuzz
mkdir -p corpus/vspw
./vspw_tp_fuzz corpus/vspw -max_total_time=3600 -timeout=5
```

Any input that exposes a defect should be minimized and committed as a
regression fixture in a follow-up change before the defect is considered
closed.


## Lifecycle and reconnect soak

The robustness workflow also runs three sanitizer-backed stateful soak targets:

- `simulator_lifecycle_ownership_soak` repeatedly opens, starts, transfers copied
  and zero-copy traffic, exhausts and recovers the TX pool, verifies stale
  zero-copy handles are rejected across reset epochs, then stops and closes.
- `udp_restart_reconnect_soak` sustains bidirectional fragmented traffic while
  repeatedly closing/reopening one peer and requiring the surviving peer to
  observe loss, accept the new transport session, and resume traffic.
- `device_vspwd_lifecycle_soak` repeatedly starts a fresh Linux `vspwd`
  daemon, exercises public DEVICE peers through loss/restart/reconnect, and
  terminates the daemon cleanly before the next iteration.

Ordinary CI uses 10 iterations as a bounded smoke. A manual Robustness workflow
dispatch defaults to 500 iterations and accepts `soak_iterations` from 1 to
10000. The exact iteration count is exported as `SPWKIT_SOAK_ITERATIONS`, so
the same campaign is reproducible locally:

```sh
cmake -S . -B build-soak \
  -DSPWKIT_BUILD_TESTS=ON -DSPWKIT_BUILD_CPP_TESTS=ON \
  -DSPWKIT_BUILD_SIMULATOR=ON -DSPWKIT_BUILD_UDP=ON \
  -DSPWKIT_BUILD_DEVICE=ON -DSPWKIT_BUILD_VSPWD=ON
cmake --build build-soak --parallel
SPWKIT_SOAK_ITERATIONS=500 ctest --test-dir build-soak -L soak --output-on-failure
```

The UDP fault engine is not enabled in this soak; reconnect behavior is driven
by deterministic peer close/reopen cycles. The simulator payload patterns are
derived from the iteration number, so no random seed is required. These runs
are software robustness evidence only and are not physical SpaceWire
qualification.
