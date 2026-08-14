# Hardware-in-the-loop Tests

HIL tests run only on explicitly labelled self-hosted GitHub Actions runners connected to the required hardware bench.

Initial profiles:

- `fpga-loopback`: one AMD SoC/FPGA endpoint, AXI/DMA/IRQ and loopback validation;
- `two-node`: two physical SpaceWire endpoints connected by a real link;
- `virtual-physical-gateway`: virtual `vspw` traffic crosses Ethernet into `/dev/spw0` and a physical SpaceWire peer.

The target-side implementation will be driven by `tests/hil/run_hil.sh`. The script must leave machine-readable results and diagnostic evidence under `artifacts/hil/`.

The lab controller is responsible for deterministic power/reset/programming, serial capture, timeout enforcement, and returning hardware to a known state between profiles.

HIL is not executed automatically for arbitrary pull requests. See `.github/workflows/hil.yml` and `docs/testing.md`.
