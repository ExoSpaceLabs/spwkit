# Device-to-device Tests

This directory will contain the Docker Compose harness used to test two independent SpWKit nodes across an isolated virtual Ethernet network.

Planned topology:

```text
node-a:vspw0 <---- Docker network ----> node-b:vspw0
      \                                  /
       +----------- verifier -----------+
```

The future `compose.yaml` should provide `node-a`, `node-b`, and `verifier` services. The verifier owns the test result and must exit non-zero on failure so GitHub Actions can use `--exit-code-from verifier`.

Coverage includes simultaneous bidirectional traffic, packet sizes around fragmentation boundaries, EOP/EEP, peer restart, reconnect, network interruption, deterministic payload verification, and transport sequencing behaviour.

Docker readiness must be explicit; container startup order is not accepted as proof that a SpaceWire peer is ready.
