# Virtual backend

Local, implementation-independent virtual SpaceWire ports used for unit tests and same-host simulation.

The first implementation should preserve packet boundaries, EOP/EEP, link state, time codes, queue limits, and deterministic fault behaviour without depending on Ethernet or POSIX semantics in the portable core.
