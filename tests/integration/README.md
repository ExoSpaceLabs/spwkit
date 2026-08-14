# Integration Tests

Integration tests combine multiple SpWKit components on one host without requiring physical hardware.

Examples include the portable API plus a local virtual backend, simulator service plus client library, tool-to-simulator interaction, and fault/recovery flows that cross component boundaries.

Tests in this directory should be labelled `integration`; simulator-specific cases may additionally use the `simulator` label.
