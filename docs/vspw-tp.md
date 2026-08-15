# VSPW-TP v1

VSPW-TP is the transport protocol used by the distributed virtual SpaceWire backend. It is an internal SpWKit transport contract, not an application-facing API and not a replacement for SpaceWire packet semantics.

Applications continue to call `libspwkit`. A distributed backend translates packet, time-code and link events into VSPW-TP messages and transports them over UDP or another datagram transport.

## Design rules

- network byte order is used for all multi-byte integers;
- the v1 fixed header is 32 bytes;
- one VSPW-TP message belongs to one virtual `link_id`;
- `message_id` identifies one logical SpaceWire-side event across fragments;
- `sequence` orders transport messages and supports later duplicate/loss handling;
- fragments are never visible through the public SpaceWire API;
- EOP and EEP belong to the logical SpaceWire packet, not to UDP datagrams;
- transport packet loss is not itself a simulated SpaceWire error;
- public SpWKit headers do not expose UDP, socket or VSPW-TP structures.

## Header

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `VSPW` (`0x56535057`) |
| 4 | 1 | version major | `1` |
| 5 | 1 | version minor | `0` |
| 6 | 1 | message type | see below |
| 7 | 1 | flags | see below |
| 8 | 2 | header size | `32` for v1 |
| 10 | 2 | payload size | bytes following this header in this datagram |
| 12 | 4 | link id | virtual link identifier |
| 16 | 4 | sequence | transport sequence number |
| 20 | 4 | message id | logical event/packet identifier |
| 24 | 4 | fragment offset | byte offset into logical payload |
| 28 | 4 | total size | complete logical payload size |

## Message types

| Value | Name | Meaning |
|---:|---|---|
| 1 | DATA | SpaceWire packet payload |
| 2 | TIME_CODE | SpaceWire time-code event |
| 3 | LINK_CONTROL | distributed link-control event |
| 4 | KEEPALIVE | peer-liveness transport event |
| 5 | ACK | transport acknowledgement |

Unknown message types are rejected in v1.

## Flags

| Bit | Name | Meaning |
|---:|---|---|
| 0 | EOP | DATA packet terminates with EOP |
| 1 | EEP | DATA packet terminates with EEP |
| 2 | FRAGMENT_START | first fragment of a fragmented DATA message |
| 3 | FRAGMENT_END | last fragment of a fragmented DATA message |
| 4 | ACK_REQUIRED | sender requests transport acknowledgement |

EOP and EEP are mutually exclusive. They are only legal for DATA messages.

For an unfragmented DATA message, both fragment flags are clear, `fragment_offset` is zero and `payload_size == total_size`.

For a fragmented DATA message, all fragments share `link_id` and `message_id`. The first fragment has `FRAGMENT_START` and offset zero. The final fragment has `FRAGMENT_END` and ends exactly at `total_size`. Intermediate fragments have neither fragment-boundary flag. Reassembly must complete before a packet is surfaced to the application.

## Bounds

The protocol codec currently defines:

- maximum UDP payload: 65,507 bytes;
- fixed VSPW-TP header: 32 bytes;
- maximum single fragment payload: 65,475 bytes;
- maximum logical packet represented by the protocol: 16 MiB.

Backends may advertise and enforce smaller packet limits. A normal UDP backend is expected to use a substantially smaller configured fragment payload, for example around 1200 bytes, to avoid IP fragmentation. The protocol limit is not a recommendation to emit maximum-size UDP datagrams, because networking already has enough opportunities for regret.

## Version compatibility

A v1.0 decoder accepts only major version 1 and minor versions less than or equal to the version it implements. Since the current implementation is 1.0, this currently means exactly 1.0.

A different major version is rejected. Future minor versions may only add behavior that an older decoder can safely ignore; otherwise a major-version change is required.

## Validation

A receiver rejects a message before using its payload when any of the following is true:

- magic does not match;
- version is unsupported;
- message type is unknown;
- header size is not the v1 fixed size;
- encoded payload length exceeds the received datagram;
- flags contain unknown/illegal combinations;
- fragment offset/length exceeds the logical total size;
- logical total size exceeds the protocol bound;
- non-DATA messages contain DATA terminator/fragment flags.

Remote length fields are therefore bounds-checked before they can drive allocation or copying.

## Payload contracts

The header codec intentionally does not yet define the complete payload body for every control message. DATA payload bytes are already fully defined by the header. TIME_CODE, LINK_CONTROL, KEEPALIVE and ACK body details will be fixed alongside the distributed backend implementation before v0.2.0 is released.

This separation lets the framing/parser become stable first without pretending unfinished control-plane behavior is already standardized.
