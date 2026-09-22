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

## Implemented separation

The transport/provider boundary is now implemented.

`vspw_engine.c/.h` owns carrier-independent protocol behavior:

- session identity and remote-session rollover;
- VSPW sequence/message IDs;
- DATA/TIME_CODE/KEEPALIVE/ACK construction;
- ACK matching, retry and duplicate suppression;
- packet fragmentation/reassembly;
- EOP/EEP and time-code semantics;
- keepalive/liveness state;
- virtual-link timing and deterministic VSPW fault state;
- VSPW-visible statistics.

`udp_transport_provider.c/.h` owns UDP carrier mechanics:

- socket creation, bind and destruction;
- IPv4/port addressing and opaque peer identity;
- `sendto()` / `recvfrom()`;
- readiness polling and socket error translation;
- UDP carrier MTU and timeout conversion.

`udp_backend.c` is the composition adapter. It validates the public UDP
configuration, creates the provider/runtime bindings, translates public UDP
fault rules into the private VSPW fault model and forwards backend operations
to the engine.

The engine validates its configured VSPW fragment size against the selected
provider MTU. VSPW-TP protocol limits are therefore no longer derived from the
UDP datagram ceiling.

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

1. Freeze the pre-refactor UDP baseline (#230). **Complete.**
2. Define private provider/runtime contracts and deterministic test provider.
   **Complete.**
3. Adapt the existing POSIX/Winsock UDP implementation to the provider
   contract. **Complete.**
4. Extract carrier-independent VSPW session/reliability/reassembly state.
   **Complete in the current refactor workstream.**
5. Prove current UDP tests and backend-equivalence tests unchanged.
   **Continuous CI gate.**
6. Measure the refactored UDP path and provider-dispatch cost against the
   frozen `v0.7.0` baseline. **Current performance gate.**
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
