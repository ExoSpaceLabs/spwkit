# Transport-provider post-refactor evidence

## Scope

This record captures the first explicit performance evidence for the
transport-provider refactor tracked by #229 and #230.

It covers:

- direct transport-provider dispatch cost;
- the refactored VSPW/UDP path relative to immutable `v0.7.0`;
- current copied-buffer ownership through UDP and raw Ethernet;
- the remaining evidence that requires a real host raw-frame path or embedded
  MAC/DMA hardware.

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

The evidence supports two conclusions for the #229 abstraction itself:

1. provider dispatch has no measurable median cost in the current hosted
   microbenchmark;
2. the refactored UDP path retains the v0.7.0 performance envelope without a
   recurring UDP regression.

It does **not** yet answer how much real kernel raw Ethernet improves over UDP,
or how much embedded MAC/DMA/IRQ scheduling costs. Those require separate
carrier evidence.

## Remaining #230 evidence

Still required:

- PC raw Ethernet using a real host raw-frame path, compared under controlled
  conditions with UDP;
- PC -> embedded and embedded -> PC raw-Ethernet instrumentation;
- embedded -> embedded evidence when the hardware setup makes that useful;
- DMA setup/completion and IRQ-to-worker timing;
- physical-carrier throughput/jitter and any platform copy below
  `spw_raw_ethernet_io_ops_t`;
- evaluation of whether reducing the documented extra raw-Ethernet copies is
  worth extending the transport ownership contract.

Until those measurements exist, in-memory raw-frame results remain functional
and software-boundary evidence, not physical Ethernet performance evidence.
