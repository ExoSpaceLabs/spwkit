# Distributed VSPW-TP/UDP peer example

This is a standalone consumer of an **installed** SpWKit package. It demonstrates that distributed applications use the same public `spw_port_*` API as local/simulator applications; VSPW-TP framing and sockets remain backend internals.

The executable is an equal peer, not a client or server. Each instance configures its own local UDP endpoint plus the expected peer endpoint.

## Build against an installed package

```bash
cmake -S . -B build \
  -DCMAKE_INSTALL_PREFIX="$PWD/install" \
  -DSPWKIT_BUILD_TESTS=OFF
cmake --build build --parallel
cmake --install build

cmake -S examples/distributed -B build-distributed \
  -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build build-distributed --parallel
```

The example currently requires a POSIX host because the v0.2 UDP runtime backend is POSIX-only.

## One-host, two-process example

Start peer A in one terminal:

```bash
./build-distributed/spwkit_udp_peer \
  --id A \
  --local-port 42000 \
  --remote-port 42001 \
  --link-id 42
```

Start peer B in another terminal:

```bash
./build-distributed/spwkit_udp_peer \
  --id B \
  --local-port 42001 \
  --remote-port 42000 \
  --link-id 42
```

Both processes wait for the link to reach `SPW_LINK_RUN`, then each sends and receives an 8 KiB packet and a time code. A sends an EOP packet and B sends an EEP packet, so the exchange also demonstrates terminator preservation across VSPW-TP fragmentation.

Startup order is intentionally irrelevant. A peer may remain in `SPW_LINK_CONNECTING` until the other process starts.

## Two Linux hosts

Assume host A is `192.0.2.10` and host B is `192.0.2.20`, with UDP port 42000 permitted between them.

Host A:

```bash
./spwkit_udp_peer \
  --id A \
  --local-address 192.0.2.10 \
  --remote-address 192.0.2.20 \
  --local-port 42000 \
  --remote-port 42000 \
  --link-id 42
```

Host B:

```bash
./spwkit_udp_peer \
  --id B \
  --local-address 192.0.2.20 \
  --remote-address 192.0.2.10 \
  --local-port 42000 \
  --remote-port 42000 \
  --link-id 42
```

Each endpoint validates the configured remote IPv4 address, UDP source port and `link_id`. NAT/firewall policy must therefore preserve or explicitly match the configured endpoints.

## Restart scenarios

The `--scenario` option exists so integration tests and users can exercise liveness/session recovery without private transport controls:

- `single`: one full-duplex packet/time-code exchange, then exit;
- `initial`: perform round 1, then exit so the peer can observe loss;
- `survivor`: perform round 1, wait for `SPW_LINK_ERROR_WAIT`, wait for the peer to return with a new session, then perform round 2;
- `restart`: join an existing survivor as the restarted peer and perform round 2.

`tests/d2d/run_multi_process.sh` orchestrates the restart scenario as two independent localhost processes. `tests/d2d/run_netns.sh` repeats the same scenario inside two Linux network namespaces connected by a 1500-byte-MTU veth pair.

The namespace script requires `iproute2` plus privileges to create network namespaces (`root` or equivalent `CAP_NET_ADMIN`; the CI workflow uses passwordless `sudo` on the hosted Linux runner).

## What this proves

The example and integration harness demonstrate:

- no shared application process or simulator memory is required;
- applications use only installed public SpWKit headers/API;
- peers are symmetric rather than server/client;
- packets larger than transport MTU preserve one SpaceWire packet boundary;
- EOP/EEP and time codes survive the process/network boundary;
- peer loss maps to public link state;
- a restarted process establishes a new transport session and recovers the same public port on the survivor.
