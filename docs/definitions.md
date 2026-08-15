# Definitions and terminology

This document defines the terminology used by SpWKit. Where a term comes from ECSS, the ECSS meaning takes precedence over informal software terminology.

Primary reference: **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks (15 May 2019)**.

Official standard page: <https://ecss.nl/standard/ecss-e-st-50-12c-rev-1-spacewire-links-nodes-routers-and-networks-15-may-2019/>

## SpaceWire

SpaceWire is a spacecraft onboard data-handling technology defining full-duplex, point-to-point serial communication links together with the logical mechanisms required to initialize links, control flow, transfer packets, and build networks from nodes and routing switches.

The standard spans several protocol levels, including physical, signal, character, exchange, packet, and network behaviour.

SpaceWire does **not** define the application contents of a packet. Upper-layer protocols define how payload data is interpreted.

## SpaceWire link

A SpaceWire link is a full-duplex point-to-point connection between two SpaceWire interfaces.

Each direction is independent. The software abstraction should therefore avoid artificial server/client ownership of the link.

## SpaceWire interface

An interface contains a transmitter and receiver associated with a SpaceWire link.

In SpWKit, a software `Port` is the closest portable abstraction to the application-visible behaviour of such an interface.

## SpaceWire port

A SpaceWire port is the endpoint through which a node connects to a SpaceWire link.

SpWKit uses the term **port** for the public software abstraction regardless of whether the implementation is:

- virtual;
- a Linux device;
- FPGA backed;
- bare metal;
- RTOS based;
- provided by a vendor SDK.

## Node

A node is an endpoint connected to a SpaceWire network. A node may produce, consume, or process SpaceWire packets.

A direct link between two nodes is already a SpaceWire network in ECSS terminology.

## SpaceWire network

A SpaceWire network comprises two or more nodes connected using one or more links and zero or more routing switches.

SpWKit therefore treats a simple two-port virtual link as a valid minimal network topology rather than as a client/server arrangement.

## Routing switch / router

A SpaceWire router is a routing switch. It forwards packets from input ports to output ports according to destination addressing and routing behaviour.

A Linux Ethernet bridge is not a SpaceWire router and must not be used as a semantic substitute in a conformance-oriented simulator.

## Data character

A data character carries one byte of packet data at the SpaceWire character level.

Character-level encoding is normally implemented by the codec and is below the default SpWKit application abstraction.

## Control character

Control characters are used by the SpaceWire link protocol for link control and packet termination rather than application payload data.

Relevant examples include flow-control and packet-termination behaviour.

## NULL

NULL control codes are used to keep an initialized SpaceWire link active and participate in link startup behaviour.

A packet-level simulator does not need to physically emit every NULL event unless character/link behavioural simulation is enabled.

## FCT

A Flow Control Token grants additional receive-buffer credit across the link.

Default packet simulation may model the effect of finite credit without serializing each FCT as a simulator transport message. Higher-fidelity behavioural simulation may expose credit transitions explicitly.

## EOP

**End Of Packet** terminates a normally completed SpaceWire packet.

SpWKit preserves EOP as packet metadata and does not infer it from a simulator/UDP frame boundary.

## EEP

**Error End of Packet** terminates a packet that ended in error.

EOP and EEP are semantically different and remain distinguishable across all supporting backends.

## Time code

SpaceWire supports time-code distribution as a control function across a network.

SpWKit treats time codes separately from normal packet payloads. Distributed VSPW-TP transport carries time-code events using a dedicated message type rather than embedding them in DATA payloads.

## Link state

SpaceWire defines link initialization, operational, and error-recovery behaviour through an exchange-level state machine.

SpWKit exposes a portable set of ECSS-oriented state names and each backend maps its observable state into that vocabulary. A software backend is not required to invent transient hardware states it cannot meaningfully observe.

## LinkStart

`LinkStart` is a management parameter that causes an enabled SpaceWire port to attempt link startup.

The public API exposes `spw_port_start()` as the portable management operation and documents backend-specific observable state transitions separately.

## RMAP

**Remote Memory Access Protocol (RMAP)** is defined by ECSS-E-ST-50-52C and operates over SpaceWire. It supports remote read/write operations and can be used for node configuration and data transfer.

RMAP is an upper-layer protocol and should remain modular rather than being hard-wired into the core link API.

Official reference: <https://ecss.nl/standard/ecss-e-st-50-52c-spacewire-remote-memory-access-protocol-5-february-2010/>

## CCSDS packet transfer over SpaceWire

ECSS-E-ST-50-53C defines transfer of CCSDS packets over SpaceWire by encapsulating a CCSDS packet in a SpaceWire packet for network transfer and extracting it at the destination.

This is an upper-layer service and is not part of the raw link abstraction.

Official reference: <https://ecss.nl/standard/ecss-e-st-50-53c-spacewire-ccsds-packet-transfer-protocol-5-february-2010/>

## Physical SpaceWire

In SpWKit documentation, **physical SpaceWire** means an implementation that ultimately drives a real SpaceWire electrical interface, typically through FPGA, ASIC, or dedicated controller hardware.

## Virtual SpaceWire

**Virtual SpaceWire** is SpWKit's software model of application-visible SpaceWire behaviour.

A virtual port preserves packet/link semantics while intentionally abstracting electrical implementation details.

Current implementations include the process-local simulator and the distributed VSPW-TP/UDP backend. Future Linux virtual-device naming is expected to use forms such as:

```text
/dev/vspw0
```

## vspw

`vspw` is shorthand used for virtual SpaceWire components.

Examples/planned names include:

```text
vspwd       virtual SpaceWire daemon/service (planned)
vspw0       virtual SpaceWire port 0/device name (planned)
```

## VSPW-TP

**VSPW-TP** is SpWKit's versioned **Virtual SpaceWire Transport Protocol** used internally by distributed virtual backends.

VSPW-TP is not a SpaceWire upper-layer protocol and is not application-facing. It frames simulator/backend events for transport over UDP while preserving the logical SpaceWire packet boundary, EOP/EEP terminator, time-code identity and virtual `link_id`.

Current v1 framing uses a fixed network-order header and supports DATA, TIME_CODE and reserved control/liveness/acknowledgement message types. DATA may be fragmented for transport; fragments are reassembled before being exposed through `spw_port_receive()`.

## UDP backend

`SPW_BACKEND_UDP` is the current distributed virtual backend on supported POSIX hosts.

UDP is only the carrier. Datagram loss, ordering, MTU, IP addressing and timing are transport concerns and must not silently redefine SpaceWire semantics.

## spw

`spw` is used for generic or physical SpaceWire components when ambiguity is low.

Examples/planned names include:

```text
spw0        physical/generic SpaceWire port
spwctl      management utility
spwmon      monitoring utility
```

## Backend

A backend is the implementation-specific layer that translates the portable SpWKit API into a particular transport or hardware mechanism.

Implemented examples include loopback, the process-local simulator and VSPW-TP/UDP. Future examples include Linux character devices, AXI/DMA and vendor SDKs.

## Transport

A transport is the mechanism used internally by a virtual backend to move simulation events or packet fragments between virtual ports.

UDP, raw Ethernet, Unix sockets and shared memory are transports. They are **not** SpaceWire semantics and remain below the virtual-port contract.
