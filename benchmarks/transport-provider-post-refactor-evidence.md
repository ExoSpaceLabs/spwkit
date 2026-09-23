# Transport-provider post-refactor evidence

## Scope

This record captures the first explicit performance evidence for the
transport-provider refactor tracked by #229 and #230.

It covers:

- direct transport-provider dispatch cost;
- the refactored VSPW/UDP path relative to immutable `v0.7.0`;
- current copied-buffer ownership through UDP and raw Ethernet;
- controlled Linux `AF_PACKET` raw-Ethernet behavior versus UDP on the same
  host/CPU;
- the remaining evidence that requires embedded MAC/DMA hardware.

It does **not** treat an in-memory frame fixture as physical or kernel raw
Ethernet evidence.

## Evidence identity

Hosted evidence workflow:

- workflow: `Transport provider evidence`;
- run: `35830578318`;
- benchmark-source head:
  `d79298adc6f3809719705b5faecd4b2117ea7954`;
- immutable UDP baseline:
  `v0.7.0` / `c2480bce2783d5f81ffc62cfd7fe95e36223c43b`;
- GitHub-hosted counter: x86 `RDTSCP`;
- UDP repetitions: 3;
- provider-dispatch repetitions: 3;
- provider-dispatch samples per repetition: 4096.

GitHub's pull-request benchmark checkout used a synthetic merge commit for the
candidate. The benchmark-source head above identifies the actual repository
changes under test.

## Transport-provider dispatch cost

The microbenchmark alternates two paths against the same no-op carrier
callback:

1. direct callback invocation;
2. `spw_transport_provider_send()` wrapper invocation.

The callback is marked non-inline and performs the same small observable state
update in both cases. Alternating order reduces directional host drift.

All three repetitions produced:

| Measurement | Run 1 | Run 2 | Run 3 |
|---|---:|---:|---:|
| Direct callback median | 78 ticks | 78 ticks | 78 ticks |
| Provider wrapper median | 78 ticks | 78 ticks | 78 ticks |
| Paired wrapper-direct median | 0 ticks | 0 ticks | 0 ticks |

This does **not** prove that provider dispatch has literally zero machine
instructions. It shows that the additional validation/function-pointer path is
below the median resolution of this hosted counter experiment. Relative to
VSPW processing and host-carrier costs measured in thousands of ticks, no
meaningful provider-dispatch cost is observable here.

## Refactored UDP versus v0.7.0

The same release-performance machinery used for release screening was rerun
with `v0.7.0` as the exact baseline and the post-refactor candidate as the
comparison target.

Parameters:

- warmup: 32 iterations;
- measured iterations: 128;
- payloads: 0, 64, 1024 and 4096 bytes;
- three independent hosted repetitions;
- same-runner paired baseline/candidate execution;
- native-relative UDP overhead retained as the compared metric.

### Repeated UDP result

No UDP TX or RX row reached the recurring-attention criterion.

| Path | Payload | Attention runs | Median delta | Observed range |
|---|---:|---:|---:|---:|
| UDP TX overhead | 0 B | 0/3 | -26.0 ticks | -172.0 .. +37.0 |
| UDP TX overhead | 64 B | 0/3 | -36.0 ticks | -123.5 .. +97.0 |
| UDP TX overhead | 1024 B | 0/3 | -50.5 ticks | -134.5 .. +13.0 |
| UDP TX overhead | 4096 B | 0/3 | -147.0 ticks | -893.5 .. +73.0 |
| UDP RX overhead | 0 B | 0/3 | +171.0 ticks | +61.0 .. +221.0 |
| UDP RX overhead | 64 B | 0/3 | +136.0 ticks | +98.0 .. +172.0 |
| UDP RX overhead | 1024 B | 0/3 | +159.0 ticks | +135.0 .. +196.0 |
| UDP RX overhead | 4096 B | 0/3 | +453.0 ticks | +244.5 .. +563.0 |

The comparator classified all eight UDP rows as `ok`. In the individual
screens, the positive RX changes remained below the configured relative
regression threshold; the 4096-byte RX sample was approximately two percent in
the first repetition.

Other benchmark families may produce hosted attention signals because the
release campaign deliberately runs many backends. Those rows do not traverse
the VSPW transport-provider path and are not evidence of provider/UDP
regression. The relevant UDP rows produced no recurring attention.

