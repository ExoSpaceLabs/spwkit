# Ethernet / distributed backend

This directory contains the distributed virtual SpaceWire transport implementation.

Current `main` includes:

- VSPW-TP v1 framing/validation;
- a POSIX IPv4 UDP backend selected as `SPW_BACKEND_UDP`;
- bounded packet fragmentation/reassembly;
- EOP/EEP preservation;
- time-code transport;
- receive timeout/statistics handling;
- active D2D CI coverage.

The default UDP fragment payload is 1200 bytes. The current backend advertises a 1 MiB logical packet/reassembly limit even though the protocol framing format can represent larger logical payloads.

Ethernet/IP is only the carrier. SpaceWire packet termination, packet boundaries, link semantics and time codes remain defined by SpWKit rather than inherited from UDP datagrams.

Remaining v0.2 work includes ACK/retransmission, peer keepalive/disconnect detection, loss/reordering policy, configurable virtual rate/latency and deterministic fault injection.

The VSPW-TP codec is deliberately independent of socket/POSIX APIs so future embedded/lwIP transports can reuse the same framing contract.
