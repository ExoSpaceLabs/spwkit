# Device-to-device Tests

This directory represents distributed virtual SpaceWire verification.

The current GitHub Actions D2D gate builds SpWKit on Linux and runs the real VSPW-TP/UDP integration test using two independent logical peers bound to opposite localhost UDP ports.

Current coverage includes:

- public `spw_port_*` API only;
- VSPW-TP framing through the actual UDP backend;
- packet fragmentation/reassembly;
- a 5 KiB EEP packet using 512-byte transport fragments;
- insufficient receive-capacity retry without consuming the completed packet;
- reverse-direction packet transfer;
- time-code round trip.

A future stronger-isolation harness may run the same semantics across separate processes, network namespaces, containers, or physical hosts:

```text
node A / libspwkit <---- IP network ----> node B / libspwkit
          \                                  /
           +----------- verifier -----------+
```

Future coverage should add transport loss/reordering, ACK/retransmission, peer restart/liveness, network interruption, configurable latency/rate, and deterministic fault injection as those v0.2 features are implemented.

Container startup order, socket bind success, or process creation alone must never be treated as proof that a virtual SpaceWire peer is operational.
