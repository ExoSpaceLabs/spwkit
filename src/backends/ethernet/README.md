# Ethernet / distributed backend

This directory contains the distributed virtual SpaceWire transport implementation.

Current `main` includes:

- VSPW-TP v1 framing/validation;
- IPv4 UDP runtimes selected as `SPW_BACKEND_UDP` on POSIX hosts and native Winsock on Windows;
- bounded packet fragmentation/reassembly;
- EOP/EEP preservation;
- time-code transport;
- receive timeout/statistics handling;
- active D2D CI coverage.

The public UDP configuration and VSPW-TP wire contract are identical across POSIX and Windows. Winsock startup, `SOCKET` handles, readiness polling, monotonic timing and error translation stay inside the private Win32 transport compatibility layer; no Winsock type is exposed through installed SpWKit headers.

The default UDP fragment payload is 1200 bytes. The current backend advertises a 1 MiB logical packet/reassembly limit even though the protocol framing format can represent larger logical payloads.

Ethernet/IP is only the carrier. SpaceWire packet termination, packet boundaries, link semantics and time codes remain defined by SpWKit rather than inherited from UDP datagrams.

v0.2.0 also includes logical-message ACK/retransmission, duplicate suppression, session/KEEPALIVE liveness and restart recovery, bounded arbitrary-order reassembly, configurable virtual rate/latency, deterministic transport faults, explicit SpaceWire EEP injection, shared contract coverage, process/network-namespace integration and capture tooling.

The VSPW-TP codec is deliberately independent of socket APIs so future embedded/lwIP transports can reuse the same framing contract.
