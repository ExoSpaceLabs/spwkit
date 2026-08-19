# HardRT POSIX integration

This standalone integration proves that an installed HardRT scheduler and an installed SpWKit package can be used together without coupling either library's runtime implementation.

The CI fixture currently pins HardRT commit:

```text
1b861393cd7967ce5d0b3ac6f45928828a2d63aa
```

HardRT reports package version `0.4.0` at that commit, but the HardRT repository does not currently expose a matching `v0.4.0` tag. Pinning the exact commit keeps this integration reproducible until HardRT has an audited release tag.

## Contract

The integration project consumes only installed CMake packages:

```cmake
find_package(HardRT 0.4 CONFIG REQUIRED)
find_package(SpWKit 0.5 CONFIG REQUIRED)
```

Two HardRT POSIX tasks each own an independent SpWKit loopback port and validate:

- caller-owned/no-heap port construction;
- link start and capability queries;
- copied DATA with EOP and EEP;
- finite public API timeouts;
- SpaceWire time codes;
- statistics;
- clean port close.

Each task owns its own `spw_port_t`. This test intentionally does not imply that concurrent calls into one shared SpWKit port are supported; thread/task safety of a shared handle requires a separate explicit contract.

## Dependency boundary

HardRT is an external integration dependency only. `libspwkit`, its exported CMake package and public headers must contain no HardRT dependency or HardRT-specific types.

The Cortex-M/bare-metal counterpart is tracked in issue #89 and will use the same dependency rule with `arm-none-eabi`, HardRT's `cortex_m` port and SpWKit's hosted backends disabled.
