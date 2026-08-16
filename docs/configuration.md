# Port and backend configuration

SpWKit applications select an implementation through `spw_port_config_t`. They do not call simulator, socket, VSPD, DMA, operating-system, or vendor APIs directly for normal SpaceWire operations.

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
    +--> Linux virtual-device backend -> VSPD -> vspwd
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

Current backend identifiers are:

```text
SPW_BACKEND_LOOPBACK
SPW_BACKEND_SIMULATOR
SPW_BACKEND_UDP
SPW_BACKEND_DEVICE
```

Backend availability is build/platform dependent. A public identifier/configuration can remain source-visible even when a particular runtime implementation is unsupported on that build.

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

## Linux virtual-device backend

The v0.4 hosted Linux device path uses `SPW_BACKEND_DEVICE` with `spw_device_config_t`:

```c
spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(0u);

spw_port_config_t config =
    SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
config.backend_config = &device;
config.backend_config_size = sizeof(device);

spw_port_t* port = NULL;
spw_result_t result = spw_port_open(&config, &port);
```

The initializer selects daemon port `0` and the development endpoint:

```text
/tmp/spwkit-vspwd.sock
```

The peer normally uses port `1`:

```c
spw_device_config_t peer = SPW_DEVICE_CONFIG_INITIALIZER(1u);
```

For a different daemon instance, copy a bounded socket-path string into `device.endpoint` before opening the port. `spw_port_open()` copies the backend configuration into the device context; the caller does not need to keep `spw_device_config_t` alive afterward.

The public configuration deliberately exposes only portable values:

- structure size/version;
- daemon virtual `port_id`;
- bounded endpoint path string.

It does **not** expose Unix file descriptors, `sockaddr_un`, VSPD framing structures, poll descriptors, or daemon-private objects.

The device backend performs VSPD HELLO/ATTACH internally and maps normal public operations onto the daemon:

- lifecycle and link state;
- copied DATA send/receive;
- EOP/EEP and zero-length packets;
- time codes;
- statistics/clear;
- reconnect/reattach after daemon/session loss during subsequent API service calls.

Logical packets may be up to the VSPD 1 MiB bound and are fragmented internally into 32 KiB daemon records. Fragmentation is never visible to applications.

If `spw_port_receive()` is given insufficient application storage, the backend returns `SPW_ERR_BUFFER_TOO_SMALL`, reports the required logical packet length/terminator, and retains the complete packet for a retry.

Zero-copy is not currently advertised by `SPW_BACKEND_DEVICE`; it remains capability-gated rather than emulated through a socket transport.

Build controls:

```text
SPWKIT_BUILD_DEVICE=ON    include the Linux hosted client backend when supported
SPWKIT_BUILD_VSPWD=ON     build/install the separate vspwd service
```

The public device headers/identifier remain available on unsupported platforms. Selecting the backend there returns `SPW_ERR_UNSUPPORTED`. Installed packages expose:

```cmake
SpWKit_DEVICE_RUNTIME_SUPPORTED
SpWKit_DEVICE_RUNTIME_SCOPE
```

The current runtime scope is `Linux`.

See `docs/vspwd.md` for service/recovery behavior and `docs/vspw-device-protocol.md` for the private VSPD contract.

## Distributed UDP backend

`SPW_BACKEND_UDP` uses `spw_udp_config_t`:

