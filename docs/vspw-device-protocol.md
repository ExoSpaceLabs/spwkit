# VSPD v1.1 — virtual SpaceWire device protocol

VSPD is the private protocol between a hosted SpWKit Linux-device backend and the `vspwd` userspace service.

It is **not** an application API. Applications continue to use the public `spw_port_*` C API, or the optional `spwkit::cpp` wrapper above that C API.

VSPD is also deliberately distinct from VSPW-TP:

- **VSPW-TP** carries distributed virtual SpaceWire over UDP/IP;
- **VSPD** carries local device/backend operations between `libspwkit` and `vspwd`.

```text
Application
    |
spw_port_* / optional spwkit::cpp
    |
Linux device backend
    |
VSPD v1 over local IPC
    |
  vspwd
    |
local virtual topology or VSPW-TP/UDP bridge
```

## Initial transport

The unprivileged reference transport is:

```text
AF_UNIX + SOCK_SEQPACKET
```

`SOCK_SEQPACKET` is used because it preserves record boundaries, supports blocking/non-blocking operation and `poll()`, requires no IP configuration, and works in normal unprivileged CI.

One socket record carries exactly one VSPD frame.

A logical SpaceWire packet is **not** required to fit in one socket record. VSPD DATA has its own fragmentation fields because correctness must not depend on host socket-buffer sizes. The initial bounds are:

```text
maximum VSPD payload per record: 32 KiB
maximum logical DATA packet:     1 MiB
```

CUSE may later expose `/dev/vspwX`, but that is a presentation layer. It must not require a different backend/daemon protocol.

## Byte order and native-layout rule

All multi-byte VSPD fields are network byte order (big endian).

Native structures are never transmitted with `send(sizeof(struct))`. No pointer, file descriptor, `sockaddr`, `pollfd`, CUSE/FUSE handle, `size_t`, compiler padding, C++ type or process-local enum representation appears on the wire.

## Fixed 40-byte header

VSPD v1.1 uses the same fixed 40-byte header:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `VSPD`, `0x56535044` |
| 4 | 1 | version_major | `1` |
| 5 | 1 | version_minor | `1` |
| 6 | 1 | type | message type |
| 7 | 1 | flags | response/fragment/terminator bits |
| 8 | 2 | header_size | `40` |
| 10 | 2 | reserved | must be zero |
| 12 | 4 | payload_size | bytes following this header in this record |
| 16 | 4 | request_id | synchronous request correlation; zero for asynchronous events |
| 20 | 4 | port_id | daemon-local virtual-port identifier |
| 24 | 4 | message_id | logical DATA identifier; zero for non-DATA |
| 28 | 4 | fragment_offset | DATA payload offset in the logical packet |
| 32 | 4 | total_size | complete logical DATA payload size |
| 36 | 4 | status | fixed signed VSPD status code; zero outside responses |

Every received record must be exactly `header_size + payload_size` bytes. Trailing bytes, truncation, unknown types, unknown flag bits, non-zero reserved fields and inconsistent message shapes are rejected before state mutation.

## Flags

```text
0x01 RESPONSE
0x02 FRAGMENT_START
0x04 FRAGMENT_END
0x08 EOP
0x10 EEP
```

Fragment and terminator flags are valid only for DATA.

The final DATA fragment carries exactly one of EOP or EEP. Non-final fragments carry neither.

## Message types

| Value | Type | Direction / role |
|---:|---|---|
| 1 | `HELLO` | request/response protocol handshake |
| 2 | `ATTACH` | request/response attach client to one daemon port |
| 3 | `DETACH` | request/response detach |
| 4 | `START` | request/response link start |
| 5 | `STOP` | request/response link stop |
| 6 | `RESET` | request/response link reset |
| 7 | `GET_LINK_STATE` | request/response |
| 8 | `GET_CAPABILITIES` | request/response |
| 9 | `DATA_TX` | fragmented request, final logical acceptance response |
| 10 | `DATA_RX` | asynchronous fragmented event |
| 11 | `TIME_CODE_TX` | request/response |
| 12 | `TIME_CODE_RX` | asynchronous event |
| 13 | `GET_STATISTICS` | request/response |
| 14 | `CLEAR_STATISTICS` | request/response |
| 15 | `LINK_STATE_EVENT` | asynchronous event |
| 16 | `GET_SERVER_INFO` | non-owning management request/response |
| 17 | `GET_PORT_INFO` | non-owning management request/response |
| 18 | `GET_PORT_STATISTICS` | non-owning management request/response |
| 19 | `CLEAR_PORT_STATISTICS` | non-owning management request/response |

