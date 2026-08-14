# SpWKit

**SpaceWire Development & Integration Toolkit**

SpWKit is an open-source software project intended to provide a stable, implementation-independent API for working with SpaceWire links and packets across development, simulation, embedded, and hardware-backed environments.

The central idea is deliberately simple:

```text
Application
    |
    v
+-----------------------------+
|          SpWKit API         |
| Port / Packet / Link / Time |
+--------------+--------------+
               |
               | backend abstraction
               |
       +-------+---------+--------------+----------------+
       |                 |              |                |
       v                 v              v                v
  /dev/vspw0        /dev/spw0      Bare metal       Vendor API
  simulator         Linux driver   / RTOS backend   backend
       |                 |              |
       v                 v              v
 Virtual link        AXI / DMA       MMIO / DMA
 over Ethernet           |              |
                         +-------> FPGA SpaceWire IP
                                      |
                                      v
                               Physical SpaceWire
```

An application should be able to develop against a virtual SpaceWire port and later move to physical hardware without rewriting its SpaceWire-facing logic.

```text
Development
-----------
Application -> SpWKit -> vspw0 -> virtual link -> vspw0 -> SpWKit -> Application

Deployment
----------
Application -> SpWKit -> spw0 -> driver -> AXI/DMA -> FPGA -> SpaceWire
```

## Why SpWKit?

SpaceWire already has mature standards, commercial FPGA IP, hardware interfaces, vendor SDKs, HDL test environments, and network simulation tools. What is much less uniform is the software-facing integration layer between applications and those implementations.

Existing solutions commonly fall into one or more of these categories:

- FPGA/ASIC SpaceWire codec or router IP;
- hardware-vendor-specific C/C++ APIs and drivers;
- HDL/SystemC verification environments;
- network and mission simulation tools;
- protocol-specific implementations such as RMAP.

SpWKit is intended to address a different problem: **provide one portable software contract from virtual development to deployed hardware**.

Its long-term goals are therefore not to replace electrical or RTL simulation, nor to replace proven commercial SpaceWire IP. Instead, SpWKit aims to make the layers above the codec portable, testable, and reusable.

## Virtual SpaceWire

SpWKit's simulator is intended to expose virtual SpaceWire ports such as:

```text
/dev/vspw0
/dev/vspw1
```

A virtual port should model the software-observable semantics of SpaceWire rather than attempt analogue or cycle-accurate electrical simulation.

The target philosophy is similar to Linux `vcan`: preserve the application contract and protocol-visible behaviour while abstracting the physical implementation.

A virtual link may be local:

```text
+---------------+                         +---------------+
| Application A |                         | Application B |
+-------+-------+                         +-------+-------+
        |                                         |
     vspw0 <=========== virtual link ==========> vspw1
```

or distributed over real Ethernet:

```text
Linux / Embedded Node A                         Node B
+----------------------+                +----------------------+
| Application          |                | Application          |
| SpWKit               |                | SpWKit               |
| vspw0                |<----Ethernet-->| vspw0                |
+----------------------+                +----------------------+
```

This enables distributed simulation, hardware-in-the-loop development, containerized test networks, and embedded-target integration before physical SpaceWire hardware is available.

## Simulation fidelity

SpWKit separates simulation fidelity from physical-layer verification.

### Packet mode

The default simulator should model:

- bidirectional packet transfer;
- arbitrary packet sizes;
- packet boundaries;
- EOP and EEP termination;
- link start, stop, reset, and state;
- time codes;
- bounded queues;
- configurable data rate and latency;
- statistics;
- deterministic fault injection.

### Behavioural link mode

An optional higher-fidelity mode may additionally model:

- SpaceWire link-state transitions;
- finite receive credit;
- flow-control effects;
- disconnect detection;
- queue pressure and congestion;
- character-level error injection.

### Out of scope for the software simulator

Electrical and RTL correctness remain separate verification concerns:

- LVDS electrical behaviour;
- Data-Strobe waveform accuracy;
- analogue cable effects;
- clock-domain crossing correctness;
- cycle-accurate codec timing.

Those belong in HDL simulation, FPGA verification, and physical interoperability testing.

## Target platforms

The intended backend matrix includes:

| Environment | Virtual link | Physical SpaceWire |
|---|---|---|
| Linux | `/dev/vspwX`, local/Ethernet | `/dev/spwX`, vendor APIs |
| Bare metal | in-process/Ethernet virtual port | MMIO/AXI/DMA backend |
| HardRT | virtual Ethernet backend | hardware backend |
| FreeRTOS | planned | planned |
| RTEMS | planned | planned |
| Vendor hardware | n/a | adapter backend |

The portable core should not require POSIX, heap allocation, exceptions, RTTI, threads, or a filesystem.

## Standards scope

The primary normative reference for SpaceWire behaviour is:

- **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks (15 May 2019)**

Related protocol standards include:

- **ECSS-E-ST-50-51C — SpaceWire protocol identification**;
- **ECSS-E-ST-50-52C — SpaceWire Remote Memory Access Protocol (RMAP)**;
- **ECSS-E-ST-50-53C — SpaceWire CCSDS packet transfer protocol**.

SpWKit currently targets these standards as design references. **The project must not claim ECSS conformance or certification until the implemented scope is backed by requirements traceability and verification evidence.** See [docs/compliance.md](docs/compliance.md).

Official ECSS standards index: <https://ecss.nl/standards/active-standards/engineering/>

## Planned architecture

```text
spwkit/
├── include/spwkit/        Public portable API
├── src/core/              OS-independent implementation
├── src/backends/          Platform and transport backends
│   ├── linux/
│   ├── virtual/
│   ├── ethernet/
│   ├── baremetal/
│   └── hardrt/
├── simulator/             Virtual SpaceWire implementation
│   └── vspwd/
├── tools/                 Diagnostics and management
│   ├── spwctl/
│   └── spwmon/
├── examples/
├── tests/
└── docs/
```

## API direction

The public interface will be built around a SpaceWire **port**, not around sockets, Ethernet, AXI registers, or a particular vendor device.

Conceptually:

```cpp
auto port = spwkit::open(config);

port.start();
port.send(packet);
auto received = port.receive();
```

Backends provide the implementation:

```text
Port
├── VirtualBackend
├── LinuxDeviceBackend
├── EthernetBackend
├── BareMetalBackend
├── HardRTBackend
└── VendorBackend
```

A C ABI is planned as the portability baseline, with an idiomatic C++ wrapper above it.

## Project status

**Pre-alpha / architecture bootstrap.**

The repository is currently defining interfaces, terminology, compliance boundaries, simulator semantics, and the initial portable core. APIs are not stable yet.

See [docs/roadmap.md](docs/roadmap.md).

## Documentation

- [Architecture](docs/architecture.md)
- [Definitions and terminology](docs/definitions.md)
- [ECSS scope and compliance policy](docs/compliance.md)
- [Current ecosystem and project positioning](docs/landscape.md)
- [Roadmap](docs/roadmap.md)

## Licensing

SpWKit software is licensed under the **Apache License 2.0**.

The open-source software project is intentionally separable from any future commercial SpaceWire FPGA/ASIC IP. A hardware implementation may implement the same SpWKit-facing contract without being part of this repository.

See [LICENSE](LICENSE) and [NOTICE](NOTICE).

## Contributing

SpWKit is intended as community infrastructure rather than a single-board demo. Contributions should preserve backend independence and avoid leaking simulator-, OS-, FPGA-, or vendor-specific assumptions into the portable API.

See [CONTRIBUTING.md](CONTRIBUTING.md).
