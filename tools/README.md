# Tools

Current development/integration tooling:

```text
wireshark/vspw_tp.lua              VSPW-TP v1 Lua dissector
wireshark/validate_dissector.py    deterministic tshark/PCAP validation
```

See `tools/wireshark/README.md` for capture, Decode As, display-filter and logical-fragment correlation workflows.

The Wireshark/tshark tooling is deliberately separate from `libspwkit` and adds no runtime dependency to the library.

Planned command-line tools:

```text
spwctl    configure and inspect SpaceWire/virtual SpaceWire ports
spwmon    monitor packets, link state, statistics, and faults
```

Application-oriented command-line tooling should use the same public SpWKit API as normal applications rather than depend directly on simulator or backend internals. Wire-inspection tooling is the intentional exception: a packet dissector reads the documented VSPW-TP transport format from captures but is never part of the application API/runtime path.
