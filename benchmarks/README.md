# SpWKit profiling benchmark

This directory contains the performance-characterization framework for SpWKit's compile-time paired probes.

The benchmark characterizes **software-layer overhead** in architectural counter ticks/cycles. It is not a SpaceWire PHY benchmark. FPGA/controller latency and physical end-to-end link performance are separate measurement domains.

GitHub-hosted timing is useful for mechanics and regression evidence, but it is not an authoritative performance result.

## Recommended local campaign

Use the centralized campaign for normal profiling work:

```bash
benchmarks/run_profile_campaign.sh
```

The default hosted campaign uses:

- `Release` builds;
- 256 warmup iterations;
- 1024 measured iterations per payload;
- payloads `0 1 8 64 256 1024 4096` bytes;
- one-second settle time after each clean build;
- all supported copied-DRIVER TX layer ranges;
- a direct/native versus SpWKit comparison after the layer measurements.

Every compiled measurement configuration is built and executed strictly one at a time. Each case receives a fresh build directory and fresh CMake configure/build. No official campaign reuses a CMake cache or object files from another probe configuration.

Results are stored under a self-identifying directory:

```text
build/profile-results/
└── <type>-<UTC timestamp>-<short commit>/
    ├── campaign.json
    ├── results.jsonl
    ├── calibration.jsonl
    ├── cases/
    ├── calibration/
    └── comparison/
        ├── tx_api_native.jsonl
        └── calibration.json
```

For example:

```text
build/profile-results/host-20260908T143443Z-5df350c3/
```

The default local result type is `host`. GitHub Actions uses `github-hosted`; physical target campaigns should use a target-specific type such as `stm32h755`.

The campaign metadata repeats the result type, UTC timestamp, full commit SHA, short SHA and directory name so copied/archived result sets remain self-identifying.

## Layer profiling ranges

The copied-DRIVER TX layer ranges are:

- `tx_api_backend`
- `tx_backend_provider`
- `tx_provider_native`
- `tx_api_native`

Each range is a separate compile-time paired-probe configuration. Exactly two probes are active in a benchmark build; all other profiling probes compile out.

To run one range directly:

```bash
benchmarks/run_profile_benchmark.sh \
  --range tx_api_native \
  --warmup 256 \
  --iterations 1024 \
  --payloads "0 1 8 64 256 1024 4096"
```

The default maximum measured sample count is 4096 and the default maximum payload is 4096 bytes. Sample storage is fixed-size and the benchmark opens the SpWKit port in caller-owned storage, so benchmark mechanics do not require the heap convenience path.

## Counter metadata and calibration

The profiling backend records counter kind, width and frequency metadata. A counter-frequency override may be supplied when it is known and reliable:

```bash
benchmarks/run_profile_campaign.sh --counter-hz 2500000000
```

A frequency of `0` means unknown/platform-reported. Raw counter ticks remain authoritative even when derived time is available.

Every clean profiling configuration also records a back-to-back counter-read calibration dataset. This is **diagnostic instrumentation data only**. It must not be treated as a universal subtraction constant.

In particular, different compiler layout, inlining and counter serialization can make a standalone back-to-back counter loop materially different from the probe sequence inside a real call path. Raw measured intervals are therefore never silently corrected, clamped or rewritten using the calibration result.

## Direct/native comparison

The primary abstraction-overhead comparison is not:

```text
SpWKit interval - standalone counter floor
```

It is:

```text
SpWKit path - equivalent direct/native path
```

The reference hosted comparison measures copied DRIVER TX from conceptual API entry to the provider/native handoff.

Both paths execute inside the **same Release executable and same process**. They:

- use the same architectural counter backend;
- use the same packet and payload storage;
- call the exact same provider submission function;
- end at the exact same provider/native boundary;
- use the same warmup and measured iteration counts;
- alternate native-first and SpWKit-first order every measured iteration to reduce systematic drift bias.

The only intentional difference before the shared provider submission is:

```text
Direct/native
  -> native API-entry probe
  -> shared provider/native submission

SpWKit
  -> spw_port_send()
  -> backend dispatch
  -> DRIVER callback
  -> shared provider/native submission
```

The comparison emits `spwkit.profile.comparison.v1` rows containing independent native and SpWKit statistics plus signed `SpWKit - native` deltas. The standalone counter-floor calibration is retained beside the comparison as diagnostic information and is not subtracted.

Run only the reference comparison with:

```bash
benchmarks/run_native_comparison.sh \
  --warmup 256 \
  --iterations 1024 \
  --payloads "0 1 8 64 256 1024 4096"
```

For normal use, prefer the centralized campaign so the layer breakdown and direct/native comparison are produced together in one result set.

## Adapting the native baseline to real hardware

The reference provider call is a methodology fixture, not a claim about a particular DMA or vendor API.

For a real platform, keep the counter, campaign, statistics and result schema unchanged and replace the shared provider/native operation with the real platform boundary. Examples include:

- direct MMIO/AXI register submission;
- DMA descriptor/API submission;
- a vendor SDK's direct read/write or transfer call;
- a socket `send`/`recv` boundary for a network-backed transport;
- another native controller API representing the operation an application would use without SpWKit.

A fair native versus SpWKit comparison must keep equivalent:

- hardware/controller;
- payload buffers and sizes;
- cache/coherency policy;
- DMA descriptor/configuration policy;
- compiler and optimization flags;
- CPU affinity and priority where applicable;
- counter source and frequency metadata;
- warmup and measured iteration counts;
- device/link state.

Record platform-specific details with the result set, including the exact native API/controller configuration. For an FPGA-backed platform, SpWKit software timing stops at the software/provider handoff; FPGA/IP-core latency is measured separately in FPGA clock cycles.

## Direct CMake execution

The benchmark project can also be configured manually:

```bash
cmake -S benchmarks -B build/profile-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BENCHMARK_PROFILE_START=SPW_PROFILE_ID_TX_API_ENTRY \
  -DSPWKIT_BENCHMARK_PROFILE_END=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY

cmake --build build/profile-benchmark --target \
  spwkit_profile_benchmark \
  spwkit_profile_counter_floor \
  spwkit_profile_native_comparison
```

The scripts remain the authoritative reproducible execution path because they enforce clean builds and serial campaign behavior.

## GitHub Actions

`.github/workflows/profile-benchmark.yml` runs a shortened clean serial campaign automatically on relevant pull requests and `develop` pushes. It validates:

- benchmark execution;
- clean-build campaign behavior;
- result directory identity;
- layer result schemas;
- calibration schemas;
- direct/native comparison schema and signed deltas;
- artifact generation.

The same workflow provides `workflow_dispatch` inputs for manual execution when the workflow is present on the repository default branch.

GitHub-hosted result artifacts are retained for 30 days and explicitly marked as non-authoritative performance data.

## Result interpretation

For hosted systems, prefer median together with p95/p99 when discussing normal-path behavior. Means and maxima can be heavily distorted by scheduler preemption, interrupts, VM scheduling and unrelated system activity.

Do not compare absolute TSC/CNTVCT values from different machines as though they were directly interchangeable CPU-cycle counts. Keep the recorded counter metadata with every result.

The strongest software-abstraction result is the direct/native differential measured on the same controlled system. Physical Cortex-M profiling uses DWT CYCCNT and should report literal MCU cycles together with the configured core clock and cache/DMA policy.
