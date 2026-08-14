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

SpWKit must preserve EOP as metadata and not merely infer it from a transport frame boundary.

## EEP

**Error End of Packet** terminates a packet that ended in error.

EOP and EEP are semantically different and must remain distinguishable across all backends.

## Time code

SpaceWire supports time-code distribution as a control function across a network.

SpWKit treats time codes separately from normal packet payloads.

## Link state

SpaceWire defines link initialization, operational, and error-recovery behaviour through an exchange-level state machine.

SpWKit may expose a simplified portable link-state API, but any mapping to ECSS state names must be explicitly documented and verified.

## LinkStart

`LinkStart` is a management parameter that causes an enabled SpaceWire port to attempt link startup.

The public API may expose equivalent operations such as `start()` while retaining a documented mapping to the underlying ECSS management behaviour.

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

A virtual port is expected to preserve packet and link semantics while intentionally abstracting electrical implementation details.

Example Linux naming:

```text
/dev/vspw0
```

## vspw

`vspw` is the shorthand used for virtual SpaceWire components.

Examples:

```text
vspwd       virtual SpaceWire daemon/service
vspw0       virtual SpaceWire port 0
```

## spw

`spw` is used for generic or physical SpaceWire components when ambiguity is low.

Examples:

```text
spw0        physical/generic SpaceWire port
spwctl      management utility
spwmon      monitoring utility
```

## Backend

A backend is the implementation-specific layer that translates the portable SpWKit API into a particular transport or hardware mechanism.

Examples include virtual Ethernet, Linux character devices, AXI/DMA, and vendor SDKs.

## Transport

A transport is the mechanism used internally by a virtual backend to move simulation events or packet fragments between virtual ports.

UDP, raw Ethernet, Unix sockets, and shared memory are transports. They are **not** SpaceWire semantics and must remain hidden below the virtual-port contract.
