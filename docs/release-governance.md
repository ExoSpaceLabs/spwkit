# Release and branch governance

SpWKit uses `develop` as the integration branch and `main` as the immutable
release boundary. Repository settings must enforce the same contract already
implemented by `.github/workflows/release-policy.yml`.

This document is normative for repository administration. Workflow policy is
not a substitute for branch protection: a failing workflow can report an
invalid direct push only after the push has already landed.

## Branch policy

### `main`

`main` is release-only.

Required repository settings:

- require changes through a pull request;
- do not allow direct pushes in normal operation;
- require the `Validate develop to main release boundary` status check;
- require the pull-request head branch to be `develop` through the existing
  release-policy workflow;
- require the branch to be up to date before merge when GitHub offers that
  option for the selected protection mechanism;
- block force pushes;
- block branch deletion;
- do not permit routine bypasses.

For a single-maintainer repository, required approving reviews may remain at
zero so the maintainer is not forced into an impossible self-review. The
release-policy check is the mandatory gate. Increase required approvals when a
second maintainer is available.

### `develop`

`develop` is the normal integration branch.

Required repository settings:

- require changes through a pull request;
- block force pushes;
- block branch deletion;
- require the stable integration gates below before merge;
- do not permit routine bypasses.

The required check set should cover the portable API, hosted platforms,
sanitizers, ABI, dependency integrations and robustness evidence. As of the
current pre-v1 baseline the stable gates are:

- `Host / Linux GCC`
- `Host / Linux Clang`
- `Host / Windows MSVC`
- `Host / macOS Clang`
- `Linux ASan + UBSan`
- `Pure C / static, shared, no-heap, freestanding`
- `Repository hygiene`
- `Shared C ABI surface`
- `Active docs and dependency baselines`
- `Parser fuzz / ASan + UBSan`
- `Lifecycle / ownership / reconnect soak`
- `HardRT Cortex-M7`
- `CCSDSPack v2 transport`

When a required check is renamed or intentionally replaced, update this
document and the repository protection settings in the same maintenance
change. Do not leave a permanently stale required-check name.

## Administrator and bypass policy

Administrative bypass is reserved for recovery from repository-policy or CI
infrastructure failure. It is not a convenience path for ordinary changes.

If a bypass is unavoidable:

1. preserve branch history; do not force-push or delete `main`/`develop`;
2. make the smallest corrective change possible;
3. restore the normal PR/check path immediately;
4. record the reason and corrective action in a repository issue or release
   incident note;
5. re-run the applicable validation workflows.

Release tags are immutable. Never move, delete and recreate, or overwrite a
published release tag as a recovery mechanism.

## Release flow

The normal release path is:

```text
feature/fix branch
      -> develop PR
      -> required develop checks
      -> develop
      -> develop -> main release PR
      -> Validate develop to main release boundary
      -> main
      -> immutable vX.Y.Z tag
      -> exact-tag Release workflow
```

`.github/workflows/release-policy.yml` validates that a new release version
is the next allowed SemVer step, that CMake/API versions agree, that release
documentation exists, and that a new `main` release boundary originated from
a merged `develop -> main` pull request.

## Recovery procedures

### Tag exists but release publication did not start

Do not recreate or move the tag.

Re-run the failed `Release policy` main-boundary workflow. Its recovery path
recognizes an existing immutable tag on the exact `main` commit and
re-dispatches the `Release` workflow when publication is still absent.

### Release workflow failed after the tag was created

Re-run the failed `Release` workflow for the same immutable tag. Fix
repository code only through `develop`; if the failure requires a source
change, publish that correction as the next SemVer release rather than
mutating the existing tag.

### Invalid state reached `main` through an emergency bypass

Do not force-push `main` and do not manufacture a release tag for the invalid
state. Prepare the corrective change on `develop`, run the normal integration
checks, and promote the corrected `develop` head through a new release PR.
Record why the bypass occurred.

### Required check is unavailable because a workflow was renamed

Treat this as repository-maintenance failure, not a reason to disable
protection indefinitely. Update the required-check setting to the intended
replacement, update this document, and verify protection with the governance
audit workflow.

## Verification

`.github/workflows/governance-audit.yml` checks that both `main` and
`develop` are reported by GitHub as protected branches. It runs weekly and
can also be dispatched manually.

The audit deliberately does not claim to verify every admin-only protection
detail because GitHub may withhold those fields from the workflow token.
Repository administrators must keep the concrete settings aligned with this
document.