Synchronous requests use a non-zero `request_id`. Their response repeats the message type and request ID and sets `RESPONSE`.

Asynchronous events use `request_id == 0` and never receive a response.

## Status values

The VSPD status field has fixed signed 32-bit values:

| Value | Meaning |
|---:|---|
| 0 | OK |
| -1 | invalid argument |
| -2 | invalid state |
| -3 | timeout |
| -4 | unsupported |
| -5 | resource exhausted |
| -6 | link unavailable |
| -7 | buffer too small |
| -8 | invalid packet |
| -9 | backend/service error |

These intentionally correspond to the current public `spw_result_t` meanings, but VSPD defines them independently. Backend code performs an explicit translation rather than serializing a process-local typedef or enum.

A failed response has no payload in v1.0.

## HELLO

The first client request on a new connection is `HELLO`.

Payload, exactly four bytes:

```text
byte 0  major = 1
byte 1  minor = 1
byte 2  reserved = 0
byte 3  reserved = 0
```

The v1.1 codec currently requires an exact 1.1 match. Version-range negotiation can be introduced in a later protocol revision rather than inferred from native package versions.

Protocol version and SpWKit package/API version are intentionally independent.

## ATTACH and virtual-port identity

`port_id` is a daemon-local numeric port identifier. It is not a Linux file descriptor and is not the future `/dev/vspwX` minor number by ABI promise.

Initial v0.4 semantics:

- HELLO succeeds before ATTACH;
- one client attachment per virtual port;
- a second simultaneous attachment to the same exclusive port is rejected explicitly;
- DETACH or connection loss releases that client attachment deterministically;
- daemon configuration/topology decides which virtual ports are peers or bridges;
- application code does not configure private daemon routing through VSPD DATA operations.

VSPD 1.1 adds a separate non-owning management connection used by `spwctl`. A management client performs HELLO but never ATTACHes to a virtual port. It can discover daemon bounds, inspect per-port ownership/link/queue state, read per-port statistics, and clear statistics without displacing the application owner. Lifecycle overrides and topology mutation are deliberately not part of v1.1.

## Link-state semantics

The VSPD wire uses fixed state values corresponding to the public link model:

```text
0 ERROR_RESET
1 ERROR_WAIT
2 READY
3 STARTED
4 CONNECTING
5 RUN
```

Initial virtual-port behavior is intended to be:

```text
attach/reset             -> ERROR_RESET or READY according to reset completion
attached, not started    -> READY
start requested          -> STARTED / CONNECTING
peer available + started -> RUN
peer/service route lost  -> ERROR_WAIT
peer returns/re-attaches -> CONNECTING -> RUN
```

The backend must expose these states through `spw_port_get_link_state()`. `LINK_STATE_EVENT` lets `vspwd` notify the backend without requiring busy polling.

If the daemon connection itself disappears, application operations must not report success. The Linux backend will expose link/service unavailability through the normal result/state model. The v0.4 restart target is that a configured backend can reconnect, HELLO/ATTACH again, and recover through `CONNECTING -> RUN` without introducing a new application handle/API.

## DATA fragmentation

DATA_TX and DATA_RX carry arbitrary binary SpaceWire packet payloads.

All fragments belonging to one logical packet use the same:

- `port_id`;
- `message_id`;
- `total_size`;
- request ID for a DATA_TX logical request.

Rules:

- `message_id` is non-zero;
- `total_size <= 1 MiB`;
- `payload_size <= 32 KiB`;
- `fragment_offset + payload_size <= total_size`;
- START is present exactly when `fragment_offset == 0`;
- END is present exactly when the fragment reaches `total_size`;
- final fragment carries exactly one of EOP/EEP;
- non-final fragments carry no terminator;
- zero-length packets are represented by one zero-payload START+END frame with EOP or EEP;
- malformed metadata is rejected before reassembly state changes.

VSPD fragmentation is private IPC mechanics. `spw_port_receive()` still returns one complete logical packet.

### DATA_TX acknowledgement

A fragmented DATA_TX is one logical request. All fragments use the same request ID and message ID. `vspwd` sends one DATA_TX response after the complete logical packet has been validated and accepted into the destination/link path, or an error response if the logical request cannot be completed.

The daemon must not acknowledge individual fragments as successful SpaceWire packets.

### DATA_RX delivery

DATA_RX is asynchronous. The backend reassembles a complete logical packet before exposing it to the application.

