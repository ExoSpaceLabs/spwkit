# VSPW-TP v1

VSPW-TP is the transport protocol used by the distributed virtual SpaceWire backend. It is an internal SpWKit transport contract, not an application-facing API and not a replacement for SpaceWire packet semantics.

Applications continue to call `libspwkit`. A distributed backend translates packet, time-code and link events into VSPW-TP messages and transports them over UDP or another datagram transport.

## Design rules

- network byte order is used for all multi-byte integers;
- the v1 fixed header is 40 bytes;
- every frame carries the non-zero 64-bit session ID of its sender;
- one VSPW-TP message belongs to one virtual `link_id`;
- `message_id` identifies one logical SpaceWire-side event across fragments and retries;
- `sequence` identifies individual transport datagrams for ordering/diagnostics;
- retransmission may use new sequence values while retaining the same logical `message_id` and sender session;
- fragments are never visible through the public SpaceWire API;
- EOP and EEP belong to the logical SpaceWire packet, not to UDP datagrams;
- transport packet loss is not itself a simulated SpaceWire error;
- public SpWKit headers do not expose UDP socket or VSPW-TP wire structures.

The session field is part of every frame rather than only KEEPALIVE. This is necessary to distinguish current traffic from delayed datagrams belonging to a previous peer process after restart.

## Header

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `VSPW` (`0x56535057`) |
| 4 | 1 | version major | `1` |
| 5 | 1 | version minor | `0` |
| 6 | 1 | message type | see below |
| 7 | 1 | flags | see below |
| 8 | 2 | header size | `40` for v1 |
| 10 | 2 | payload size | bytes following this header in this datagram |
| 12 | 4 | link id | virtual link identifier |
| 16 | 8 | session id | non-zero sender transport session |
| 24 | 4 | sequence | transport datagram sequence number |
| 28 | 4 | message id | logical event/packet identifier |
| 32 | 4 | fragment offset | byte offset into logical payload |
| 36 | 4 | total size | complete logical payload size |

## Message types

| Value | Name | Meaning |
|---:|---|---|
| 1 | DATA | SpaceWire packet payload |
| 2 | TIME_CODE | SpaceWire time-code event |
| 3 | LINK_CONTROL | reserved distributed link-control event |
| 4 | KEEPALIVE | peer/session liveness transport event |
| 5 | ACK | session-bound logical-message acknowledgement |

Unknown message types are rejected in v1.

### DATA

DATA carries one SpaceWire packet. Fragmented DATA messages retain one `message_id` and sender `session_id` across every fragment and retransmission of that logical packet.

For an unfragmented DATA message, both fragment flags are clear, `fragment_offset` is zero and `payload_size == total_size`.

For a fragmented DATA message, the first fragment has `FRAGMENT_START` and offset zero. The final fragment has `FRAGMENT_END` and ends exactly at `total_size`. Intermediate fragments have neither boundary flag. Reassembly completes before a packet is surfaced to the application.

The UDP backend keeps one active fragmented logical packet at a time, but its fragments may arrive in arbitrary UDP order. A fixed one-bit-per-payload-byte coverage map records the received portions of the existing 1 MiB reassembly buffer. Exact duplicate fragments and byte-identical partial overlaps are idempotent; conflicting overlaps or inconsistent message metadata are dropped. A packet becomes application-visible only after every payload byte is covered and both `FRAGMENT_START` and `FRAGMENT_END` have been observed.

Incomplete reassembly is cleared on peer/session loss or transition and expires after `peer_timeout_ms` without DATA-fragment activity. KEEPALIVEs keep the peer session alive but do not indefinitely preserve a stalled partial packet. Transport reordering or loss never synthesizes an application-visible EEP.

### TIME_CODE

TIME_CODE v1.0 uses a two-byte payload:

```text
{ time_count, control_flags }
```

Reliable TIME_CODE events use `ACK_REQUIRED` and a non-zero logical `message_id` in the same way as DATA.

### ACK

The ACK frame header identifies the ACK sender's current session. Its payload carries exactly one 64-bit non-zero **acknowledged sender session ID** in network byte order:

- `payload_size == 8`;
- `total_size == 8`;
- `message_id` identifies the logical DATA/TIME_CODE event being acknowledged;
- `header.session_id` identifies the peer sending the ACK;
- the 8-byte payload identifies the session that originally sent the acknowledged event;
- ACK itself never carries `ACK_REQUIRED`.

Acknowledgement is **logical-message based**, not per-fragment. A fragmented packet is acknowledged only after complete reassembly has been accepted into the backend receive queue.

