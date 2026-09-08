# Release procedure

SpWKit uses `develop` for integration and `main` for stable release boundaries. A release is created only when a validated `develop` branch is promoted to `main` through a pull request and the candidate version is exactly one allowed semantic-version step above the latest immutable release tag.

The repository intentionally keeps only the long-lived `main` and `develop` branches after completed work. Temporary feature or release branches must be deleted after integration. Release history is preserved by immutable tags and GitHub Releases, not by permanent release branches.

## Version authority

The package version is declared in `CMakeLists.txt` and must exactly match the public API version in `include/spwkit/api.h`.

A release candidate must also have:

- a finalized dated `CHANGELOG.md` heading, `## vX.Y.Z — YYYY-MM-DD`;
- `docs/releases/vX.Y.Z.md`;
- matching stable-version documentation in `README.md` and `docs/current-status.md`;
- installed consumers requesting the current `X.Y` package minor.

Changing these values on `develop` does **not** create a release. Release automation runs only at the `main` promotion boundary.

## Allowed version transition

Let the latest immutable release be `vA.B.C`. A `develop -> main` release PR may declare exactly one of:

- patch: `vA.B.(C+1)`;
- minor: `vA.(B+1).0`;
- major: `v(A+1).0.0`.

Skipping versions is rejected. Reusing an existing tag is rejected. Moving or overwriting a release tag is never permitted.

For example, from `v0.5.1` the only accepted candidates are:

```text
v0.5.2
v0.6.0
v1.0.0
```

A typo such as `v0.7.0`, `v0.6.3`, or `v2.0.0` is rejected rather than interpreted as intent.

## Pull-request gate

Every PR targeting `main` is checked by `.github/workflows/release-policy.yml`.

The release PR must:

1. target `main`;
2. originate from `develop`;
3. declare a package/API version that is exactly one allowed step after the latest release tag;
4. have no existing tag for the candidate version;
5. contain finalized changelog and release-note metadata;
6. preserve the current installed-consumer package minor;
7. contain the latest release tag in its ancestry.

A `develop -> main` PR that keeps the already released version is therefore rejected. `develop` itself is not constrained in the opposite direction: it may carry the next development version before a release is ready.

## Main push state machine

After a commit reaches `main`, the release-policy workflow revalidates the exact `main` head and compares the declared project version with the latest immutable SemVer tag.

### Main version equals latest tag

Result: **no tag and no release**.

Ordinary same-version repository changes cannot accidentally republish the current release.

### Main version is lower than latest tag

Result: **hard failure**.

The repository is not permitted to move its declared release version backwards.

### Main version is higher than latest tag

The workflow requires all of the following before tagging:

- the version is exactly one allowed SemVer step;
- project/API/release metadata are internally consistent;
- the latest release tag is an ancestor of the new `main` head;
- the candidate tag does not already exist;
- GitHub associates the exact `main` commit with one merged `develop -> main` PR.

A direct push, unrelated branch merge, skipped version, downgrade, reused tag, or inconsistent release metadata fails without creating a release tag.

## Tag creation and publication

Once the new `main` boundary passes all checks, the workflow creates a lightweight immutable `vX.Y.Z` tag directly at the exact `main` commit through the GitHub API.

Because tags created with the workflow `GITHUB_TOKEN` do not recursively trigger another workflow, the release policy explicitly dispatches `.github/workflows/release.yml` using that exact tag as its ref.

The Release workflow independently verifies that:

- the tag name matches the project/API version;
- the changelog is finalized;
- the tagged commit is the exact current `main` head;
- installed-package version requirements agree.

Only then are Debian packages, SHA-256 files, the multi-platform GHCR image, and the GitHub Release published.

## Failure and recovery rules

Release tags are immutable. Never delete, move, or recreate a published release tag to repair a failed build.

If validation fails **before** tag creation, fix the problem on `develop`, rerun CI, and promote a corrected `develop -> main` release PR.

If tag creation succeeds but dispatch fails, rerun the failed Release policy workflow attempt. Its recovery path may dispatch the Release workflow only for the already-existing tag at the exact release boundary.

If the Release workflow itself fails after the tag exists, fix only release infrastructure when the source boundary itself is still valid, then rerun the Release workflow against the same immutable tag. Never silently change source code underneath an existing tag.

## Normal release checklist

1. Complete implementation and evidence on `develop`.
2. Choose the next allowed SemVer version.
3. Align `CMakeLists.txt`, `SPWKIT_API_VERSION_*`, changelog, release notes, stable-version docs, and installed consumers.
4. Ensure consolidated CI and any release-specific hardware/evidence gates are green on `develop`.
5. Open `develop -> main` PR.
6. Require the Release policy PR gate and normal CI to pass.
7. Merge the PR to `main`.
8. Let the `main` Release policy create the immutable tag and dispatch the exact-tag Release workflow.
9. Verify release assets and GHCR publication.
10. Synchronize `develop` with the released `main` head if necessary and remove temporary branches.

This procedure makes a release an explicit branch-promotion event rather than a side effect of editing a version string somewhere in the repository.
