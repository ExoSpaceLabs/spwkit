# `vspwd` virtual SpaceWire service

`vspwd` is the shipped Linux userspace virtual SpaceWire service, introduced in v0.4. The implementation lives under `src/vspwd` and applications normally reach it through `SPW_BACKEND_DEVICE` and the private VSPD protocol.

Current behavior includes:

- a deterministic two-port virtual topology;
- link start/stop/reset and peer loss/recovery;
- complete packet transport with EOP/EEP preservation;
- time-code transport;
- bounded queueing, readiness and statistics;
- management through `spwctl`;
- passive state/statistics monitoring through `spwmon`;
- an optional VSPW-TP/UDP bridge for distributed virtual links;
- optional `/dev/vspwX` presentation through the separate `spwcuse` process.

See [`docs/vspwd.md`](../../docs/vspwd.md) for the service contract and [`docs/vspw-device-protocol.md`](../../docs/vspw-device-protocol.md) for the private VSPD framing.

`vspwd` is a software endpoint/link simulator. It does not implement generic SpaceWire routing and does not simulate LVDS electrical behavior, Data-Strobe waveforms, a physical SpaceWire controller, or PHY interoperability.
