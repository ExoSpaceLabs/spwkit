# Ethernet / distributed backend

This directory contains the distributed virtual SpaceWire transport implementation.

Stable v0.7.0 includes:

- VSPW-TP v1 framing/validation;
- IPv4 UDP runtimes selected as `SPW_BACKEND_UDP` on POSIX hosts and native Winsock on Windows;
- bounded packet fragmentation/reassembly;
- EOP/EEP preservation;
- time-code transport;
- ACK/retransmission, duplicate suppression and sender-session restart recovery;
- configurable virtual rate/latency and deterministic transport/SpaceWire faults;
- active process, namespace and installed-package D2D coverage;
- installed-package CCSDSPack v2.0.0 PUS-C integration, including the isolated two-node Docker Compose topology.

The public UDP configuration and VSPW-TP wire contract are identical across POSIX and Windows. Winsock startup, `SOCKET` handles, readiness polling, monotonic timing and error translation stay inside the private Win32 compatibility layer; no Winsock type is exposed through installed SpWKit headers.

```mermaid
flowchart LR
    A[SpWKit peer A] --> UA[SPW_BACKEND_UDP]
    UA <-->|VSPW-TP / IPv4 UDP| UB[SPW_BACKEND_UDP]
    UB --> B[SpWKit peer B]
```

The default UDP fragment payload is 1200 bytes. The current backend advertises a 1 MiB logical packet/reassembly limit even though the protocol framing can represent larger logical payloads.

Ethernet/IP is only the carrier. SpaceWire packet termination, packet boundaries, link semantics and time codes remain defined by SpWKit rather than inherited from UDP datagrams.

Post-v0.7 `develop` completes that separation: a carrier-independent VSPW engine binds to UDP, deterministic in-memory transport, or the public raw-Ethernet callback provider without changing the application-facing API.


## Raw Ethernet on develop

`SPW_BACKEND_RAW_ETHERNET` wraps VSPW-TP directly in Ethernet-II frames and
delegates actual frame I/O plus timing to versioned callbacks. The development
outer framing is version 2.0 and includes an explicit VSPW-frame length to
ignore Ethernet minimum-frame padding safely.
