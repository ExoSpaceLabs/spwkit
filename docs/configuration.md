# Port and backend configuration

SpWKit applications select an implementation through `spw_port_config_t`. They do not call simulator, socket, DMA, operating-system, or vendor APIs directly for normal SpaceWire operations.

```text
Application
    |
    v
spw_port_open(config)
    |
    v
libspwkit
    |
    +--> loopback backend
    +--> process-local simulator backend
    +--> VSPW-TP / UDP distributed backend
    +--> future Linux-device backend
    +--> future embedded / hardware backend
```

## Common port configuration

`spw_port_config_t` contains backend-independent dispatch fields:

```c
struct spw_port_config {
    uint32_t struct_size;
    uint32_t version;
    spw_backend_id_t backend;
    uint32_t flags;
    const void* backend_config;
    size_t backend_config_size;
};
```

Use `SPW_PORT_CONFIG_INITIALIZER(...)` to obtain deterministic defaults.

`struct_size` and `version` provide an explicit extension contract. Unknown versions or unsupported flags are rejected rather than guessed.

The `backend_config` pointer is consumed synchronously while the port is opened. Public backend-specific configuration structures carry their own size/version contract when necessary.

## Loopback

The loopback backend needs no backend-specific configuration:

```c
spw_port_config_t config =
    SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

spw_port_t* port = NULL;
spw_result_t result = spw_port_open(&config, &port);
```

Loopback is a deterministic single-port reference backend used heavily by the shared contract suite.

## Process-local simulator

The process-local simulator is selected through the same common configuration:

```c
spw_simulator_config_t sim = SPW_SIMULATOR_CONFIG_INITIALIZER;
sim.link_id = 42;
sim.endpoint = SPW_SIMULATOR_ENDPOINT_A;

spw_port_config_t config =
    SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
config.backend_config = &sim;
config.backend_config_size = sizeof(sim);
```

Two local peers use the same `link_id` and opposite endpoint values. A/B are labels only; they do not create server/client roles.

The simulator backend supports packet transfer, EOP/EEP, time codes, link lifecycle/recovery, statistics, bounded queues and zero-copy ownership emulation.

## Distributed UDP backend

Current v0.2 development uses `SPW_BACKEND_UDP` with `spw_udp_config_t`:

```c
spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(42000, 42001, 42);
udp.fragment_payload_size = SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD;
udp.ack_timeout_ms = SPW_UDP_DEFAULT_ACK_TIMEOUT_MS;
udp.max_retries = SPW_UDP_DEFAULT_MAX_RETRIES;
udp.keepalive_interval_ms = SPW_UDP_DEFAULT_KEEPALIVE_INTERVAL_MS;
udp.peer_timeout_ms = SPW_UDP_DEFAULT_PEER_TIMEOUT_MS;

spw_port_config_t config =
    SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
config.backend_config = &udp;
config.backend_config_size = sizeof(udp);
```

The initializer uses numeric localhost (`127.0.0.1`) for both addresses. For another numeric IPv4 peer, copy the address strings into `local_address` and `remote_address` before opening the port. DNS resolution is deliberately not part of the current backend contract.

The opposite peer swaps the local/remote ports and uses the same `link_id`:

```c
spw_udp_config_t udp_peer = SPW_UDP_CONFIG_INITIALIZER(42001, 42000, 42);
```

The backend validates incoming source address, source port and `link_id`; a matching link ID from an unrelated UDP source is not accepted as the configured peer.

### Reliability parameters

`fragment_payload_size`
: Maximum VSPW-TP DATA payload per UDP datagram. Default: 1200 bytes. The current backend accepts values from 256 bytes through the VSPW-TP UDP maximum.

`ack_timeout_ms`
: Time before an unacknowledged logical DATA/TIME_CODE event becomes eligible for retransmission while the backend is being serviced. Default: 100 ms.

`max_retries`
: Maximum complete logical-message retransmissions after the initial transmission. Default: 5.

`keepalive_interval_ms`
: Interval between transport KEEPALIVE advertisements while API calls are servicing the backend. Default: 1000 ms.

`peer_timeout_ms`
: Time without valid traffic from the configured peer before the backend maps the peer to `SPW_LINK_ERROR_WAIT`. Default: 3000 ms. It must be greater than the keepalive interval.

### Cooperative progress

The UDP backend intentionally does not require a hidden worker thread. It retains at most one unacknowledged logical outbound event and advances ACK processing, retransmission and keepalive/liveness work when normal SpWKit calls service the port.

Blocking receive calls and `spw_port_get_link_state()` also service transport control traffic. An application that performs no SpWKit calls at all does not run transport timers in the background. This keeps the design portable to future bare-metal and RTOS adapters instead of quietly making POSIX threads a dependency.

A successful `spw_port_send()` or `spw_port_send_time_code()` means the event was accepted into the backend's bounded reliable TX slot and its first transmission completed. It does not mean the remote application has consumed the event.

If a second send arrives while the reliable TX slot is still occupied, the backend services ACK/retry traffic up to the caller timeout. Retry exhaustion maps the link to `SPW_LINK_ERROR_WAIT` and subsequent service-dependent operations report `SPW_ERR_LINK_UNAVAILABLE` until valid peer traffic/acknowledgement recovers the link.

### Current POSIX feature set

- IPv4 UDP transport;
- VSPW-TP v1 framing;
- bounded fragmentation/reassembly;
- EOP/EEP preservation;
- reliable logical DATA and TIME_CODE delivery with bounded retry;
- duplicate logical-message suppression;
- peer session/keepalive tracking and restart recovery;
- peer-loss mapping into public link state/errors;
- receive timeouts and statistics;
- up to 1 MiB logical packet payload;
- default 1200-byte fragment payload.

Windows retains the public backend identifier/configuration ABI but currently reports the UDP backend as unsupported until a Winsock implementation is added.

Configurable virtual rate/latency, deterministic fault injection and capture/Wireshark tooling remain later v0.2 work.

## Backend isolation

The mandatory common configuration and operation signatures deliberately contain no:

- file descriptors;
- socket objects;
- DMA or physical addresses;
- AXI/MMIO types;
- RTOS handles;
- vendor SDK handles.

Backend-specific public configuration may contain portable descriptive values such as numeric IP addresses, ports, virtual link identifiers and timing/sizing limits. Platform-native implementation handles remain internal.
