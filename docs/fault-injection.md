# Deterministic fault injection

SpWKit v0.2 provides bounded deterministic fault injection in the distributed UDP backend. The design keeps two fault domains explicit: VSPW-TP transport faults manipulate carrier datagrams, while SpaceWire-visible faults manipulate logical SpaceWire events.

## Configuration model

`spw_udp_config_t` contains a fixed array of eight `spw_udp_fault_rule_t` entries and a `fault_seed`. Disabled rules are zero-cost configuration entries and no dynamic rule allocation is required.

Each enabled rule defines an action, target, probability in units of 1/10000, optional maximum firing count, and delay for transport-delay actions. Rules are evaluated in array order. Each rule has a deterministic PRNG stream derived from the configured seed, so an identical configuration and event stream reproduce the same decisions.

A probability of `SPW_UDP_FAULT_PROBABILITY_SCALE` (10000) means always fire. `max_events == 0` means unlimited firings; a non-zero value bounds how many times the rule may inject a fault.

## Transport fault domain

Transport actions operate after VSPW-TP framing and may target DATA, TIME_CODE, ACK or KEEPALIVE/control datagrams:

- `SPW_UDP_FAULT_ACTION_TRANSPORT_DROP` suppresses the selected datagram;
- `SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE` sends it twice;
- `SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER` performs a bounded adjacent swap using one fixed datagram-sized holding slot;
- `SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY` delays the selected datagram by `delay_us`.

The normal reliability layer remains responsible for retry and duplicate suppression. In particular, a dropped ACK can cause complete-message retransmission, while the receiver still exposes the logical packet only once.

Transport faults do **not** synthesize SpaceWire EEP. A network failure is not silently promoted into a SpaceWire link/packet error.

## SpaceWire fault domain

`SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP` is valid only for DATA. When it fires for an outgoing EOP packet, the logical terminator becomes EEP before VSPW-TP framing. The receiver therefore observes EEP through the normal `spw_port_receive()` API.

This explicit action is the only v0.2 fault rule currently intended to alter an application-visible SpaceWire packet. Additional SpaceWire-side fault types can extend this domain without overloading transport-loss semantics.

## Diagnostics

Fault-capable backends advertise `SPW_CAP_FAULT_INJECTION`. `spw_port_get_fault_statistics()` returns `spw_fault_statistics_t`, which contains separate counters for transport drops, duplicates, reorders, delays and SpaceWire EEP injections.

`spw_port_clear_fault_statistics()` clears these diagnostics without modifying the deterministic rule schedule. Explicit port `reset()` restarts the deterministic injector state.

## Bounds and portability

The implementation uses fixed rule storage and one fixed maximum-UDP-datagram reorder slot. It introduces no mandatory heap allocation, thread, socket type or vendor handle into the common application API. The deterministic decision engine is socket-independent; only application of transport actions belongs to the POSIX UDP backend.

## Verification

The v0.2 test suite covers deterministic rule replay, rule validation, dropped-ACK recovery, duplicate suppression, arbitrary-order reassembly under injected adjacent reordering, transport-delay timeout behavior, explicit SpaceWire EEP injection, and fault-domain statistics.
