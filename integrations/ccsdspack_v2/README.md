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

## Packet contract

The standalone integration peer builds against installed `spwkit::cpp` and `ccsdspack::CCSDSPack` targets.

Peer A:

- creates a PUS-C telecommand with service `17/1`, source ID `0x1234`, acknowledgement flags `0x09`, and application bytes `60 70`;
- serializes it with CCSDSPack;
- transports the resulting bytes through SpWKit;
- independently constructs the expected PUS-C telemetry bytes for the peer response;
- requires exact byte equality before parsing;
- deserializes the received packet as `ccsds::pus::rev_c::TmHeader` and validates its metadata and packet coherence.

Peer B performs the inverse operation with a PUS-C telemetry packet using service `3/25`, message-type counter `7`, destination ID `0x0102`, time-reference status `5`, a four-octet implicit CUC timestamp, and application bytes `80 81`.

The application-level packet contract executes over:

1. independent VSPW-TP/UDP peers;
2. independent Linux DEVICE peers attached to `vspwd` through VSPD;
3. a deployment-shaped Docker Compose topology with two independent containers and IPv4 endpoints on an isolated Ethernet bridge.

## Two-node CCSDS over Ethernet

The Compose example combines the CCSDSPack packet layer and SpWKit distributed simulator into one end-to-end demonstration:

```text
peer-a container                         peer-b container
172.29.0.10                              172.29.0.11

PUS-C TC                                 PUS-C TC validation
CCSDSPack serialize                      CCSDSPack deserialize
      |                                         ^
      v                                         |
SpWKit packet API                         SpWKit packet API
      |                                         ^
      v                                         |
VSPW-TP / UDP -------- Ethernet -------- VSPW-TP / UDP
      ^                                         |
      |                                         v
SpWKit packet API                         SpWKit packet API
      ^                                         |
      |                                         v
PUS-C TM validation                      PUS-C TM
CCSDSPack deserialize                    CCSDSPack serialize
```

Run it from the repository root:

```bash
bash integrations/ccsdspack_v2/run_compose.sh
```

The wrapper:

- builds an image from `integrations/ccsdspack_v2/docker/Dockerfile`;
- fetches the immutable CCSDSPack `v2.0.0` tag and verifies its exact commit;
- installs CCSDSPack and SpWKit independently;
- builds this integration only through `find_package` and installed public targets;
- starts peer A and B in separate container/network namespaces;
- binds SpWKit UDP to `172.29.0.10` and `172.29.0.11` respectively;
- waits for both peer processes to terminate;
- requires exit status zero from both containers;
- requires the exact PUS-C TC/TM PASS record from each peer.

Successful completion prints:

```text
CCSDS_ETHERNET_COMPOSE_PASS
```

The peer still defaults to `127.0.0.1` for ordinary host-process tests. `SPWKIT_LOCAL_ADDRESS` and `SPWKIT_REMOTE_ADDRESS` override those addresses for separate hosts or containers.

This topology is software network-isolation evidence. It does not claim physical SpaceWire, FPGA, PHY or electrical interoperability.

## CI ownership

The consolidated `.github/workflows/ci.yml` owns the integration evidence. Its CCSDSPack job checks out the immutable dependency, verifies the exact commit, installs both projects independently, and runs the host UDP plus DEVICE/VSPD packet contract. The distributed Compose gate additionally runs `run_compose.sh`, requiring the two-container Ethernet PUS-C exchange to pass.

This separation prevents a source-tree-only dependency from accidentally satisfying the integration while keeping CCSDSPack outside `libspwkit` itself.
