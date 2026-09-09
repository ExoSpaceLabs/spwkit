# Ecosystem and project positioning

SpaceWire is not an underserved protocol in the sense of having no tooling. There are mature standards, commercial IP cores, hardware interfaces, vendor SDKs, verification environments, and network simulation products.

SpWKit is intended to occupy a narrower integration gap.

## Existing solution categories

### FPGA and ASIC IP

Commercial and open implementations exist for SpaceWire codecs, routers, DMA interfaces, and related functions.

These solutions generally focus on synthesizable hardware and electrical/protocol implementation rather than providing a portable application API across simulator and hardware deployments.

### Hardware vendor SDKs

Commercial SpaceWire interfaces commonly ship with device-specific drivers and C/C++ APIs.

These are appropriate for their hardware but naturally couple applications to a particular device family, operating system, or vendor software model.

### HDL and SystemC verification

HDL/SystemC environments can model codec-level behaviour and validate link implementation details.

These are the correct tools for character, exchange, Data-Strobe, timing, and RTL verification. They are not intended to provide a lightweight virtual device API for ordinary application development.

### Network and mission simulation

SpaceWire network simulators can model routing, topology, congestion, and mission traffic.

These are useful for architecture studies but are typically distinct from the runtime API used by embedded or Linux applications deployed on actual hardware.

### Protocol libraries

Libraries exist for higher-layer SpaceWire protocols such as RMAP and for interacting with specific SpaceWire equipment.

These do not necessarily provide a shared hardware-independent port abstraction or a virtual endpoint compatible with physical deployment.

## SpWKit's intended niche

SpWKit aims to provide continuity across these stages:

```text
unit testing
     |
local virtual link
     |
distributed Ethernet simulation
     |
embedded/MCU provider validation
     |
hardware-in-the-loop
     |
physical SpaceWire provider
```

The same application-facing API should remain usable across the path.

```text
                         +------------------+
                         |   Application    |
                         +--------+---------+
                                  |
                               SpWKit
                                  |
             +--------------------+--------------------+
             |                    |                    |
        simulator          Linux device path      DRIVER provider
      current/stable        current/stable      MCU qualified; FPGA future
```

## Primary differentiators

### Stable port abstraction

Applications operate on SpaceWire ports and packets rather than sockets, UDP endpoints, AXI registers, or vendor handles.

### Virtual device semantics

The simulator exposes virtual SpaceWire endpoints with behaviour analogous in purpose to Linux `vcan`: useful software-level fidelity without pretending to be an analogue/electrical simulator.

### Distributed simulation

Two virtual ports may be connected over real Ethernet, allowing separate hosts, containers, or embedded boards to participate in one virtual SpaceWire environment.

### Embedded-first portability

The core supports no-heap/freestanding builds and a portable DRIVER provider boundary. Current evidence includes HardRT integration plus physical STM32H755 DMA/cache qualification; additional RTOS/platform providers can be added without changing the common application API.

### Simulator-to-hardware continuity

A deployment should be able to change from a virtual backend to FPGA/vendor hardware with minimal changes above the backend configuration boundary.

### Open integration layer

The software integration layer, simulator, API, and tooling can remain open source while hardware implementations remain independent. This allows open, proprietary, commercial, or vendor FPGA/ASIC cores to implement the same software-facing contract.

## Non-goals

SpWKit does not aim to replace:

- commercial SpaceWire EGSE;
- electrical compliance equipment;
- HDL simulators;
- FPGA synthesis tools;
- mission-level network simulators;
- proven SpaceWire codec/router IP;
- the ECSS standards themselves.

Generic SpaceWire router implementation is also outside the core SpWKit scope. A SpWKit endpoint may communicate through an external router or a provider that handles routing information; the toolkit does not need to become that router.

The project is successful if those technologies become easier to integrate behind one portable software API.

## Comparison model

| Capability | Vendor SDK | HDL simulator | Network simulator | SpWKit target |
|---|---:|---:|---:|---:|
| Application packet API | often | testbench-oriented | model-specific | yes |
| Hardware independent | usually no | n/a | often | yes |
| Virtual local port | uncommon | not as device API | model-specific | yes |
| Virtual link over Ethernet | product-specific | no | sometimes | yes |
| Bare-metal target | device-specific | n/a | no | yes |
| RTOS target | device-specific | n/a | no | yes |
| Physical FPGA provider | yes | DUT-oriented | no | future provider below stable DRIVER contract |
| Electrical fidelity | hardware | potentially signal-level | no | no |
| Generic router/network simulation | hardware dependent | possible | yes | out of core scope |

This table describes solution categories rather than every product. Individual products may provide capabilities beyond the general patterns shown here.

## Positioning principle

SpWKit should avoid marketing itself as a replacement for existing SpaceWire technology. Its value proposition is interoperability between development environments and implementations:

> Develop against a virtual SpaceWire port. Deploy against a real one.
