# Release performance gate

## Purpose

A SpWKit release is not accepted merely because functional CI is green. Changes that can affect a hot path, ownership path, or lifecycle cost are compared with the previous immutable release before tagging.

For `v0.7.0`, the baseline is `v0.6.1`.

## Controlled release comparison

Run from a Linux checkout containing the release-comparison tooling:

```bash
benchmarks/run_release_profile_comparison.sh \
  --baseline-ref v0.6.1 \
  --candidate-ref HEAD \
  --cpu auto \
  --governor performance
```

The runner:

1. resolves immutable baseline and candidate commits;
2. creates detached worktrees for each revision;
3. selects one logical CPU and uses it for both revisions;
4. runs the same clean serial profiling campaign for each revision;
5. runs lifecycle profiling separately for each revision;
6. compares matching rows with `compare_release_profiles.py`;
7. writes machine-readable and Markdown comparison artifacts.

The comparison uses:

- complete public-operation medians for LOOPBACK and SIMULATOR;
- `SpWKit - native` paired overhead for DRIVER copied I/O, UDP/VSPW-TP and DEVICE/VSPD where a direct comparator exists;
- zero-copy operation and ownership medians for DRIVER zero-copy paths;
- separate lifecycle operation medians from `run_lifecycle_profile.sh`.

The direct/native differential is preferred when available because it removes much of the unrelated transport or provider cost from the release delta.

## Reproducibility rule

A single timing run is not sufficient evidence for accepting or rejecting a release change.

For release acceptance:

- run at least three controlled baseline/candidate comparisons on the same otherwise-idle host;
- use the same CPU, compiler/toolchain policy, build type, payload set and benchmark parameters;
- prefer a fixed performance governor where the system permits it;
- retain all campaign metadata and raw JSONL results;
- investigate an attention row only when the direction/magnitude is reproducible across runs rather than a one-off scheduler excursion.

The comparator's default attention rule requires both a positive delta above 20 ticks and a positive relative change above 5%. These defaults are **triage thresholds**, not a normative allowance to regress by 5% or 20 ticks. A smaller but stable regression can still require investigation; a larger one-off can still be noise.

## v0.7.0 affected paths

The `v0.7.0` behavioral-contract work added common lifecycle-state checks on application operations and ownership-epoch validation on zero-copy handles. Before release, review at minimum:

- LOOPBACK copied TX/RX;
- SIMULATOR copied TX/RX;
- DRIVER copied TX/RX differential;
- DRIVER zero-copy acquire/submit/reclaim/release paths;
- VSPW-TP/UDP TX/RX differential;
- Linux DEVICE/VSPD TX/RX differential and readiness evidence;
- lifecycle construction/start/stop/reset/close costs;
- Cortex-M7/STM32H755 DWT measurements for changed common paths when physical hardware is available.

`#214` added tests/documentation and does not itself add a packet hot-path layer, but its final candidate is still measured as part of the release commit.

## Acceptance

A release candidate passes this gate when:

- no changed hot path has an unexplained reproducible regression outside normal measurement variation;
- avoidable regressions have been optimized before release;
- necessary correctness/semantic costs are quantified and documented;
- functional, sanitizer, backend-contract and platform CI remains green after optimization;
- physical-target claims are based on physical-target measurements rather than hosted virtual timing.

A known avoidable regression is not intentionally deferred to `v0.7.1`. Patch releases remain available for defects discovered after publication, which is quite enough chaos without scheduling our own.

## GitHub-hosted screen

`.github/workflows/release-performance.yml` runs the comparator self-test and a shortened same-runner `v0.6.1` versus candidate screen on relevant pull requests. It uploads the complete comparison artifact.

GitHub-hosted timing is screening evidence only. It is useful for detecting obvious changes and validating the comparison mechanics, but the release decision uses repeated controlled-host results.