If caller receive capacity is too small, the existing SpWKit rule remains authoritative:

- return `SPW_ERR_BUFFER_TOO_SMALL`;
- report the required complete payload length and terminator;
- do not partially modify caller payload storage;
- do not consume the complete logical packet.

## Time codes

TIME_CODE payload is exactly two bytes:

```text
time_count     u8, 0..63
control_flags  u8, 0..3
```

Current ordinary SpaceWire use expects control flags zero where required by the public API policy; VSPD preserves both bits rather than inventing a narrower transport shape.

## Capabilities payload

Successful `GET_CAPABILITIES` response payload is 24 bytes:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | capability bits |
| 8 | 4 | max packet size |
| 12 | 4 | TX queue depth |
| 16 | 4 | RX queue depth |
| 20 | 4 | buffer alignment |

The daemon/backend translation validates narrowing from public host `size_t` values into the fixed 32-bit VSPD fields. VSPD v1 therefore cannot advertise values larger than `UINT32_MAX`, well above the current logical packet limit.

## Statistics payload

Successful `GET_STATISTICS` response is nine network-order `u64` values, 72 bytes total:

1. TX packets
2. RX packets
3. TX bytes
4. RX bytes
5. TX time codes
6. RX time codes
7. EEP packets
8. link errors
9. dropped packets

`CLEAR_STATISTICS` has no request or success-response payload.

## Management payloads

`GET_SERVER_INFO` is valid after HELLO without ATTACH. Its 20-byte success payload is five network-order `u32` values: port count, client capacity, packet queue depth, time-code queue depth, and maximum logical packet size.

`GET_PORT_INFO` is also HELLO-only and uses the header `port_id` as the queried port. Its 16-byte success payload contains flags, link state, queued packet count, and queued time-code count. Flags report attached, started, reset-latched and ever-attached state.

`GET_PORT_STATISTICS` returns the normal 72-byte statistics payload for the selected port. `CLEAR_PORT_STATISTICS` clears those counters and returns no payload. These operations do not ATTACH, START, STOP, RESET, dequeue traffic, or alter application ownership.

## Blocking, non-blocking and `poll()` direction

VSPD does not encode POSIX `pollfd` or file-descriptor values.

The Linux backend uses socket readiness privately:

- immediate/non-blocking public operations use non-blocking IPC progress;
- finite public timeouts are translated to bounded `poll()`/socket waits;
- infinite waits may block until a relevant response/event or disconnect;
- socket disconnect wakes waiting operations and becomes service/link unavailability;
- queued asynchronous DATA/TIME_CODE/link-state events are processed while waiting for synchronous responses.

The backend cannot assume that the next readable record is the response it is waiting for; events and responses may interleave and are distinguished by type/request ID.

## Resource and safety rules

The daemon/backend implementation must remain bounded and validate before mutation:

- fixed maximum record payload;
- fixed maximum logical packet;
- bounded in-progress DATA reassembly per connection/port;
- bounded request tracking;
- no unbounded queue created merely because a client stops reading;
- malformed record or impossible fragmentation cannot partially modify an existing valid logical packet;
- client disconnect releases attachment and partial reassembly state;
- one client's malformed input cannot corrupt another virtual port's state.

## Test evidence for the protocol slice

The v0.4 protocol foundation includes pure-C tests for:

- exact golden 40-byte header bytes;
- HELLO request/success/error response shapes;
- EOP/EEP and zero-length DATA;
- multi-record DATA fragmentation metadata;
- capabilities/statistics/link-state payload codecs;
- invalid magic/version/flags/reserved fields;
- truncation and trailing-size mismatch;
- invalid request/event correlation;
- invalid time-code and link-state values;
- Linux `socketpair(AF_UNIX, SOCK_SEQPACKET)` record preservation;
- non-blocking empty receive;
- `poll()` readability and peer disconnect behavior.

The protocol codec itself is portable C and has no socket dependency. Only the Linux `SOCK_SEQPACKET` transport test depends on Linux/POSIX APIs.

## CUSE and `/dev/vspwX`

The intended user-facing Linux naming remains:

```text
/dev/vspw0   virtual SpaceWire
/dev/spw0    physical SpaceWire
```

The first v0.4 implementation does not require a kernel module or CUSE to function. The Unix-socket path establishes and tests semantics first.

If CUSE is adopted, it will map `/dev/vspwX` operations onto the same daemon/device model. It must not become a competing application API or change VSPD packet/link semantics.
