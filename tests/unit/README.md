# Unit Tests

Unit tests cover deterministic, host-executable logic with no simulator, network, OS device, or hardware dependency.

Primary targets include packet/value validation, state helpers, queue boundaries, capability handling, error mapping, fragmentation helpers, and other portable core behaviour as those components are introduced.

Unit tests must be fast, isolated, repeatable, and labelled `unit` in CTest.
