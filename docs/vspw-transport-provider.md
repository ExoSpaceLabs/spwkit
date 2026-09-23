# VSPW transport-provider boundary

## Objective

Issue #229 separates VSPW-TP protocol/session/reliability behavior from the
carrier implementation without changing the application-facing
`spw_port_*` contract.

The target dependency direction is:

```text
application
    -> SpWKit public API
        -> VSPW engine
            -> transport-provider contract
                -> UDP / raw Ethernet / platform provider
                    -> OS socket or board Ethernet driver
```

SpWKit must not depend directly on a board-support package, MCU SDK or RTOS.
A board-support package must not know about SpWKit.

## Current coupling audit

The v0.7 source already keeps the VSPW-TP wire codec in
`vspw_tp.c/.h`, but `udp_backend.c` still owns both carrier mechanics and
protocol-engine behavior.

Protocol/session behavior currently living in the UDP backend includes:

- session identity and remote-session rollover;
- VSPW sequence/message IDs;
- DATA/TIME_CODE/KEEPALIVE/ACK construction;
- ACK matching and retry state;
- duplicate suppression;
- packet fragmentation orchestration;
- fragment reassembly and delivery;
- EOP/EEP preservation;
- time-code queues;
- keepalive/liveness state;
- virtual timing and deterministic fault handling.

Carrier/platform behavior currently mixed into the same implementation
includes:

- socket creation/bind/address handling;
- peer identity represented as IP address/port;
- `sendto()` / `recvfrom()`;
- readiness polling and socket error translation;
- UDP datagram-size constraints;
- host monotonic clock access used by retry/liveness scheduling.

The extraction must move only true carrier mechanics below the provider
boundary. Reliability, fragmentation and SpaceWire-visible semantics remain in
the VSPW engine.

## Carrier contract requirements

The internal provider boundary is message/frame oriented. It must preserve one
carrier message per send/receive operation and must not pretend to be a byte
stream.

The contract needs to express:

- send one complete carrier message/frame;
- receive one complete carrier message/frame;
- returned source/peer identity where the carrier has multiple peers;
- maximum carrier payload/MTU;
- readiness/wait integration without forcing polling;
- carrier/link availability where meaningful;
- deterministic result/error translation;
- send-completion semantics.

Peer identity must be an opaque provider-owned value rather than an IP/port
type. This prevents VSPW session association from depending on socket
addressing and allows a raw-Ethernet provider to use MAC/link identity.

## Timing/runtime boundary

Timeout/retry/keepalive policy belongs to the VSPW engine, not to the carrier.
The engine may require a small platform/runtime service for monotonic time and
waiting/wakeup.

That runtime service is separate from the transport-provider contract unless a
wait operation is inherently carrier-specific.

This keeps the embedded composition valid:

```text
VSPW engine
    + platform clock/wait hooks
    + raw-Ethernet provider
        -> board Ethernet driver
            -> DMA / MAC / PHY
```

An RTOS may implement the clock, synchronization and IRQ-to-task wakeup hooks;
it does not become a networking layer and SpWKit does not take a hard
dependency on that RTOS.

## UDP provider mapping

The retained UDP provider owns:

- POSIX/Winsock socket lifecycle;
- bind/local endpoint configuration;
- peer IP/port configuration and conversion to opaque peer identity;
- datagram send/receive;
- readiness integration;
- UDP/socket error mapping;
- carrier MTU/datagram constraints.

It does not own:

- VSPW ACK/retry policy;
- VSPW keepalive semantics;
- VSPW session IDs;
- packet fragmentation/reassembly policy;
- EOP/EEP;
- time-code semantics;
- duplicate suppression.

## Raw-Ethernet provider mapping

A raw-Ethernet provider will own:

- interface/MAC binding;
- Ethernet frame send/receive;
- source/destination MAC identity;
- carrier MTU;
- readiness/completion integration;
- platform-specific raw-frame or board-driver glue.

VSPW frames are carried directly in an Ethernet payload. The EtherType or other
protocol discriminator must be selected and documented deliberately before the
provider is merged. An arbitrary registered EtherType must not be borrowed.

## In-memory provider

An in-memory provider should be introduced early enough to test the extracted
VSPW engine without kernel/network scheduling.

It should support deterministic:

- send/receive message boundaries;
- peer identity;
- controlled loss/duplication/reordering where required by protocol tests;
- readiness wakeup;
- bounded MTU.

This becomes the preferred unit-level proof that the engine no longer depends
on socket APIs.

