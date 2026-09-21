# Hardware provider acceptance

This document defines the public acceptance criteria for a physical SpaceWire provider implementing the SpWKit hardware-driver contract.

The criteria are intentionally independent from FPGA vendor, processor, bus, DMA engine, operating system, register map, descriptor format, or proprietary RTL. A provider may use PIO, DMA, PCIe, a vendor SDK, an RTOS driver, or another native mechanism as long as the observable SpWKit contract is preserved.

## Scope

Acceptance at this boundary proves that a physical provider behaves correctly through the public SpWKit API for the capabilities it advertises.

SpWKit owns and may claim the ECSS requirements that are applicable to, implemented by, and verified at its software/runtime boundary. The concrete provider owns the requirements implemented below that boundary. For the ExoSpaceLabs FPGA path, `spwkit-fpga` is responsible for its FPGA/controller/PHY/electrical conformance claim and evidence.

Provider acceptance through this document therefore does **not** by itself prove:

- SpaceWire electrical compliance of an arbitrary provider;
- provider-side ECSS requirements that have not been tested on the concrete hardware;
- radiation tolerance;
- timing closure or FPGA implementation quality;
- performance beyond the measured test configuration;
- correctness of proprietary implementation details hidden below `spw_driver_ops_t`.

Likewise, physical/provider delegation does not remove SpWKit's responsibility to claim and evidence the software requirements it can satisfy. See [`ecss-conformance.md`](ecss-conformance.md).

Evidence must state exactly which layer was exercised.

## Public API invariance

A hardware-backed application must continue to use the ordinary public API:

```text
application
    |
spw_port_* / spw_buffer_*
    |
SPW_BACKEND_DRIVER
    |
spw_driver_ops_t
    |
native physical provider
```

No physical provider is accepted if application code must depend on its register map, DMA descriptors, native buffer handles, interrupt objects, or FPGA implementation types.

## Build and ABI

The provider must demonstrate:

- a supported `SPW_DRIVER_OPS_VERSION` and configuration version;
- required callbacks for the advertised capability set;
- deterministic rejection of malformed or incompatible callback/configuration tables;
- no leakage of implementation-native types into common SpWKit signatures;
- bounded resource use on configurations that claim no-heap/freestanding support.

## Lifecycle and link state

Exercise at least:

1. open/configure;
2. start;
3. observable link state;
4. stop;
5. restart where supported;
6. reset;
7. close/release.

Native controller states may be richer than `spw_link_state_t`. The provider must map them into the public model consistently rather than exposing vendor-specific states to applications.

Reset must leave the provider in a deterministic state and invalidate application-visible ownership whose backing hardware state can no longer be trusted.

## Packet semantics

The provider must preserve complete SpaceWire packet semantics end to end.

Required evidence includes:

- bidirectional packet transfer;
- arbitrary payload bytes;
- explicit and distinct EOP and EEP termination;
- packet boundaries preserved despite internal FIFO, USB, DMA, bus, or descriptor fragmentation;
- no silent receive truncation;
- ordered delivery according to the provider's documented queue semantics.

### Undersized receive

If the next complete packet is larger than the caller buffer, copied receive must return `SPW_ERR_BUFFER_TOO_SMALL`, report the complete required packet length and terminator, and retain the packet for a later retry.

A provider that destructively consumes part or all of the packet on this path does not satisfy the public contract.

## Capabilities

`get_capabilities` must describe the combined behavior of the complete driver + hardware implementation.

A hardware feature must not be advertised merely because a register, HDL block, or vendor SDK function exists. The corresponding public semantics must be implemented and verified end to end.

Capability-gated features should be tested when advertised, including as applicable:

- time-code transmit/receive;
- statistics;
- readiness/wait support;
- EEP handling;
- link control;
- zero-copy/DMA ownership.

Unsupported optional capabilities must remain unadvertised.

## DMA and zero-copy acceptance

If zero-copy is advertised, all six ownership callbacks must be implemented and exercised:

