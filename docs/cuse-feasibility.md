# CUSE `/dev/vspwX` feasibility

> **Historical v0.4 design record.** This document records the feasibility decision that preceded the production implementation. v0.5 subsequently shipped `spwcuse`, a packet-record-oriented CUSE presenter with live `/dev/cuse` CI coverage. For current behavior, build instructions, record semantics and ownership rules, see [cuse.md](cuse.md).

## Decision

CUSE is technically suitable for creating an optional Linux character-device presentation for virtual SpaceWire ports, but **SpWKit must not expose a virtual SpaceWire port as an unframed byte stream**.

The v0.4 decision was therefore:

- CUSE/libfuse3 is a viable userspace mechanism for `/dev/vspwX`;
- no kernel module is justified at this stage;
- a character device must be packet-record oriented so SpaceWire DATA boundaries, EOP/EEP and time codes survive the file API;
- the record format was private/draft in v0.4 and was not yet installed as public ABI;
- the full presenter was deferred until its blocking-read, `poll()` and ownership model were implemented around an explicit event loop/broker rather than undocumented concurrent calls into `spw_port_*`.

Those constraints directly informed the v0.5 production `spwcuse` implementation.

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

Its data callbacks intentionally return `EOPNOTSUPP`. It exists to validate the CUSE plumbing, not to smuggle an unfinished device ABI into the historical v0.4 release.

## Why a raw byte stream is rejected

SpaceWire is packet oriented. The public SpWKit model preserves:

- one logical packet boundary per send/receive operation;
- EOP versus EEP termination;
- zero-length packets;
- time codes as a separate logical event type.

A UART-style `read()`/`write()` byte stream cannot represent those semantics without an out-of-band convention. Treating one `write()` system call as one packet is also not a stable ABI: callers, language runtimes and copying layers are free to split or aggregate writes.

Therefore `/dev/vspwX` must carry explicit records.

## Draft private record format

The v0.4 feasibility prototype used a fixed 16-byte big-endian header followed by a payload.

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

The codec lives under `src/cuse/` and was deliberately not installed during the feasibility stage. Its purpose was to force the design through deterministic golden/malformed tests before promotion to the production presenter.

## Required read/write behavior identified by the study

The feasibility work required each successful character-device operation to represent one complete record:

- `write()` accepts exactly one complete record; partial/multiple records in a single call are rejected;
- `read()` returns one complete record;
- if the caller buffer cannot hold the next record, return a size/error result without consuming that record;
- `poll()` reports readable when a DATA packet or time code is pending;
- packet/time-code readiness remains non-consuming until the successful `read()`;
- link/lifecycle/statistics control should use explicit ioctls only if/when a stable ioctl ABI is designed.

The production v0.5 implementation follows the packet-record/non-consuming readiness principles; see [cuse.md](cuse.md) for the current contract.

## Ownership and concurrency

The study rejected a thin CUSE wrapper that simply called one `SPW_BACKEND_DEVICE` port from arbitrary CUSE callback threads.

Reasons:

1. CUSE may issue simultaneous direct-I/O operations.
2. SpWKit does not promise that one `spw_port_t` is safe for arbitrary concurrent calls.
3. A single-threaded CUSE loop combined with an infinite blocking SpaceWire read would starve writes/control operations.
4. The VSPD Unix socket is already pollable internally, while `spw_port_wait()` intentionally hides native descriptors from applications.

The design therefore required explicit presenter ownership/event handling rather than accidental thread safety.

## CI boundary at the time

The v0.4 feasibility workflow validated pure-C record-codec tests, libfuse3 discovery, standalone probe compilation with `CXX=/bin/false`, and API-check execution. GitHub-hosted `/dev/cuse` availability was not assumed.

v0.5 later added production live-CUSE testing on runners/environments where `/dev/cuse` is explicitly available.

## v0.4 conclusion

The v0.4 investigation was positive: **userspace character devices are feasible and a kernel module is unnecessary for the simulator path**. That conclusion was acted on in v0.5 with `spwcuse`; this file remains as the design-history record rather than a statement of current implementation status.