The sender accepts an ACK only when:

1. the ACK header belongs to the currently established remote session;
2. the logical `message_id` matches its pending event; and
3. the ACK payload matches its own current local session ID.

That prevents delayed ACKs from old peer or local sessions from completing a new reliable transfer after restart/message-ID reuse.

The receiver remembers a bounded history of delivered logical message IDs for the current remote session. If an ACK is lost and the sender retransmits the same logical message, the receiver re-sends the ACK without surfacing the packet/time-code a second time.

### KEEPALIVE

KEEPALIVE is header-only in v1. Its `session_id` field advertises the sender's current transport session. `payload_size`, `total_size`, and `message_id` are zero.

KEEPALIVE is the only frame allowed to establish or replace the current remote session. Once established, DATA, TIME_CODE, ACK, and other control traffic are accepted only when their header `session_id` matches that current peer session.

When a new peer session is accepted, the backend clears partial reassembly and duplicate-history state for the prior session while preserving already completed application-visible receive data. A bounded retired-session history prevents delayed KEEPALIVEs from immediately reverting the peer to a recently superseded session. Frames from retired or otherwise non-current sessions are ignored and do not refresh peer liveness.

This also handles UDP reordering around restart: DATA from a new session that arrives before its KEEPALIVE is ignored rather than delivered ambiguously; after the KEEPALIVE establishes the session, normal reliable retransmission delivers it once.

The UDP backend additionally validates source IPv4 address and source UDP port against the configured peer. `link_id` alone is not treated as peer identity.

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
4. the peer ACKs the logical `message_id` after accepting it, with both frames bound to transport sessions;
5. while normal SpWKit API calls service the backend, an unacknowledged message is retransmitted after `ack_timeout_ms`;
6. retries are bounded by `max_retries`;
7. retry exhaustion maps to `SPW_LINK_ERROR_WAIT`, increments link-error/drop statistics once for that failed logical event, and returns `SPW_ERR_LINK_UNAVAILABLE` from service-dependent operations;
8. a later valid current-session keepalive/traffic may recover the link without re-opening the public port.

This model deliberately avoids requiring a second thread merely to make two peers work. Blocking receive/wait operations and link-state polling also service keepalive/ACK/retry traffic. Their internal wake interval is bounded by the next keepalive or ACK timeout so retransmission is not delayed behind a longer receive wait.

## Virtual-link timing

The UDP backend can apply deterministic **SpaceWire-side** timing before the first transport transmission of each logical DATA or TIME_CODE event. `virtual_link_bps` controls effective serialization delay and `virtual_latency_us` adds one fixed propagation/processing delay. Both default to zero, preserving the previous immediate behavior.

The model is intentionally logical rather than PHY-accurate. DATA serialization charges the payload plus one logical terminator octet. TIME_CODE serialization charges its two-byte logical event. Serialization delay is rounded up to the next microsecond.

ACK and KEEPALIVE are VSPW-TP transport control frames, so they do not consume the virtual SpaceWire timing budget. Likewise, retransmitting an already accepted logical message after an ACK timeout does not reapply SpaceWire serialization or propagation latency. The transport may repeat datagrams; the simulated SpaceWire application event occurred once.

Caller send timeouts include the virtual delay. If the remaining timeout cannot cover the configured delay, the send returns `SPW_ERR_TIMEOUT` before occupying the reliable TX slot. While a non-zero delay elapses, the backend continues cooperative peer/control servicing through the normal pump path; no background worker is introduced.

Host Ethernet/UDP latency is not measured or folded into this model. It remains incidental carrier behavior and cannot redefine the configured virtual SpaceWire timing.

## Deterministic fault injection

The UDP backend can apply deterministic seeded fault rules without changing the VSPW-TP wire format. Rules are fixed-size configuration state and require no dynamic allocation or mandatory worker thread. Each matching rule advances its own reproducible PRNG stream derived from the configured seed; an always-fire probability is available for exact CI scenarios.

Transport faults operate strictly on VSPW-TP carrier datagrams after framing:

- **drop** suppresses a selected datagram while reporting local transport success so the normal reliability machinery can observe the loss and retry where appropriate;
- **duplicate** transmits the selected datagram twice;
- **reorder** holds one selected datagram in a fixed datagram-sized slot and sends the next outgoing datagram first, providing a bounded adjacent swap;
- **delay** postpones one selected transport datagram by the configured microseconds and consumes the applicable transport timeout.

Targets can distinguish DATA, TIME_CODE, ACK and KEEPALIVE/control traffic. This makes scenarios such as a dropped ACK reproducible without modifying application payloads or pretending that carrier loss is a SpaceWire event. Reliable retransmission and duplicate suppression continue to operate normally around injected transport faults.

