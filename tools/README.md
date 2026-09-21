# Tools

Current development/integration tooling includes:

```text
spwctl                            VSPD management queries and statistics control
spwmon                            passive link-state/statistics monitoring
wireshark/vspw_tp.lua             VSPW-TP v1 Lua dissector
wireshark/validate_dissector.py   deterministic tshark/PCAP validation
```

`spwctl` and `spwmon` are built from `src/tools` when `SPWKIT_BUILD_TOOLS=ON`. `spwctl` uses the VSPD management channel without attaching as an application owner. `spwmon` subscribes passively to bounded daemon state/statistics snapshots and deliberately does **not** receive or print application DATA payloads.

See [`../docs/spwmon.md`](../docs/spwmon.md) for the monitoring contract and [`wireshark/README.md`](wireshark/README.md) for capture, Decode As, display-filter and logical-fragment correlation workflows.

The Wireshark/tshark tooling is deliberately separate from `libspwkit` and adds no runtime dependency to the library. It is the appropriate path when actual VSPW-TP wire packets or payloads must be inspected.

Application-oriented command-line tooling uses the same public/service contracts as normal applications rather than depending on backend implementation internals. Wire-inspection tooling is the intentional exception: a packet dissector reads the documented VSPW-TP transport format from captures but is never part of the application API/runtime path.
