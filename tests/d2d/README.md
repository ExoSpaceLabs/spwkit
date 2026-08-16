# Device-to-device Tests

This directory contains distributed virtual SpaceWire verification. The D2D gate now has three complementary layers: public backend contract, transport-specific UDP tests, and actual process/network-isolation scenarios.

## Public and transport coverage

The Linux D2D workflow builds SpWKit and runs all tests carrying the `d2d` label, including:

- reusable `backend_contract_udp` public-API behavior;
- VSPW-TP/UDP packet fragmentation and reassembly;
- arbitrary fragment ordering and duplicate/overlap handling;
- EOP/EEP preservation and no-truncation retry;
- reverse/full-duplex traffic and time codes;
- ACK/retransmission, duplicate suppression and peer/session recovery;
- configurable virtual rate/latency behavior;
- deterministic transport and SpaceWire fault scenarios.

## Installed-package process isolation

The workflow then installs SpWKit and separately builds `examples/distributed` with `find_package(SpWKit 0.2 CONFIG REQUIRED)`. This matters: the process-isolation example consumes only installed public headers/targets and cannot accidentally reach source-private backend APIs.

`run_multi_process.sh` launches two independent instances of `spwkit_udp_peer` on localhost. It deliberately starts A before B, performs an 8 KiB full-duplex packet/time-code exchange, terminates B, waits for A to report `SPW_LINK_ERROR_WAIT`, starts a fresh B process/session, and verifies recovery plus a second exchange.

`run_netns.sh` repeats the same scenario with A and B in separate Linux network namespaces connected by a veth pair:

```text
process A                 process B
    |                         |
 libspwkit                  libspwkit
    |                         |
 netns A                    netns B
10.231.0.1                 10.231.0.2
    |                         |
    +---- veth / MTU 1500 ----+
```

The application packet is 8 KiB while the veth MTU remains 1500 bytes, so successful delivery demonstrates VSPW-TP fragmentation/reassembly across a real isolated IP boundary rather than shared process memory or loopback-only scheduling.

Peer A sends EOP and peer B sends EEP, and both exchange time codes. The restart phase proves that the surviving public port observes loss and accepts a newly created peer transport session without application access to VSPW-TP internals.

## Requirements

The ordinary two-process script needs only a POSIX host and the built peer executable.

The namespace script additionally requires:

- Linux network namespaces / `iproute2`;
- privileges to create namespaces and veth interfaces (`root` or `CAP_NET_ADMIN`);
- on GitHub-hosted Ubuntu runners the D2D workflow uses passwordless `sudo` and runs this gate actively.

Container startup order, socket bind success, or process creation alone are not treated as proof that a virtual SpaceWire peer is operational. The gates require verified SpaceWire-visible packet, terminator, time-code, loss and recovery behavior through the public API.