The explicitly SpaceWire-visible fault action is **EEP injection**. It applies to an outgoing logical DATA packet before VSPW-TP framing, converting a selected EOP terminator to EEP. This is intentionally a different fault domain: transport drop, duplicate, reorder or delay never synthesizes EEP.

`spw_port_get_fault_statistics()` reports transport drops, duplicates, reorders and delays separately from SpaceWire EEP injections. This separation is part of the simulation contract rather than merely a diagnostic convention.

## Liveness

Each started UDP backend chooses a new non-zero local session ID and advertises it through KEEPALIVE while stamping the same ID on all other frames. `keepalive_interval_ms` controls periodic advertisement while API calls are servicing the transport. `peer_timeout_ms` controls when lack of valid current-session traffic from the configured peer is mapped to `SPW_LINK_ERROR_WAIT`.

There is no hidden mandatory background worker in v0.2. If an application performs no SpWKit calls at all, transport timers do not execute in the background. This is intentional for portability to bare-metal/RTOS adapters. Applications that need continuous liveness observation can poll link state or keep a blocking receive/service loop active.

## Bounds

The protocol codec defines:

- maximum UDP payload: 65,507 bytes;
- fixed VSPW-TP header: 40 bytes;
- maximum single fragment payload: 65,467 bytes;
- maximum logical packet represented by the protocol: 16 MiB.

The UDP backend deliberately advertises a smaller 1 MiB maximum packet size so TX retention and reassembly storage remain bounded and deterministic. Reassembly uses the 1 MiB payload buffer plus a fixed 128 KiB coverage bitmap (one bit per possible payload byte), avoiding an arbitrary fragment-range-count limit. Its default fragment payload is 1200 bytes to avoid relying on IP fragmentation.

## UDP backend configuration

`SPW_BACKEND_UDP` is selected through the normal `spw_port_config_t`. `spw_udp_config_t` supplies:

- local/remote numeric IPv4 addresses;
- local/remote UDP ports;
- virtual `link_id`;
- fragment payload size;
- ACK timeout;
- maximum retransmissions;
- keepalive interval;
- peer timeout;
- effective virtual SpaceWire link bit rate;
- fixed virtual propagation/processing latency;
- deterministic fault seed and fixed transport/SpaceWire fault rules.

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

The earlier 32-byte VSPW-TP draft existed only on unreleased v0.2 development history. The session-aware 40-byte format is the VSPW-TP v1 format used by the v0.2.0 release.

## Validation

A receiver rejects a message before using its payload when any of the following is true:

- magic does not match;
- version is unsupported;
- message type is unknown;
- header size is not the v1 fixed size;
- sender session ID is zero;
- encoded payload length does not fit the received datagram;
- flags contain unknown/illegal combinations;
- fragment offset/length exceeds the logical total size;
- logical total size exceeds the protocol bound;
- TIME_CODE/KEEPALIVE/ACK message shape is invalid;
- an ACK contains a zero/invalid acknowledged-session payload;
- non-DATA messages contain DATA terminator/fragment flags;
- source address/port does not match the configured UDP peer.

After structural validation, non-KEEPALIVE frames from a session other than the current remote session are ignored before they can affect application-visible queues or liveness.

## Verification

The codec has golden-vector and malformed-frame tests for the 40-byte session-aware header, DATA, TIME_CODE, session-bound ACK and header-only KEEPALIVE framing.

The distributed backend integration tests verify:

- 5 KiB EEP transfer using 512-byte fragments;
- deliberately reordered and duplicated UDP fragments reassembling into one packet;
- stale incomplete reassembly expiring without losing the current peer session;
- insufficient-capacity retry without packet consumption;
- reverse traffic;
- reliable time-code transfer;
- forced ACK timeout and logical-message retransmission;
- duplicate suppression after retry;
- peer timeout mapping to `SPW_LINK_ERROR_WAIT`;
- peer restart/session recovery back to `SPW_LINK_RUN`;
- compile-time deterministic virtual timing calculations;
- immediate send timeout when configured virtual delay is non-zero;
- successful DATA and TIME_CODE delivery when the caller budget covers the virtual delay;
- dropped ACK recovery without duplicate logical delivery;
- deterministic DATA duplication and bounded adjacent fragment reordering;
- transport-delay timeout behavior;
- explicit EOP-to-EEP SpaceWire fault injection with separate fault-domain counters.

The dedicated Device-to-device workflow executes the distributed tests as a real transport gate.
