# Embedded Tests

Embedded verification is split into host-side cross-build checks and runtime execution on a target.

Cross-build CI checks freestanding compatibility, toolchain files, static-allocation configurations, and platform adapter compilation. Runtime tests execute the reusable backend contract harness on bare-metal, HardRT, FreeRTOS, or RTEMS targets where a runner/controller is available.

A successful cross-build is never treated as proof of runtime behaviour.