```c
spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(42000, 42001, 42);
udp.fragment_payload_size = SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD;
udp.ack_timeout_ms = SPW_UDP_DEFAULT_ACK_TIMEOUT_MS;
udp.max_retries = SPW_UDP_DEFAULT_MAX_RETRIES;
udp.keepalive_interval_ms = SPW_UDP_DEFAULT_KEEPALIVE_INTERVAL_MS;
udp.peer_timeout_ms = SPW_UDP_DEFAULT_PEER_TIMEOUT_MS;
udp.virtual_link_bps = SPW_UDP_DEFAULT_VIRTUAL_LINK_BPS;
udp.virtual_latency_us = SPW_UDP_DEFAULT_VIRTUAL_LATENCY_US;
udp.fault_seed = SPW_UDP_DEFAULT_FAULT_SEED;

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

### Virtual-link timing parameters

`virtual_link_bps`
: Effective simulated SpaceWire-side bit rate. `0` disables serialization delay, preserving the previous immediate behavior. DATA serialization charges the logical payload plus one terminator octet. TIME_CODE serialization charges its two-byte logical event. This is an effective logical timing model, not character- or signal-accurate PHY simulation.

`virtual_latency_us`
: Fixed propagation/processing latency added once to each logical DATA or TIME_CODE event. `0` disables fixed latency.

The virtual delay is applied before the first VSPW-TP transmission of a logical event. ACK, KEEPALIVE and reliability retransmissions are transport mechanisms and do not consume the SpaceWire-side timing budget. In particular, losing an ACK and retransmitting a packet does not serialize the same logical SpaceWire packet a second time in the timing model.

Caller send timeouts include the virtual delay. If the remaining timeout cannot cover the configured delay, `spw_port_send()` or `spw_port_send_time_code()` returns `SPW_ERR_TIMEOUT` before accepting the event into the reliable TX slot. Zero timing parameters preserve the previous behavior.

### Deterministic fault injection

Fault injection is disabled by default. `spw_udp_config_t` contains eight fixed `fault_rules`, copied into the backend at open time, plus a deterministic `fault_seed`. No dynamic rule storage or background worker is required.

Each rule contains:

- an `action`;
- a target VSPW-TP event class;
- `probability_per_10000` in the range 1..10000 for enabled rules;
- `max_events`, where zero means unlimited firings;
- `delay_us` for transport-delay rules only.

Rules are evaluated in array order. Each matching rule has its own PRNG stream derived from `fault_seed`. Therefore the same configuration and event stream reproduce the same injections. `probability_per_10000 = 10000` gives a deterministic always-fire rule, useful for CI and targeted tests.

Transport-side actions are:

- `SPW_UDP_FAULT_ACTION_TRANSPORT_DROP`;
- `SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE`;
- `SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER`;
- `SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY`.

The explicitly SpaceWire-visible action is `SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP`. It is valid only for DATA. Ordinary transport faults never synthesize EEP.

Fault-domain diagnostics are available through:

```c
spw_fault_statistics_t faults;
spw_port_get_fault_statistics(port, &faults);
```

Backends without fault injection return `SPW_ERR_UNSUPPORTED` from the fault-statistics operations.

### Cooperative progress

The UDP backend intentionally does not require a hidden worker thread. It retains at most one unacknowledged logical outbound event and advances ACK processing, retransmission and keepalive/liveness work when normal SpWKit calls service the port.

Blocking receive calls and `spw_port_get_link_state()` also service transport control traffic. The virtual timing wait follows the same rule and pumps peer/control traffic while the logical delay elapses. An application that performs no SpWKit calls at all does not run transport timers in the background.

### Hosted platform policy

The UDP runtime is currently POSIX-only. Linux is the primary fully exercised distributed platform and macOS is a supported second POSIX host. Native Windows/Winsock transport is tracked separately.

This does **not** remove the UDP public API on Windows. `SPW_BACKEND_UDP`, `spw_udp_config_t` and the normal `spw_port_*` entry points remain available from the same installed headers. A structurally valid UDP configuration on an unsupported build returns `SPW_ERR_UNSUPPORTED` when selected.

`SPWKIT_BUILD_UDP` controls whether the hosted implementation is included when the build platform supports it.

The installed CMake package exports `SpWKit_UDP_RUNTIME_SUPPORTED` and `SpWKit_UDP_RUNTIME_SCOPE`.

## Backend isolation

The mandatory common configuration and operation signatures deliberately contain no:

- file descriptors;
- socket objects;
- DMA or physical addresses;
- AXI/MMIO types;
- RTOS handles;
- vendor SDK handles.

Backend-specific public configuration may contain portable descriptive values such as numeric IP addresses, ports, virtual link identifiers, endpoint paths and timing/fault limits. Platform-native implementation handles remain internal.