## Controlled host raw Ethernet versus UDP

A second evidence run exercised the public raw-Ethernet backend through real
Linux `AF_PACKET` sockets over an isolated veth pair and compared it with the
existing UDP benchmark on the same GitHub Actions runner and logical CPU.

Evidence:

- workflow run: `35849098636`;
- host raw-Ethernet job: `Host AF_PACKET raw Ethernet vs UDP`;
- artifact: `spwkit-host-carrier-evidence-35849098636`;
- host: Ubuntu 24.04 runner, Linux `6.17.0-1022-azure`, x86_64,
  Intel Xeon 6973P-C;
- counter: invariant x86 `RDTSCP`;
- CPU affinity: logical CPU 0;
- isolated veth interfaces with fixed locally-administered MAC addresses;
- raw carrier: Linux `AF_PACKET/SOCK_RAW`;
- raw development EtherType: `0x88B5`;
- native raw comparator EtherType: `0x88B6`;
- raw fragment payload: 1400 bytes;
- warmup: 32;
- iterations: 128;
- repetitions: 3;
- payloads: 0, 64, 1024 and 4096 bytes.

The native raw comparator uses the same number of 1400-byte carrier frames as
the VSPW path for a given logical payload. In particular, the 4096-byte case is
three carrier frames in both paths; it is not compared with one impossible
1500-MTU jumbo frame.

Median-of-three results:

| Dir | Payload | Raw native | Raw SpWKit | Raw delta | UDP native | UDP SpWKit | UDP delta |
|---|---:|---:|---:|---:|---:|---:|---:|
| TX | 0 B | 2668 | 5609 | +2954 | 3249 | 7408 | +4174 |
| TX | 64 B | 2757 | 5683 | +2905 | 3326 | 7435 | +4110 |
| TX | 1024 B | 2979 | 5796 | +2825 | 3368 | 7546 | +4191 |
| TX | 4096 B | 8270 | 11132 | +2859 | 3712 | 18228 | +14580 |
| RX | 0 B | 1619 | 15379 | +13760 | 812 | 6562 | +5742 |
| RX | 64 B | 1633 | 15212 | +13574 | 905 | 6588 | +5680 |
| RX | 1024 B | 1702 | 15504 | +13802 | 956 | 6666 | +5703 |
| RX | 4096 B | 4817 | 29391 | +24574 | 1146 | 12426 | +11289 |

The result is directional rather than a blanket "raw Ethernet is faster"
claim:

- **TX:** the current raw-Ethernet path is faster than UDP for all four tested
  payloads. At 4096 bytes the SpWKit median is 11132 ticks versus 18228 for
  UDP, while the native comparator correctly pays for three raw frames.
- **RX:** the current copied raw-Ethernet path is slower than UDP for all four
  payloads. The raw path performs an additional SpWKit-owned decapsulation
  copy before the VSPW RX buffer, matching the copy audit below; `AF_PACKET`
  receive mechanics also cost more than loopback UDP on this host.
- **Jitter:** raw TX p95 remains close to its median
  (5788/5848/5992/11348 ticks for 0/64/1024/4096 B), but raw RX is visibly
  noisier (p95 21988/21344/21788/36522 ticks). Median raw-RX standard deviation
  across the three repetitions is approximately
  1998/2771/2096/4261 ticks, compared with UDP RX
  204/83/411/485 ticks.

These are controlled hosted software/carrier measurements, not physical
Ethernet or SpaceWire timing. They demonstrate that removing IP/UDP is not by
itself sufficient to make every direction faster: the current RX copy and
host raw-socket path dominate enough to reverse the expected advantage.

### Raw-Ethernet framing correction exposed by the host path

The real Ethernet audit also exposed a correctness issue that an in-memory
frame fixture could not reproduce. The original development envelope had no
explicit VSPW-frame length. A VSPW KEEPALIVE produced a 58-byte Ethernet frame
excluding FCS, so a real Ethernet implementation could add minimum-frame
padding and the receiver could mistake those padding bytes for VSPW data.

The development framing is therefore version 2.0 in this PR and carries an
explicit 16-bit VSPW-frame length after the subtype/version fields. The
receiver treats that declared length as authoritative and ignores trailing
Ethernet padding. With the two-byte length field, a KEEPALIVE occupies exactly
60 bytes excluding FCS.

