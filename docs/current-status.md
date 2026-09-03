# Current project status

## Stable release: v0.5.1

`v0.5.1` is the current stable maintenance release on the v0.5 line. It preserves the v0.5 C ABI and backend behavior while completing optional C++17 wrapper parity for workspace and zero-copy operations.

The stable v0.5 line includes:

- process-local SpaceWire simulation;
- VSPW-TP/UDP on POSIX hosts and native Windows/Winsock;
- Linux `SPW_BACKEND_DEVICE`, VSPD and `vspwd`;
- `spwctl` and `spwmon`;
- production CUSE `/dev/vspwX` presentation;
- installed-package C and optional C++17 consumers;
- caller-owned/no-heap construction;
- zero-copy ownership semantics where advertised;
- HardRT `0.4.0` POSIX and Cortex-M7 integration evidence;
- multi-architecture Debian and GHCR publication.

See [v0.5.1 release notes](releases/v0.5.1.md).

## Development release: v0.6.0

`develop` carries the v0.6 integration work.

Completed software slices include:

- public `SPW_BACKEND_DRIVER` configuration/callback contract;
- DMA/zero-copy ownership mapping through the existing `spw_buffer_t` API;
- deterministic host reference-driver execution;
- no-heap/freestanding driver evidence;
- CCSDSPack PUS-C packet transport through installed-package UDP and Linux DEVICE/VSPD paths;
- two-node Docker Compose CCSDSPack-over-VSPW-TP/UDP integration;
- CI pinning of CCSDSPack `v2.0.0` to immutable commit `c2f318c330c564429bcc565a8acbff22728b2851`;
- repository documentation reconciliation and C++ convenience-wrapper parity work.

The old provisional `CCSDSPack/develop` reference is no longer used by the integration gate. Issue #90 remains open for explicit acceptance/closure bookkeeping around that dependency baseline.

## Remaining v0.6 evidence

```mermaid
flowchart LR
    SW["Portable driver + DMA contract<br/>complete"] --> STM["STM32H755 DMA/cache runtime evidence<br/>#119"]
    SW --> FPGA["Public FPGA/driver boundary<br/>#113"]
    SW --> CCSDS["CCSDSPack baseline acceptance / closure<br/>#90"]
    STM --> AUDIT["Final v0.6 audit"]
    FPGA --> AUDIT
    CCSDS --> AUDIT
    AUDIT --> REL["v0.6.0 release"]
```

The STM32H755 runtime test is intentionally not considered complete until its exact board/test architecture is agreed and executed. Physical FPGA/SpaceWire electrical HIL remains outside the current software-only evidence.