## Extraction sequence

1. Freeze the pre-refactor UDP baseline (#230).
2. Define private provider/runtime contracts and deterministic test provider.
3. Move carrier-independent VSPW session/reliability state out of
   `udp_backend.c` without changing behavior.
4. Adapt the existing POSIX/Winsock UDP implementation to the provider
   contract.
5. Prove current UDP tests and backend-equivalence tests unchanged.
6. Measure provider-dispatch overhead against the frozen baseline.
7. Add PC raw-Ethernet provider and controlled comparison.
8. Add embedded raw-Ethernet binding when board-driver support is ready.

Each step should remain reviewable. A single giant rewrite would technically
be possible, in the same way that juggling chainsaws is technically possible.

## Compatibility rule

The public application-facing API remains unchanged by this refactor:

- `spw_port_send()`
- `spw_port_receive()`
- lifecycle/query/readiness APIs;
- time-code APIs;
- public packet/terminator semantics.

VSPW codec and provider interfaces remain private implementation details.


## Raw-Ethernet carrier binding

The first raw-Ethernet binding keeps Ethernet ownership below the VSPW engine
and exposes a narrow frame-I/O contract to platform integration code.

The software stack is:

```text
application
    -> spw_port_* API
        -> raw-Ethernet backend
            -> carrier-independent VSPW engine
                -> raw-Ethernet transport provider
                    -> spw_raw_ethernet_io_ops
                        -> AF_PACKET / board MAC-DMA driver / other platform glue
```

The public raw-frame callbacks exchange complete Ethernet-II frames beginning
with destination MAC and ending with the payload. Preamble/SFD and FCS are not
part of the callback buffer. SpWKit owns the Ethernet protocol framing above
those callbacks; the platform binding owns only frame transport, readiness and
link state.

### Development frame format

The initial development format is:

```text
0               6              12     14     16  17  18     20
+---------------+---------------+------+------+---+---+------+------------------+
| Destination   | Source MAC    |Type  |Subtyp|Maj|Min|Length| VSPW-TP frame    |
| MAC (6 bytes) | (6 bytes)     |2 B   |2 B   |1 B|1 B| 2 B  | ...              |
+---------------+---------------+------+------+---+---+------+------------------+
```

All multi-byte framing values are network byte order.

The subtype defaults to the SpWKit development discriminator
`0x5357`. Development framing version 2.0 adds an explicit 16-bit VSPW-frame
length. That length is authoritative for decapsulation, so Ethernet minimum-
frame padding is ignored rather than exposed as VSPW data. Keeping an explicit
subtype, version and length also means a future standards/registration change
can replace the outer Ethernet identification without changing the VSPW-TP
protocol engine.

### EtherType policy

SpWKit does not claim an unassigned permanent EtherType.

For isolated development networks,
`SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE` is `0x88B5`, IEEE Local
Experimental EtherType 1. The caller must still set `ether_type` explicitly;
the raw-Ethernet configuration initializer leaves it at zero so experimental
identifiers cannot silently become production protocol identifiers.

Wider deployment must move to an appropriate IEEE-assigned EtherType or another
standards-compliant organization-specific identification scheme. Changing that
outer identifier does not require a VSPW engine change.

### Embedded binding rule

`spw_raw_ethernet_io_ops_t` deliberately contains no STM32, lwIP, DMA
descriptor, MMIO, POSIX or RTOS type. An embedded binding can implement:

- frame TX submission using the board MAC/DMA driver;
- frame RX completion using DMA-owned buffers copied or presented to the
  binding;
- level-triggered readiness using an IRQ-to-task event/semaphore;
- link state using the board PHY/MAC status path.

The separate `spw_runtime_ops_t` supplies monotonic time and bounded delay.
A hardRT integration may implement those runtime hooks and the frame-I/O wait
operation, but SpWKit does not include or depend on hardRT itself.

The same public raw-Ethernet backend can therefore be composed over a host
raw-frame facility or an embedded board driver without changing VSPW protocol
logic or application-facing `spw_port_*` calls.

### Current limitations

The first raw-Ethernet carrier intentionally does not interpret VLAN tags,
bridging headers or jumbo-frame policy. Its input is an Ethernet-II frame with
the configured EtherType directly at bytes 12-13. Platform integration may
perform NIC/interface selection and filtering below this boundary.

Zero-copy ownership across the raw-Ethernet/VSPW boundary is not claimed in
this first implementation. Copy count and ownership reduction remain explicit
performance work under #230 rather than being hidden behind an API name.
