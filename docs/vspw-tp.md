# VSPW-TP v1

VSPW-TP is the transport protocol used by the distributed virtual SpaceWire backend. It is an internal SpWKit transport contract, not an application-facing API and not a replacement for SpaceWire packet semantics.

Applications continue to call `libspwkit`. A distributed backend translates packet, time-code and link events into VSPW-TP messages and transports them over UDP or another datagram transport.

## Design rules

- network byte order is used for all multi-byte integers;
- the v1 fixed header remains 32 bytes;
- one VSPW-TP message belongs to one virtual `link_id`;
- `message_id` identifies one logical SpaceWire-side event across fragments and retries;
- `sequence` identifies individual transport datagrams for ordering/diagnostics;
- retransmission may use new sequence values while retaining the same logical `message_id`;
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
| 16 | 4 | sequence | transport datagram sequence number |
| 20 | 4 | message id | logical event/packet identifier |
| 24 | 4 | fragment offset | byte offset into logical payload |
| 28 | 4 | total size | complete logical payload size |

## Message types

| Value | Name | Meaning |
|---:|---|---|
| 1 | DATA | SpaceWire packet payload |
| 2 | TIME_CODE | SpaceWire time-code event |
| 3 | LINK_CONTROL | reserved distributed link-control event |
| 4 | KEEPALIVE | peer/session liveness transport event |
| 5 | ACK | logical-message transport acknowledgement |

Unknown message types are rejected in v1.

### DATA

DATA carries one SpaceWire packet. Fragmented DATA messages retain one `message_id` across every fragment and every retransmission of that logical packet.

For an unfragmented DATA message, both fragment flags are clear, `fragment_offset` is zero and `payload_size == total_size`.

For a fragmented DATA message, the first fragment has `FRAGMENT_START` and offset zero. The final fragment has `FRAGMENT_END` and ends exactly at `total_size`. Intermediate fragments have neither boundary flag. Reassembly completes before a packet is surfaced to the application.

The current UDP reassembler accepts one logical fragmented packet at a time and requires contiguous fragment offsets. Reordered/missing fragments therefore prevent acknowledgement; the sender later retransmits the complete logical message. Reordering is a transport recovery event, not an application-visible EEP.

### TIME_CODE

TIME_CODE v1.0 uses a two-byte payload:

```text
{ time_count, control_flags }
```

Reliable TIME_CODE events use `ACK_REQUIRED` and a non-zero logical `message_id` in the same way as DATA.

### ACK

ACK is header-only:

- `payload_size == 0`;
- `total_size == 0`;
- `message_id` is the logical DATA/TIME_CODE message being acknowledged;
- ACK itself never carries `ACK_REQUIRED`.

Acknowledgement is **logical-message based**, not per-fragment. A fragmented packet is acknowledged only after complete reassembly has been accepted into the backend receive queue.

The receiver remembers a bounded history of delivered logical message IDs. If an ACK is lost and the sender retransmits the same logical message, the receiver re-sends ACK without surfacing the packet/time-code to the application a second time.

### KEEPALIVE

KEEPALIVE carries one 64-bit non-zero session identifier in network byte order. It has no SpaceWire payload semantics and never requires ACK.

A new peer session identifier indicates that the configured peer restarted/reopened its transport session. The receiver clears partial reassembly and duplicate-history state for the old session while preserving already completed application-visible receive data.

The UDP backend also validates the source IPv4 address and source UDP port against the configured peer before accepting a frame. `link_id` alone is not treated as peer identity.

## Flags

| Bit | Name | Meaning |
|---:|---|---|
| 0 | EOP | DATA packet terminates with EOP |
| 1 | EEP | DATA packet terminates with EEP |
| 2 | FRAGMENT_START | first fragment of fragmented DATA |
| 3 | FRAGMENT_END | final fragment of fragmented DATA |
| 4 | ACK_REQUIRED | DATA/TIME_CODE requires logical acknowledgement |

EOP and EEP are mutually exclusive and legal only for DATA. `ACK_REQUIRED` is currently legal only for DATA and TIME_CODE.

## Reliability model

The v0.2 UDP backend uses bounded cooperative reliability rather than a mandatory background thread.

For one port:

