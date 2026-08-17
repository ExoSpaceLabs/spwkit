# CUSE `/dev/vspwX` feasibility

## Decision

CUSE is technically suitable for creating an optional Linux character-device presentation for virtual SpaceWire ports, but **SpWKit must not expose a virtual SpaceWire port as an unframed byte stream**.

The v0.4 decision is therefore:

- CUSE/libfuse3 is a viable userspace mechanism for `/dev/vspwX`;
- no kernel module is justified at this stage;
- a future character device must be packet-record oriented so SpaceWire DATA boundaries, EOP/EEP and time codes survive the file API;
- the record format remains private/draft in v0.4 and is not installed as public ABI yet;
- the full presenter is deferred until its blocking-read, `poll()` and ownership model are implemented around an explicit event loop/broker rather than undocumented concurrent calls into `spw_port_*`.

## Why CUSE is mechanically viable

libfuse3 CUSE provides character-device callbacks for `open`, direct-I/O `read`/`write`, `ioctl` and `poll`. A userspace process can therefore create a Linux character device without a custom kernel module.

The repository contains `spwcuse_probe`, an opt-in standalone compile/runtime probe under `tools/cuse-probe`. It links only to libfuse3 and does not add FUSE discovery or linkage to the main SpWKit CMake project or `libspwkit`.

```sh
cmake -S tools/cuse-probe -B build-cuse-probe \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-cuse-probe --parallel
build-cuse-probe/spwcuse_probe --api-check
```

When the host exposes `/dev/cuse` and permits CUSE device creation, the probe can create a temporary character device:

```sh
sudo build-cuse-probe/spwcuse_probe --serve spwkit-cuse-probe
ls -l /dev/spwkit-cuse-probe
```

Its data callbacks intentionally return `EOPNOTSUPP`. It exists to validate the CUSE plumbing, not to smuggle an unfinished device ABI into the release.

## Why a raw byte stream is rejected

SpaceWire is packet oriented. The public SpWKit model preserves:

- one logical packet boundary per send/receive operation;
- EOP versus EEP termination;
- zero-length packets;
- time codes as a separate logical event type.

A UART-style `read()`/`write()` byte stream cannot represent those semantics without an out-of-band convention. Treating one `write()` system call as one packet is also not a stable ABI: callers, language runtimes and copying layers are free to split or aggregate writes.

Therefore `/dev/vspwX` must carry explicit records.

## Draft private record format

The v0.4 feasibility prototype uses a fixed 16-byte big-endian header followed by a payload.

```text
offset size field
0      4    magic = "SPWR" / 0x53505752
4      1    version = 1
5      1    type
6      1    flags
7      1    reserved = 0
8      4    payload_size
12     4    reserved = 0
```

Record types:

```text
1 DATA
2 TIME_CODE
```

For `DATA`:

- payload size is `0 .. 1 MiB`;
- flag bit 0 means EEP;
- flag bit 0 clear means EOP;
- zero-length EOP/EEP DATA records are valid.

For `TIME_CODE`:

- payload size is exactly 2 bytes;
- byte 0 is the six-bit time count (`0..63`);
- byte 1 contains the two control bits (`0..3`);
- flags are zero.

The codec lives under `src/cuse/` and is deliberately not installed. Its purpose is to force the design through deterministic golden/malformed tests before deciding whether this should become a stable native-device ABI.

## Required read/write behavior for a production presenter

A production CUSE implementation should treat each successful character-device operation as one complete record:

- `write()` accepts exactly one complete record; partial/multiple records in a single call are rejected;
- `read()` returns one complete record;
- if the caller buffer cannot hold the next record, return a size/error result without consuming that record;
- `poll()` reports readable when a DATA packet or time code is pending;
- packet/time-code readiness remains non-consuming until the successful `read()`;
- link/lifecycle/statistics control should use explicit ioctls only if/when a stable ioctl ABI is designed.

This preserves the same logical semantics as `spw_port_receive()` and `spw_port_wait()` instead of making the character device a second, incompatible interpretation of SpaceWire.

## Ownership and concurrency

A thin CUSE wrapper that simply calls the current `SPW_BACKEND_DEVICE` from arbitrary CUSE callback threads is rejected for now.

Reasons:

1. CUSE may issue simultaneous direct-I/O operations.
2. SpWKit does not currently promise that one `spw_port_t` is safe for concurrent operations from unrelated host threads.
3. A single-threaded CUSE loop combined with an infinite blocking SpaceWire read would starve writes and control operations.
4. The VSPD Unix socket is already pollable internally, while `spw_port_wait()` intentionally hides native descriptors from applications.

The production architecture should therefore use one explicit owner/event loop for the virtual port, integrating CUSE request completion with VSPD/daemon readiness. Two viable directions are:

```text
CUSE callbacks -> presenter broker/event loop -> VSPD -> vspwd
```

or, after daemon refactoring:

```text
CUSE presentation integrated beside VSPD clients inside vspwd
```

The first direction keeps CUSE as an optional process. The second can avoid an extra local hop but couples libfuse to the Linux daemon build. Both preserve `libspwkit` as FUSE-free.

## CI boundary

The CUSE workflow always validates:

- pure-C record codec tests under GCC and Clang;
- `libfuse3` discovery through `pkg-config`;
- compilation/linking of the standalone CUSE probe with `CXX=/bin/false`;
- `spwcuse_probe --api-check`.

GitHub-hosted runners are not treated as proof that the host kernel exposes `/dev/cuse`. The workflow reports whether the device exists but does not turn a hosted-runner kernel feature into a release requirement.

Real `/dev/vspwX` behavior should eventually be exercised on a runner/container/VM where `/dev/cuse` is explicitly provided.

## v0.4 conclusion

The CUSE investigation is positive: **userspace character devices are feasible and a kernel module is unnecessary for the simulator path**. The full native-device presenter should be a later implementation slice with a deliberately reviewed record/ioctl ABI and asynchronous event-loop ownership.
