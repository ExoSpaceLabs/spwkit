# vspwd

`vspwd` is the planned virtual SpaceWire service.

Responsibilities are expected to include:

- creation and management of virtual SpaceWire ports;
- local and distributed peer links;
- packet/EOP/EEP transport;
- link-state modelling;
- time-code transport;
- modeled data rate and latency;
- queueing and statistics;
- deterministic fault injection;
- future router/network simulation support.

The service is not intended to simulate LVDS electrical behaviour or Data-Strobe waveforms. Those remain HDL/hardware verification concerns.
