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
