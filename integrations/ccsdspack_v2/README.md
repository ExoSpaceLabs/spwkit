# CCSDSPack 2.x integration

This integration proves the intended layering between CCSDSPack and SpWKit using only each project's installed public package API.

Pinned dependency:

- repository: `ExoSpaceLabs/CCSDSPack`
- release: `v2.0.0`
- commit: `c2f318c330c564429bcc565a8acbff22728b2851`

CCSDSPack is an integration dependency only. `libspwkit` does not include CCSDSPack headers, link to CCSDSPack, parse CCSDS/PUS packets, or depend on the CCSDSPack runtime.

## Layering

```text
CCSDSPack v2 PUS-C TC/TM construction
              |
              v
       serialized bytes
              |
              v
        SpWKit packet API
              |
      +-------+--------+
      |                |
 VSPW-TP/UDP      DEVICE / VSPD
      |                |
      +-------+--------+
              |
              v
        SpWKit packet API
              |
              v
      received byte vector
              |
      byte-for-byte check
              |
              v
CCSDSPack v2 typed parse + Validator
```

The SpaceWire EOP terminator remains transport metadata. It is not part of the serialized CCSDS packet bytes and is checked separately.

## Test contract

The standalone integration peer builds against installed `spwkit::cpp` and `ccsdspack::CCSDSPack` targets. Two independent processes are started for each transport path.

Peer A:

- creates a PUS-C telecommand with service `17/1`, source ID `0x1234`, acknowledgement flags `0x09`, and application bytes `60 70`;
- serializes it with CCSDSPack;
- transports the resulting bytes through SpWKit;
- independently constructs the expected PUS-C telemetry bytes for the peer response;
- requires exact byte equality before parsing;
- deserializes the received packet as `ccsds::pus::rev_c::TmHeader` and validates its metadata and packet coherence.

Peer B performs the inverse operation with a PUS-C telemetry packet using service `3/25`, message-type counter `7`, destination ID `0x0102`, time-reference status `5`, a four-octet implicit CUC timestamp, and application bytes `80 81`.

The same application-level packet contract executes over:

1. independent VSPW-TP/UDP peers;
2. independent Linux DEVICE peers attached to `vspwd` through VSPD.

## CI ownership

`.github/workflows/ccsdspack.yml` checks out the immutable CCSDSPack release, verifies its exact commit, builds and installs both projects independently, then builds this directory as a standalone consumer. This prevents a source-tree-only dependency from accidentally satisfying the integration.