1. at most one logical outbound DATA/TIME_CODE event is retained as the reliable TX slot;
2. the first transmission uses normal VSPW-TP fragmentation;
3. successful `spw_port_send*` means the event was accepted by the backend and transmitted, not that the remote application consumed it;
4. the peer ACKs the logical `message_id` after accepting it;
5. while normal SpWKit API calls service the backend, an unacknowledged message is retransmitted after `ack_timeout_ms`;
6. retries are bounded by `max_retries`;
7. retry exhaustion maps to `SPW_LINK_ERROR_WAIT`, increments link-error/drop statistics, and returns `SPW_ERR_LINK_UNAVAILABLE` from service-dependent operations;
8. a later valid peer message/ACK/keepalive may recover the link without re-opening the public port.

This model deliberately avoids requiring a second thread merely to make two peers work. Blocking receive/wait operations and link-state polling also service keepalive/ACK traffic.

## Liveness

Each started UDP backend advertises a new local session ID through KEEPALIVE. `keepalive_interval_ms` controls periodic advertisement while API calls are servicing the transport. `peer_timeout_ms` controls when lack of valid traffic from the configured peer is mapped to `SPW_LINK_ERROR_WAIT`.

There is no hidden mandatory background worker in v0.2. If an application performs no SpWKit calls at all, transport timers do not execute in the background. This is intentional for portability to bare-metal/RTOS adapters. Applications that need continuous liveness observation can poll link state or keep a blocking receive/service loop active.

## Bounds

The protocol codec defines:

- maximum UDP payload: 65,507 bytes;
- fixed VSPW-TP header: 32 bytes;
- maximum single fragment payload: 65,475 bytes;
- maximum logical packet represented by the protocol: 16 MiB.

The UDP backend deliberately advertises a smaller 1 MiB maximum packet size so TX retention and reassembly storage remain bounded and deterministic. Its default fragment payload is 1200 bytes to avoid relying on IP fragmentation.

## UDP backend configuration

`SPW_BACKEND_UDP` is selected through the normal `spw_port_config_t`. `spw_udp_config_t` supplies:

- local/remote numeric IPv4 addresses;
- local/remote UDP ports;
- virtual `link_id`;
- fragment payload size;
- ACK timeout;
- maximum retransmissions;
- keepalive interval;
- peer timeout.

Both endpoints are peers; there is no application-visible server/client role.

```text
Application A                        Application B
    |                                    |
libspwkit                            libspwkit
    |                                    |
SPW_BACKEND_UDP                     SPW_BACKEND_UDP
    |                                    |
    +----------- VSPW-TP/UDP ------------+
```

Completed packets retain the normal no-truncation rule: if caller receive capacity is too small, `SPW_ERR_BUFFER_TOO_SMALL` reports the required length without consuming the completed packet.

## Version compatibility

A v1.0 decoder accepts only major version 1 and minor versions less than or equal to the version it implements. Since the current implementation is 1.0, this currently means exactly 1.0.

A different major version is rejected. Future minor versions may only add behavior that an older decoder can safely ignore; otherwise a major-version change is required.

## Validation

A receiver rejects a message before using its payload when any of the following is true:

- magic does not match;
- version is unsupported;
- message type is unknown;
- header size is not the v1 fixed size;
- encoded payload length does not fit the received datagram;
- flags contain unknown/illegal combinations;
- fragment offset/length exceeds the logical total size;
- logical total size exceeds the protocol bound;
- TIME_CODE/KEEPALIVE/ACK message shape is invalid;
- non-DATA messages contain DATA terminator/fragment flags;
- source address/port does not match the configured UDP peer.

Remote length fields are therefore bounds-checked before they can drive copying.

## Verification

The codec has golden-vector and malformed-frame tests for DATA, TIME_CODE, ACK and KEEPALIVE framing.

The distributed backend integration test creates two localhost peers through the public `spw_port_*` API and verifies:

- 5 KiB EEP transfer using 512-byte fragments;
- insufficient-capacity retry without packet consumption;
- reverse traffic;
- reliable time-code transfer;
- forced ACK timeout and logical-message retransmission;
- duplicate suppression after retry;
- peer timeout mapping to `SPW_LINK_ERROR_WAIT`;
- peer restart/session recovery back to `SPW_LINK_RUN`.

The dedicated Device-to-device workflow executes this test as a real transport gate.
