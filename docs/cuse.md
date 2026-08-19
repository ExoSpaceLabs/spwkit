# Linux CUSE `/dev/vspwX` presentation

SpWKit v0.5 adds an optional Linux CUSE presenter named `spwcuse`. It exposes one `vspwd` virtual SpaceWire port as a character device while keeping libfuse3 completely outside the portable `libspwkit` ABI and dependency surface.

```text
application
    |
/dev/vspwX
    |
libfuse3 CUSE callbacks
    |
spwcuse broker
    |
public SPW_BACKEND_DEVICE
    |
VSPD / AF_UNIX SOCK_SEQPACKET
    |
vspwd
```

`spwcuse` is a presentation/service process. Ordinary applications that already use `spw_port_*` should continue doing so; CUSE is useful when software needs a Linux device node rather than a linked SpWKit API.

## Build

CUSE support is opt-in:

```bash
cmake -S . -B build \
  -DSPWKIT_BUILD_DEVICE=ON \
  -DSPWKIT_BUILD_VSPWD=ON \
  -DSPWKIT_BUILD_CUSE=ON
cmake --build build
```

The CUSE target requires Linux, pthreads and libfuse3 development files. `libspwkit` itself does **not** link libfuse3.

A typical Ubuntu development host needs:

```bash
sudo apt install libfuse3-dev pkg-config fuse3
sudo modprobe cuse
```

The kernel must expose `/dev/cuse`. Container use additionally requires deliberately passing the CUSE device/capability through; the normal SpWKit runtime image does not gain host device access merely by containing `spwcuse`.

## Start a presenter

Run the daemon first:

```bash
vspwd --socket /tmp/mission-vspwd.sock
```

Then present daemon port 0 as `/dev/vspw0`:

```bash
spwcuse \
  --socket /tmp/mission-vspwd.sock \
  --port 0 \
  --device vspw0
```

`--device /dev/vspw0` is also accepted; only the final device entry name is passed to CUSE.

The presenter attaches and starts its VSPD port before serving the character device. That attachment is exclusive. While `spwcuse` owns port 0, a normal `SPW_BACKEND_DEVICE` application cannot attach to daemon port 0. It can still attach to another free port such as port 1.

Only one application may open one `spwcuse` device node at a time. A second `open()` receives `EBUSY`. This prevents two unrelated processes from racing for packet records on one SpaceWire endpoint.

## Record ABI v1

The device is **record-oriented**, not a UART-like byte stream. One successful `write()` contains exactly one complete record; one successful `read()` returns exactly one complete record.

All multibyte fields are big-endian. The fixed header is 16 bytes:

```text
offset  size  field
------  ----  --------------------------------------------
0       4     magic = 0x53505752 ("SPWR")
4       1     version = 1
5       1     type
6       1     flags
7       1     reserved = 0
8       4     payload_size
12      4     reserved = 0
```

Record types:

```text
1  DATA
2  TIME_CODE
```

### DATA

`payload_size` may be `0..1048576` bytes. The payload is one complete SpaceWire packet.

Flags:

```text
0x00  EOP
0x01  EEP
```

Zero-length EOP and EEP packets are valid records. Packet boundaries are never inferred from multiple reads or writes.

### TIME_CODE

A time-code record always has `payload_size = 2`, flags `0`, and payload:

```text
byte 0  time_count      0..63
byte 1  control_flags   low two bits only
```

Time codes are therefore sideband records rather than invented DATA bytes.

## Read semantics

CUSE uses direct I/O. SpWKit deliberately does not split a record to satisfy a short user buffer.

- if a complete record is available and the supplied read buffer is large enough, the complete record is returned and consumed;
- if the buffer is too small, `read()` fails with `EMSGSIZE` and the record remains pending;
- `poll()`/`select()` remains readable after that short-read error;
- an `O_NONBLOCK` read first performs one immediate backend readiness probe; it returns a complete already-ready record when VSPD has one, otherwise it returns `EAGAIN`;
- a blocking read remains pending until a complete record arrives.

The immediate nonblocking probe matters because an empty presenter-local queue is not proof that the daemon-side socket has no pending packet. The broker still remains event/request-driven: it does not continuously poll VSPD while no read or poll waiter exists.

Read readiness is level-triggered and non-consuming.

## Write semantics

Each `write()` must contain exactly one valid record header plus the declared payload. Malformed records are rejected without transmitting partial SpaceWire content.

The presenter serializes CUSE requests through one broker ownership domain. CUSE callbacks never issue concurrent calls against one `spw_port_t`. DATA is sent with the declared EOP/EEP terminator and TIME_CODE records use the ordinary public time-code API.

The internal broker queues are bounded. Saturation returns `EAGAIN` for a nonblocking write or `ENOBUFS` for a blocking write rather than growing memory without limit.

## Lifecycle and ioctls

v0.5 initially keeps lifecycle ownership simple:

- starting `spwcuse` attaches and starts the selected VSPD port;
- terminating `spwcuse` closes that public port and releases the daemon attachment;
- no START/STOP/RESET/statistics ioctl ABI is exposed yet.

This is deliberate. Any future ioctl interface must use a separately reviewed fixed-width Linux UAPI contract rather than leaking libfuse, native pointers, C enums or VSPD structs into applications.

## CI evidence

The dedicated CUSE workflow has two boundaries:

1. GCC/Clang pure-C compile/API/package tests with `CXX=/bin/false`, including proof that `libspwkit` has no libfuse dependency.
2. A live Linux CUSE test that explicitly loads the kernel `cuse` module, requires `/dev/cuse`, creates a real `/dev/vspw*` node, and exchanges DATA, EOP/EEP, zero-length DATA and a time code with an independent public `SPW_BACKEND_DEVICE` peer through `vspwd`.

The live test also verifies single-open ownership, empty nonblocking `EAGAIN`, direct nonblocking delivery of a record already pending in VSPD without a preceding `poll()`, `poll()` readiness, short-read `EMSGSIZE` without consumption, and a genuinely blocking read.

This remains software-device evidence. It is not physical SpaceWire signalling or FPGA/HIL interoperability evidence.
