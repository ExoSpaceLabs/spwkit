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
 Local simulator    /dev/spw0      Bare metal       Vendor API
 backend            Linux driver   / RTOS backend   backend
       |                 |              |
       v                 v              v
 Virtual peer        AXI / DMA       MMIO / DMA
 link                    |              |
                         +-------> FPGA SpaceWire IP
                                      |
                                      v
                               Physical SpaceWire
```

An application should be able to develop against a virtual SpaceWire port and later move to physical hardware without rewriting its SpaceWire-facing logic.

```text
Development today
-----------------
Application -> SpWKit -> simulator endpoint A <=> endpoint B -> SpWKit -> Application

Future deployment
-----------------
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

The first v0.1 virtual backend is now implemented as a process-local two-peer SpaceWire link. Applications select it through the normal `spw_port_config_t` API:

```text
+---------------+                         +---------------+
| Application A |                         | Application B |
+-------+-------+                         +-------+-------+
        |                                         |
        v                                         v
+-------+-------+                         +-------+-------+
| libspwkit     |                         | libspwkit     |
| endpoint A    |<==== virtual link ====> | endpoint B    |
+---------------+        link_id          +---------------+
```

Endpoints A and B are equal SpaceWire peers. They are deterministic pairing labels, not client/server roles.

The current local simulator supports:

- bidirectional and concurrent packet transfer;
- complete packet boundaries;
- EOP and EEP termination;
- start, stop, reset, disconnect, and recovery behavior;
- time codes;
- bounded queues;
- immediate, finite, and infinite waits;
- statistics;
- deterministic peer disappearance/reconnection behavior.

Future virtual transports may expose Linux devices such as `/dev/vspw0` or connect peers over real Ethernet:

```text
Linux / Embedded Node A                         Node B
+----------------------+                +----------------------+
| Application          |                | Application          |
| SpWKit               |                | SpWKit               |
| virtual backend      |<----Ethernet-->| virtual backend      |
+----------------------+                +----------------------+
```

That later distributed layer is intended to enable containerized device-to-device tests, hardware-in-the-loop development, and embedded-target integration while preserving the same application-facing API.

## Simulation fidelity

SpWKit separates simulation fidelity from physical-layer verification.

### Packet/link mode

The implemented v0.1 local simulator models:

- bidirectional/full-duplex packet transfer;
- packet boundaries;
- EOP and EEP termination;
- software-visible link start, stop, reset, connect, disconnect, and state;
- time codes;
- bounded packet and time-code queues;
- statistics;
- deterministic disconnect/recovery.

Planned simulator extensions include configurable data rate and latency, explicit fault injection, and higher-fidelity flow-control behavior.

### Behavioural link mode

A higher-fidelity mode may additionally model:

- more detailed SpaceWire link-state timing;
- finite receive credit;
- flow-control effects;
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
| Linux | process-local now; `/dev/vspwX`/Ethernet planned | `/dev/spwX`, vendor APIs planned |
| Bare metal | in-process/Ethernet planned | MMIO/AXI/DMA backend planned |
| HardRT | virtual Ethernet backend planned | hardware backend planned |
| FreeRTOS | planned | planned |
| RTEMS | planned | planned |
| Vendor hardware | n/a | adapter backend planned |

The long-term portable core should not require POSIX, heap allocation, exceptions, RTTI, threads, or a filesystem. The current host-side v0.1 implementation still uses dynamic allocation for public port objects; the static/no-heap path is tracked separately.

## Standards scope

The primary normative reference for SpaceWire behaviour is:

- **ECSS-E-ST-50-12C Rev.1 — SpaceWire — Links, nodes, routers and networks (15 May 2019)**

Related protocol standards include:

- **ECSS-E-ST-50-51C — SpaceWire protocol identification**;
- **ECSS-E-ST-50-52C — SpaceWire Remote Memory Access Protocol (RMAP)**;
- **ECSS-E-ST-50-53C — SpaceWire CCSDS packet transfer protocol**.

SpWKit currently targets these standards as design references. **The project must not claim ECSS conformance or certification until the implemented scope is backed by requirements traceability and verification evidence.** See [docs/compliance.md](docs/compliance.md).

Official ECSS standards index: <https://ecss.nl/standards/active-standards/engineering/>

## Architecture

```text
spwkit/
├── include/spwkit/        Public portable C API
├── src/core/              API dispatch and backend contract
├── src/backends/
│   ├── loopback/          Deterministic single-port reference backend
│   ├── virtual/           Working process-local paired simulator
│   ├── linux/             Planned
│   ├── ethernet/          Planned
│   ├── baremetal/         Planned
│   └── hardrt/            Planned
├── simulator/             Future simulator services such as vspwd
├── tools/                 Diagnostics and management
├── examples/
├── tests/
└── docs/
```

## API direction

The public interface is built around a SpaceWire **port**, not around sockets, Ethernet, AXI registers, or a particular vendor device.

The C ABI is the portability baseline:

```c
spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
spw_port_t* port = NULL;

spw_port_open(&config, &port);
spw_port_start(port);
spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE);
```

Backends implement the same contract beneath that API:

```text
Port
├── LoopbackBackend
├── SimulatorBackend
├── LinuxDeviceBackend     planned
├── EthernetBackend        planned
├── BareMetalBackend       planned
├── HardRTBackend          planned
└── VendorBackend          planned
```

An idiomatic C++ wrapper can be layered above the C ABI without changing backend implementations.

## Project status

**Pre-alpha / v0.1 implementation.**

The public C ABI, core packet/link types, backend abstraction, loopback backend, backend configuration model, and process-local paired simulator are implemented. APIs are not stable yet.

The v0.1 focus is now reusable contract tests, static/no-heap portability, public examples/documentation, and optional zero-copy ownership semantics. Physical FPGA/HIL validation is deliberately deferred until suitable hardware is available.

See [docs/roadmap.md](docs/roadmap.md).

## Documentation

- [Architecture](docs/architecture.md)
- [Public API contract](docs/api.md)
- [Core public types](docs/types.md)
- [Backend contract](docs/backend-contract.md)
- [Port/backend configuration](docs/configuration.md)
- [Local virtual simulator](docs/simulator.md)
- [Testing strategy](docs/testing.md)
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
