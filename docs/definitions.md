# Definitions and terminology

This document defines terminology used by SpWKit. Where a term comes from ECSS, the ECSS meaning takes precedence over informal software terminology.

Primary reference: **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks (15 May 2019)**.

Official standard page: <https://ecss.nl/standard/ecss-e-st-50-12c-rev-1-spacewire-links-nodes-routers-and-networks-15-may-2019/>

## SpaceWire

SpaceWire is a spacecraft onboard data-handling technology defining full-duplex point-to-point serial links plus logical mechanisms for startup, flow control, packet transfer, time distribution and networks built from nodes/routers.

The standard spans physical, signal, character, exchange, packet and network behavior. SpWKit's default software abstraction intentionally sits above the electrical/Data-Strobe implementation level.

## SpaceWire link

A full-duplex point-to-point connection between two SpaceWire interfaces. The two directions are independent; SpWKit therefore avoids imposing server/client semantics on a link.

## SpaceWire interface / port

A SpaceWire interface contains a transmitter and receiver associated with a link. SpWKit's `spw_port_t` is the portable application-facing abstraction for the software-visible behavior of such an endpoint.

A SpWKit port may be implemented by:

- loopback/reference software;
- the process-local simulator;
- VSPW-TP/UDP;
- the Linux DEVICE/VSPD stack;
- a portable driver callback implementation;
- future vendor/FPGA/physical hardware.

## Node and network

A node is an endpoint connected to a SpaceWire network. A direct link between two nodes is already a minimal SpaceWire network. A network may additionally contain routing switches.

A Linux Ethernet bridge used to carry VSPW-TP is **not** a SpaceWire router and is never presented as such.

## Data character and control character

A data character carries one byte of packet data at the SpaceWire character level. Control characters support link operation and packet termination.

Character encoding, NULL/FCT behavior, parity and Data-Strobe timing are below the normal packet-level SpWKit software abstraction unless a future higher-fidelity model explicitly implements them.

## NULL

NULL control codes keep an initialized link active and participate in link startup. A packet-level software simulator does not need to serialize every NULL event unless character/link-exchange fidelity is explicitly enabled.

## FCT

A Flow Control Token grants receive-buffer credit. Packet-level software may model bounded flow-control effects without representing every FCT as an application-visible event.

## EOP / EEP

**EOP** (End Of Packet) terminates a normally completed SpaceWire packet.

**EEP** (Error End of Packet) terminates a packet that ended in error.

SpWKit preserves EOP/EEP as packet metadata. They are not inferred from UDP datagram or Unix-socket record boundaries.

## Time code

SpaceWire time codes are control events for network time distribution. SpWKit exposes them separately from DATA payloads. VSPW-TP, VSPD and CUSE presentation therefore carry time codes as dedicated event/record types rather than inventing DATA bytes.

## Link state

SpWKit exposes a portable SpaceWire-oriented state vocabulary through `spw_link_state_t`. Each backend maps what it can meaningfully observe into those states; a software backend does not fabricate hardware-only transient phases solely to exercise every enum value.

`spw_port_start()` is the portable management operation corresponding to requesting link startup.

## RMAP

**Remote Memory Access Protocol (RMAP)** is defined by ECSS-E-ST-50-52C and operates above SpaceWire. It is an upper-layer protocol and should remain modular rather than being hard-wired into the core port API.

Official reference: <https://ecss.nl/standard/ecss-e-st-50-52c-spacewire-remote-memory-access-protocol-5-february-2010/>

## CCSDS packet transfer over SpaceWire

ECSS-E-ST-50-53C defines transfer of CCSDS packets over SpaceWire. It is an upper-layer service, not part of the raw link abstraction.

SpWKit's CCSDSPack integration demonstrates the intended layering: CCSDSPack serializes/parses CCSDS/PUS bytes while SpWKit transports those bytes as a SpaceWire packet with EOP/EEP kept separately.

Official reference: <https://ecss.nl/standard/ecss-e-st-50-53c-spacewire-ccsds-packet-transfer-protocol-5-february-2010/>

## Physical SpaceWire

In this repository, **physical SpaceWire** means an implementation that ultimately drives a real SpaceWire electrical interface through dedicated controller, FPGA, ASIC or equivalent hardware.

No current hosted simulator/device result is described as physical SpaceWire evidence.

## Virtual SpaceWire

**Virtual SpaceWire** is SpWKit's software model of application-visible SpaceWire behavior. Current virtual paths include:

- the process-local simulator;
- distributed VSPW-TP/UDP;
- Linux `SPW_BACKEND_DEVICE` through `vspwd`;
- optional CUSE presentation such as `/dev/vspw0` through `spwcuse`.

`/dev/vspwX` is therefore a shipped v0.5 presentation, not future naming.

## `vspw`

`vspw` is shorthand used for virtual SpaceWire components, for example:

```text
vspwd       virtual SpaceWire daemon/service
/dev/vspw0  CUSE-presented virtual SpaceWire device
```

## VSPW-TP

**VSPW-TP** is SpWKit's versioned **Virtual SpaceWire Transport Protocol** for distributed virtual links. It is internal transport framing, not an application-facing SpaceWire upper-layer protocol.

VSPW-TP v1 carries DATA, TIME_CODE, liveness/control and acknowledgement information over datagram transport while preserving logical packet boundaries, EOP/EEP, `link_id` and sender session identity. DATA may be fragmented/reassembled internally before one packet reaches `spw_port_receive()`.

## UDP backend

`SPW_BACKEND_UDP` is the hosted distributed virtual backend. Its runtime uses POSIX sockets on supported Unix-like hosts and native Winsock on Windows.

UDP is only the carrier. Datagram loss, ordering, MTU, IP addressing and timing must not silently redefine SpaceWire semantics.

## VSPD

**VSPD** is the private local protocol between Linux `SPW_BACKEND_DEVICE` and `vspwd`. It is distinct from VSPW-TP and not an application API.

## `vspwd`

`vspwd` is the Linux userspace virtual SpaceWire service. It owns a bounded virtual topology, accepts DEVICE/VSPD attachments, provides management/monitoring state, and can bridge a topology-owned port to VSPW-TP/UDP.

## `spwcuse`

`spwcuse` is the optional Linux v0.5 CUSE presenter. It attaches to a `vspwd` port through the ordinary DEVICE backend and presents packet-oriented records through `/dev/vspwX`. It is outside `libspwkit` and keeps libfuse types out of the public ABI.

## `spwctl` / `spwmon`

`spwctl` is a non-owning management utility for `vspwd`. `spwmon` is a passive observer that subscribes to bounded daemon snapshots without attaching to or consuming a SpaceWire port.

## Driver backend

`SPW_BACKEND_DRIVER` is the v0.6 portable software boundary that delegates operations to a versioned `spw_driver_ops_t` callback table and caller-owned context. A driver may represent a host reference model, MCU/RTOS device, vendor SDK or future FPGA controller.

Driver callbacks may know about DMA/cache/interrupt/native mechanisms. Those details do not become application SpaceWire types.

## Transport

A transport is an internal mechanism used to carry backend/protocol events. UDP/IP, Unix sockets, shared memory and future network stacks are transports. They are not SpaceWire semantics.
