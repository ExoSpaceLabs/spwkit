# Port and simulator configuration

SpWKit applications select an implementation through `spw_port_config_t`. They do not call simulator, operating-system, DMA, or vendor APIs directly.

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
    +--> simulator backend (#4)
    +--> future Linux-device backend
    +--> future Ethernet backend
    +--> future embedded / hardware backend
```

## Common port configuration

`spw_port_config_t` contains only backend-independent fields:

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

Use `SPW_PORT_CONFIG_INITIALIZER(...)` to obtain deterministic v0.1 defaults.

`struct_size` and `version` provide an explicit extension contract. New fields may be appended in future versions without changing the common operation signatures. Unknown versions or flags are rejected rather than guessed.

The `backend_config` pointer is an input to `spw_port_open`. The v0.1 library reads it synchronously and does not retain the generic pointer after `open` returns. Backend-specific configuration structures must carry their own size/version fields when they are public ABI objects.

## Loopback

The loopback backend needs no backend-specific configuration:

```c
spw_port_config_t config =
    SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

spw_port_t* port = NULL;
spw_result_t result = spw_port_open(&config, &port);
```

Loopback is currently the first executable backend exposed through the public library API.

## Simulator

The simulator is selected through the same common configuration:

```c
spw_simulator_config_t sim = SPW_SIMULATOR_CONFIG_INITIALIZER;
sim.link_id = 42;
sim.endpoint = SPW_SIMULATOR_ENDPOINT_A;

spw_port_config_t config =
    SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
config.backend_config = &sim;
config.backend_config_size = sizeof(sim);
```

Two local peers are configured by using the same `link_id` and opposite endpoint values:

```text
Port A                          Port B
link_id = 42                    link_id = 42
endpoint = A                    endpoint = B
      |                              |
      +--------- virtual link -------+
```

A and B are labels only. They do not create server/client roles. Both sides are equal SpaceWire peers.

The configuration contract is active in v0.1. Until issue #4 implements the concrete two-peer simulator backend, `spw_port_open` validates simulator configuration and returns `SPW_ERR_UNSUPPORTED`. Issue #4 will replace that result with an instantiated simulator backend without changing the application-facing configuration shape.

## Backend isolation

The common configuration deliberately contains no:

- file descriptors;
- socket addresses;
- Ethernet addresses;
- DMA or physical addresses;
- AXI/MMIO types;
- RTOS handles;
- vendor SDK handles.

Those belong to backend-specific public extension structures or remain entirely internal to the backend.