## Copy and ownership audit

The current provider contract is intentionally copied-I/O. The counts below
describe SpWKit-owned payload copies and separate them from kernel, DMA or
platform-driver copies that occur below the provider callback.

### UDP TX

For a logical packet:

1. application buffer -> VSPW pending-TX packet;
2. pending-TX packet -> VSPW carrier-frame fragment buffer;
3. the UDP provider passes that carrier buffer to `sendto()`.

Therefore the current VSPW/UDP path performs **two SpWKit payload copies** per
payload byte before the operating-system socket boundary. Kernel/network-stack
copies are outside this count.

### UDP RX

The UDP provider receives directly into the VSPW RX carrier buffer.

For an unfragmented packet:

1. VSPW RX carrier payload -> pending application packet;
2. pending packet -> caller receive buffer.

That is **two SpWKit payload copies**.

For a fragmented packet:

1. each carrier payload -> reassembly buffer;
2. completed reassembly buffer -> pending application packet;
3. pending packet -> caller receive buffer.

That is **three SpWKit payload copies** for the completed payload.

### Raw-Ethernet TX

The current raw-Ethernet carrier adds Ethernet framing in its own bounded
frame buffer:

1. application buffer -> VSPW pending-TX packet;
2. pending packet -> VSPW carrier-frame fragment;
3. VSPW carrier frame -> raw Ethernet frame buffer;
4. platform `send_frame` receives the completed Ethernet frame.

This is **three SpWKit payload copies** before the platform frame-I/O boundary,
one more than UDP. A board binding may then copy again into a DMA buffer, or it
may arrange for the supplied frame to be consumed directly; that cost is
platform-specific and must be measured on hardware.

### Raw-Ethernet RX

The platform fills the raw-Ethernet provider frame buffer.

For an unfragmented VSPW packet:

1. Ethernet payload -> VSPW RX carrier buffer during decapsulation;
2. VSPW RX payload -> pending application packet;
3. pending packet -> caller receive buffer.

This is **three SpWKit payload copies**, one more than UDP.

For a fragmented packet:

1. Ethernet payload -> VSPW RX carrier buffer;
2. VSPW fragment payload -> reassembly buffer;
3. completed reassembly -> pending application packet;
4. pending packet -> caller receive buffer.

This is **four SpWKit payload copies**.

If a board driver first copies DMA-owned data into the buffer supplied to
`receive_frame`, that is an additional platform copy and is not hidden in the
SpWKit count.

## Ownership implications

The raw-Ethernet provider is therefore architecturally correct but not yet
copy-optimal. The obvious future optimization points are:

- transport TX support for caller-provided headroom or scatter/gather so the
  Ethernet header need not force a full VSPW-frame copy;
- transport RX views/ownership transfer so decapsulation can expose the VSPW
  payload without copying it into a second carrier buffer;
- direct reassembly into final application-visible storage where the public
  ownership contract can safely permit it.

Those changes should be justified by host/embedded measurements rather than
added speculatively. The existing copied contract remains deterministic and
portable.

## Interpretation

The evidence supports three conclusions for the #229 abstraction itself:

1. provider dispatch has no measurable median cost in the current hosted
   microbenchmark;
2. the refactored UDP path retains the v0.7.0 performance envelope without a
   recurring UDP regression;
3. the same VSPW engine runs over a real host raw-frame carrier without a
   protocol-logic fork, and the carrier comparison exposes a concrete
   optimization target: raw TX benefits from bypassing UDP/IP while the
   current copied raw RX path is slower and noisier than loopback UDP.

Embedded MAC/DMA/IRQ scheduling still requires separate target evidence.

## Remaining #230 evidence

Still required:

- PC -> embedded and embedded -> PC raw-Ethernet instrumentation;
- embedded -> embedded evidence when the hardware setup makes that useful;
- DMA setup/completion and IRQ-to-worker timing;
- physical-carrier throughput/jitter and any platform copy below
  `spw_raw_ethernet_io_ops_t`;
- evaluation of whether reducing the documented extra raw-Ethernet copies is
  worth extending the transport ownership contract.

The AF_PACKET/veth result is real host raw-frame evidence, but it is still not
physical Ethernet or embedded-driver evidence. In-memory raw-frame results
remain functional/software-boundary evidence only.
