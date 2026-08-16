# spwmon

`spwmon` is the passive observation tool for the Linux `vspwd` virtual SpaceWire service.

It connects through the private VSPD management plane, performs HELLO, and subscribes to daemon port snapshots. It **never ATTACHes** to a SpaceWire port, so it cannot displace the application that owns that port.

```text
application -> SPW_BACKEND_DEVICE -> VSPD ATTACH -> vspwd
                                               ^
                                               |
                           HELLO + SUBSCRIBE ---+--- spwmon
```

## Usage

Monitor every daemon port until interrupted:

```bash
spwmon
```

Monitor one port:

```bash
spwmon --port 0
```

Use a non-default daemon endpoint:

```bash
spwmon --socket /tmp/mission-vspwd.sock --port 1
```

Emit JSON Lines for scripts and log ingestion:

```bash
spwmon --json
```

Bound the number of emitted snapshots, which is also useful for tests:

```bash
spwmon --port 0 --count 5 --json
```

`SIGINT` and `SIGTERM` stop continuous monitoring cleanly.

## Snapshot contents

Each event contains only daemon metadata:

- port identity;
- attached, started, reset-latched and ever-attached flags;
- link state;
- packet and time-code queue occupancy;
- TX/RX packet and byte counters;
- TX/RX time-code counters;
- EEP, link-error and dropped-packet counters.

`spwmon` does not receive or print application packet payloads.

## Event behavior

Subscribing to a port immediately queues its current snapshot. Later observable changes mark that port dirty. While an event is still pending, repeated changes are coalesced into the latest snapshot for that port rather than appended to an unbounded queue.

The Unix `SOCK_SEQPACKET` transport is itself bounded as well. If a monitor stops reading, `vspwd` retains pending state and retries when the socket becomes writable; it does not allocate an ever-growing event backlog.

The current daemon tracks at most 32 subscription bits per management connection. The v0.4 reference topology exposes two ports, so this is a deliberately bounded implementation detail rather than a public ABI promise.

## Ownership boundary

Observation is intentionally weaker than administration. `spwmon` cannot:

- ATTACH or DETACH an application port;
- START, STOP or RESET a link;
- mutate topology;
- consume SpaceWire packets or time codes;
- clear statistics.

Use `spwctl` for explicit management queries and statistic clearing. Application lifecycle remains owned by the application using the public `spw_port_*` API.
