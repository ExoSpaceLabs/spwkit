# Ethernet backend

Distributed virtual SpaceWire transport for connecting `vspw` endpoints across Linux hosts, containers, bare-metal boards, and RTOS targets.

Initial transport target: UDP. Raw Ethernet may be added where a lower-level point-to-point transport is useful.

Ethernet is only the carrier. SpaceWire packet termination, link state, modeled rate, latency, and error behaviour remain defined by the virtual SpaceWire protocol rather than inherited from Ethernet.