- `acquire_tx_buffer`;
- `submit_tx_buffer`;
- `reclaim_tx_buffer`;
- `release_tx_buffer`;
- `acquire_rx_buffer`;
- `release_rx_buffer`.

Evidence must cover the ownership sequence:

```text
TX: driver free -> application -> device/DMA -> completed -> application/released
RX: device/DMA -> completed -> application -> driver free -> device/DMA
```

The provider must demonstrate:

- successful TX and RX ownership transfer;
- no hidden payload copy in a path advertised as zero-copy;
- failed ownership transfer does not steal the application's buffer;
- stale, foreign, direction-mismatched, or already-released handles are rejected;
- reset invalidates stale ownership deterministically;
- reclaim/release does not recycle a buffer while another party still owns it.

## Cache and coherency

On non-coherent targets, DMA acceptance must include the real cache/coherency path rather than a host-side no-op substitute.

Evidence should demonstrate the required ordering, for example:

```text
TX CPU write
 -> cache clean if required
 -> memory barrier
 -> device ownership

RX device completion
 -> memory barrier
 -> cache invalidate if required
 -> CPU ownership
```

The implementation may realize this through `sync_buffer`, platform DMA APIs, coherent memory, or another native mechanism. The public contract defines ownership and synchronization points, not CPU-specific cache instructions.

## Error and recovery behavior

A physical-provider test should exercise the failures that can be produced safely by the target, such as:

- unavailable/not-started link;
- timeout;
- reset while idle;
- disconnect/reconnect for a physical SpaceWire link;
- provider or DMA queue failure where injectable;
- EEP receive;
- invalid/stale ownership operations.

Native errors must map to useful portable results, link state, and statistics without corrupting subsequent operation.

## Physical SpaceWire HIL

When the target contains an actual SpaceWire PHY/link implementation, physical HIL should use an independent endpoint whenever possible.

Record at minimum:

- provider hardware/platform and revision;
- firmware/driver version;
- FPGA bitstream/core revision where applicable;
- SpWKit commit/version;
- peer hardware and software version;
- configured and measured link rate;
- cable/configuration relevant to the result.

Exercise:

- link initialization;
- repeated bidirectional DATA packets;
- EOP and EEP;
- advertised time-code behavior;
- disconnect/reconnect and restart;
- sustained traffic appropriate to the implementation;
- any advertised zero-copy/DMA path on the host side.

Passing these tests proves interoperability for the recorded configuration. Provider-side ECSS conformance may be claimed only for requirements explicitly mapped to and evidenced by that concrete provider/HIL campaign. It must not be generalized to untested electrical, timing or implementation requirements.

## ECSS composition

For a concrete hardware system, the conformance record is composed across the boundary:

```text
SpWKit software/runtime
    applicable software requirements
            +
physical provider / spwkit-fpga
    applicable controller/PHY/electrical requirements
            =
combined system evidence for the requirements actually covered
```

The provider should reference the SpWKit software-conformance record for software-owned requirements and maintain its own traceability for delegated requirements. Neither layer should duplicate or silently assume the other's evidence.

## Evidence levels

Use the following terminology so different tests are not quietly promoted into stronger claims:

| Evidence | What it proves |
|---|---|
| Reference-driver/host tests | Public callback and ownership semantics without physical hardware. |
| MCU DMA/cache test | Real processor DMA, ownership and coherency behavior; not a SpaceWire PHY test. |
| Controller/FPGA runtime test | Driver plus target hardware datapath behavior. |
| Physical SpaceWire HIL | Real link interoperability against another physical endpoint. |
| Requirement-level conformance campaign | Only the ECSS requirements explicitly mapped to and covered by that campaign. |

No earlier level should be described as proof of a later level.

## Acceptance record

A provider is accepted into a supported configuration only when its result records:

- the exact capability set tested;
- software/hardware revisions;
- pass/fail result;
- known limitations;
- applicable ECSS provider requirements claimed by the recorded evidence;
- performance claims only where measurements exist.

This keeps the public API portable while allowing precise software and hardware conformance claims without pretending every underlying implementation has identical resources or performance.