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

Two local peers use the same `link_id` and opposite endpoint values:

```text
Port A                          Port B
link_id = 42                    link_id = 42
endpoint = A                    endpoint = B
      |                              |
      +--------- virtual link -------+
```

A and B are labels only. They do not create server/client roles. Both sides are equal SpaceWire peers.

The simulator backend is implemented and supports packet transfer, EOP/EEP, time codes, link lifecycle/recovery, statistics, bounded queues and zero-copy ownership emulation.

## Distributed UDP backend

Current `main` contains the first v0.2 distributed backend. Select it with `SPW_BACKEND_UDP` and provide `spw_udp_config_t`:

```c
spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER;
udp.local_address = "127.0.0.1";
udp.remote_address = "127.0.0.1";
udp.local_port = 42000;
udp.remote_port = 42001;
udp.link_id = 42;
udp.fragment_payload_size = 1200;

spw_port_config_t config =
    SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
config.backend_config = &udp;
config.backend_config_size = sizeof(udp);
```

The peer uses the opposite local/remote port assignment and the same `link_id`.

```text
Application A                         Application B
    |                                    |
libspwkit                            libspwkit
    |                                    |
UDP :42000 ------------------------> UDP :42001
UDP :42000 <------------------------ UDP :42001
          VSPW-TP, same link_id
```

The public configuration describes peer transport parameters, but applications still send and receive through `spw_port_send`, `spw_port_receive`, and the other normal SpWKit operations.

The current POSIX backend supports:

- IPv4 UDP transport;
- VSPW-TP v1 framing;
- bounded fragmentation/reassembly;
- EOP/EEP preservation;
- time-code transfer;
- receive timeouts and statistics;
- up to 1 MiB logical packet payload in this backend;
- default 1200-byte fragment payload.

Windows retains the public backend identifier/configuration ABI but currently reports the UDP backend as unsupported until a Winsock implementation is added.

ACK/retransmission, keepalive/disconnect detection, configurable latency/rate and deterministic fault injection remain v0.2 work.

## Backend isolation

The mandatory common configuration and operation signatures deliberately contain no:

- file descriptors;
- socket objects;
- DMA or physical addresses;
- AXI/MMIO types;
- RTOS handles;
- vendor SDK handles.

Backend-specific public configuration may contain portable descriptive values such as numeric/string IP addresses, ports, a virtual link identifier or sizing limits when that is required to select the backend. Implementation handles and platform-native types remain internal.
