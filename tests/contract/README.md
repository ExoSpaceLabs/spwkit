# Backend Contract Tests

This directory will contain the reusable SpaceWire port/backend contract suite.

Every backend should run the same applicable behavioural cases through a common fixture. The suite covers link lifecycle, bidirectional packet transfer, packet boundaries, EOP/EEP, time codes, timeout/non-blocking behaviour, statistics, reset, and recovery.

Optional cases are selected from declared backend capabilities. Unsupported optional features may be skipped explicitly; required SpaceWire semantics may not be reinterpreted per backend.

The target is to execute this suite against loopback, virtual, Ethernet, Linux device, embedded/HardRT, physical, and vendor backends as those implementations become available.
