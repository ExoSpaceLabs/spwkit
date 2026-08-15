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
- public SpWKit headers do not expose UDP socket or VSPW-TP wire structures.

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
| 3 | LINK_CONTROL | reserved distributed link-control event |
| 4 | KEEPALIVE | reserved peer-liveness transport event |
| 5 | ACK | reserved transport acknowledgement |

Unknown message types are rejected in v1.

TIME_CODE v1.0 uses a two-byte payload: `{time_count, control_flags}`. The remaining control-message bodies stay reserved until their state-machine semantics are implemented.

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

For a fragmented DATA message, all fragments share `link_id` and `message_id`. The first fragment has `FRAGMENT_START` and offset zero. The final fragment has `FRAGMENT_END` and ends exactly at `total_size`. Intermediate fragments have neither fragment-boundary flag. Reassembly completes before a packet is surfaced to the application.

## Bounds

The protocol codec defines:

- maximum UDP payload: 65,507 bytes;
- fixed VSPW-TP header: 32 bytes;
- maximum single fragment payload: 65,475 bytes;
- maximum logical packet represented by the protocol: 16 MiB.

The initial UDP backend deliberately advertises a smaller 1 MiB maximum packet size so reassembly storage remains bounded and deterministic. Its default fragment payload is 1200 bytes to avoid relying on IP fragmentation. `spw_udp_config_t` may request another supported fragment size.

The v0.2 initial reassembler accepts one logical packet at a time and requires contiguous offsets for that packet. Sequence and message identity are already present in the wire format so later duplicate/loss/reordering handling can evolve without changing the public SpaceWire API or the v1 header.

## UDP backend configuration

`SPW_BACKEND_UDP` is selected through the normal `spw_port_config_t`. `spw_udp_config_t` supplies local/remote numeric IPv4 addresses, local/remote UDP ports, the virtual `link_id`, and fragment payload size. Both endpoints are peers; there is no application-visible server/client role.

```text
Application A                        Application B
    |                                    |
libspwkit                            libspwkit
    |                                    |
SPW_BACKEND_UDP                     SPW_BACKEND_UDP
    |                                    |
    +----------- VSPW-TP/UDP ------------+
```

The backend currently implements packet transfer, EOP/EEP, time codes, statistics, timeouts, fragmentation, and reassembly. Completed packets retain the normal no-truncation rule: if caller receive capacity is too small, `SPW_ERR_BUFFER_TOO_SMALL` reports the required length without consuming the completed packet.

UDP loss/reordering is not silently reclassified as a simulated SpaceWire error. ACK, keepalive, disconnect detection and richer distributed link control remain explicit transport-layer work rather than accidental packet-API behavior.

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

Remote length fields are therefore bounds-checked before they can drive copying.

## Verification

The codec has golden-vector and malformed-frame tests. The distributed backend integration test creates two localhost peers through the public `spw_port_*` API, sends a 5 KiB EEP packet using 512-byte fragments, verifies insufficient-capacity retry without packet consumption, verifies reverse traffic, and round-trips a time code. The dedicated Device-to-device workflow executes this test instead of reporting a staged no-op as success.
