# SpWKit profiling benchmark

This directory contains the hosted benchmark runner for the compile-time paired-probe profiling framework.

The benchmark exists to characterize SpWKit software overhead in architectural counter ticks. It is not a SpaceWire PHY benchmark, and GitHub-hosted runner timings are not authoritative performance results.

## Local/manual execution

The helper script configures an isolated Release build, runs a packet-size sweep and emits one JSON object per payload size:

```bash
benchmarks/run_profile_benchmark.sh \
  --range tx_api_native \
  --warmup 256 \
  --iterations 1024 \
  --payloads "0 1 8 64 256 1024 4096" \
  --output build/profile-results.jsonl
```

Supported copied-DRIVER TX ranges are:

- `tx_api_backend`
- `tx_backend_provider`
- `tx_provider_native`
- `tx_api_native`

The default maximum measured sample count is 4096 and the default maximum payload is 4096 bytes. Sample storage is fixed-size and the benchmark opens the SpWKit port in caller-owned storage, so benchmark mechanics do not require the SpWKit heap convenience path.

If the architectural counter frequency is known and reliable for the platform, pass it explicitly:

```bash
benchmarks/run_profile_benchmark.sh \
  --range tx_api_native \
  --counter-hz 2500000000 \
  --payloads "64 1024"
```

A frequency of `0` means unknown/platform-reported. Raw counter ticks remain authoritative even when derived time is emitted.

## Direct CMake execution

The benchmark is also a standalone CMake project:

```bash
cmake -S benchmarks -B build/profile-benchmark \
  -DCMAKE_BUILD_TYPE=Release \
  -DSPWKIT_BENCHMARK_PROFILE_START=SPW_PROFILE_ID_TX_API_ENTRY \
  -DSPWKIT_BENCHMARK_PROFILE_END=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
cmake --build build/profile-benchmark --target spwkit_profile_benchmark
./build/profile-benchmark/spwkit_profile_benchmark \
  --warmup 256 --iterations 1024 --payload 64
```

## GitHub Actions

`.github/workflows/profile-benchmark.yml` runs a short hosted sweep automatically on relevant pull requests and `develop` pushes. This validates the benchmark executable, statistics and JSON schema.

The same workflow defines `workflow_dispatch` inputs for:

- profiling range;
- warmup iterations;
- measured iterations;
- packet-size sweep;
- optional counter-frequency override.

Because GitHub only exposes manual dispatch for workflow files present on the repository default branch, the **Run workflow** button becomes available after this workflow reaches `main`. This does not require changing the repository's `develop`-first release policy.

Manual Actions results are uploaded as a 30-day artifact containing:

- `results.jsonl`, one `spwkit.profile.v1` object per payload size;
- `run-metadata.json`, identifying the commit/ref/runner and explicitly marking GitHub-hosted timing as non-authoritative.

## Result interpretation

The result schema records the selected probe pair, backend/path, payload size, compiler, optimization state and counter metadata together with:

- minimum;
- median;
- mean;
- p95;
- p99;
- maximum;
- population standard deviation.

Use controlled/local machines or physical targets for numbers intended to support performance claims. Shared CI runners are useful for regression mechanics and convenient snapshots, not precision timing baselines.
