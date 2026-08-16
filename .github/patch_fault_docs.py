from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"needle not found in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


replace(
    "README.md",
    "- deterministic configurable SpaceWire-side virtual link rate and fixed latency;\n"
    "- active device-to-device CI exercising real UDP transfer, recovery and timing behavior.\n\n"
    "Deterministic fault injection, stronger multi-process/container examples, broader distributed contract coverage and capture/Wireshark tooling remain v0.2 work.\n",
    "- deterministic configurable SpaceWire-side virtual link rate and fixed latency;\n"
    "- deterministic seeded transport drop/duplicate/reorder/delay injection;\n"
    "- explicit SpaceWire-side EEP injection with transport/SpaceWire fault-domain counters;\n"
    "- active device-to-device CI exercising real UDP transfer, recovery, timing and fault behavior.\n\n"
    "Stronger multi-process/container examples, broader distributed contract coverage and capture/Wireshark tooling remain v0.2 work.\n",
)
replace(
    "README.md",
    "The optional virtual timing model adds deterministic SpaceWire-side serialization and fixed latency to logical DATA/TIME_CODE events without treating incidental host UDP delay as simulated link timing. ACKs, keepalives and retransmissions remain transport mechanics and do not reapply the logical SpaceWire delay.\n",
    "The optional virtual timing model adds deterministic SpaceWire-side serialization and fixed latency to logical DATA/TIME_CODE events without treating incidental host UDP delay as simulated link timing. ACKs, keepalives and retransmissions remain transport mechanics and do not reapply the logical SpaceWire delay.\n\n"
    "The UDP backend also supports fixed-size seeded fault rules for transport drop, duplicate, adjacent reorder and delay. These operate on VSPW-TP carrier datagrams and remain distinct from explicit SpaceWire-side EEP injection. Ordinary transport loss or reordering never synthesizes EEP. `spw_port_get_fault_statistics()` exposes separate counters for the two fault domains.\n",
)

replace(
    "docs/roadmap.md",
    "- deterministic SpaceWire-side virtual link rate/latency timing for DATA and TIME_CODE;\n"
    "- active device-to-device UDP CI including forced retry/dedup/recovery and timing coverage;\n",
    "- deterministic SpaceWire-side virtual link rate/latency timing for DATA and TIME_CODE;\n"
    "- deterministic seeded VSPW-TP transport drop/duplicate/reorder/delay injection;\n"
    "- explicit SpaceWire-side EEP injection with separate fault-domain diagnostics;\n"
    "- active device-to-device UDP CI including forced retry/dedup/recovery, timing and fault coverage;\n",
)
replace(
    "docs/roadmap.md",
    "- deterministic SpaceWire-side and transport-side fault injection;\n",
    "",
)

replace(
    "docs/vspw-tp.md",
    "Host Ethernet/UDP latency is not measured or folded into this model. It remains incidental carrier behavior and cannot redefine the configured virtual SpaceWire timing.\n\n## Liveness\n",
    "Host Ethernet/UDP latency is not measured or folded into this model. It remains incidental carrier behavior and cannot redefine the configured virtual SpaceWire timing.\n\n"
    "## Deterministic fault injection\n\n"
    "The UDP backend can apply deterministic seeded fault rules without changing the VSPW-TP wire format. Rules are fixed-size configuration state and require no dynamic allocation or mandatory worker thread. Each matching rule advances its own reproducible PRNG stream derived from the configured seed; an always-fire probability is available for exact CI scenarios.\n\n"
    "Transport faults operate strictly on VSPW-TP carrier datagrams after framing:\n\n"
    "- **drop** suppresses a selected datagram while reporting local transport success so the normal reliability machinery can observe the loss and retry where appropriate;\n"
    "- **duplicate** transmits the selected datagram twice;\n"
    "- **reorder** holds one selected datagram in a fixed datagram-sized slot and sends the next outgoing datagram first, providing a bounded adjacent swap;\n"
    "- **delay** postpones one selected transport datagram by the configured microseconds and consumes the applicable transport timeout.\n\n"
    "Targets can distinguish DATA, TIME_CODE, ACK and KEEPALIVE/control traffic. This makes scenarios such as a dropped ACK reproducible without modifying application payloads or pretending that carrier loss is a SpaceWire event. Reliable retransmission and duplicate suppression continue to operate normally around injected transport faults.\n\n"
    "The explicitly SpaceWire-visible fault action is **EEP injection**. It applies to an outgoing logical DATA packet before VSPW-TP framing, converting a selected EOP terminator to EEP. This is intentionally a different fault domain: transport drop, duplicate, reorder or delay never synthesizes EEP.\n\n"
    "`spw_port_get_fault_statistics()` reports transport drops, duplicates, reorders and delays separately from SpaceWire EEP injections. This separation is part of the simulation contract rather than merely a diagnostic convention.\n\n"
    "## Liveness\n",
)
replace(
    "docs/vspw-tp.md",
    "- fixed virtual propagation/processing latency.\n",
    "- fixed virtual propagation/processing latency;\n"
    "- deterministic fault seed and fixed transport/SpaceWire fault rules.\n",
)
replace(
    "docs/vspw-tp.md",
    "- successful DATA and TIME_CODE delivery when the caller budget covers the virtual delay.\n",
    "- successful DATA and TIME_CODE delivery when the caller budget covers the virtual delay;\n"
    "- dropped ACK recovery without duplicate logical delivery;\n"
    "- deterministic DATA duplication and bounded adjacent fragment reordering;\n"
    "- transport-delay timeout behavior;\n"
    "- explicit EOP-to-EEP SpaceWire fault injection with separate fault-domain counters.\n",
)

replace(
    "docs/api.md",
    "The UDP backend configuration carries portable descriptive network values such as numeric IPv4 addresses, UDP ports, virtual `link_id`, and fragment payload size. Native socket handles and platform socket structures remain internal.\n",
    "The UDP backend configuration carries portable descriptive values such as numeric IPv4 addresses, UDP ports, virtual `link_id`, fragment payload size, virtual timing controls and fixed deterministic fault rules. Native socket handles and platform socket structures remain internal.\n",
)
replace(
    "docs/api.md",
    "- default 1200-byte transport fragments.\n\nACK/retransmission, peer liveness/disconnect detection, loss/reordering handling, configurable latency/rate and deterministic fault injection remain v0.2 work.\n",
    "- default 1200-byte transport fragments;\n"
    "- session-bound ACK/retransmission and duplicate suppression;\n"
    "- peer liveness/disconnect/restart recovery;\n"
    "- bounded arbitrary-order fragment reassembly;\n"
    "- deterministic virtual SpaceWire rate/latency timing;\n"
    "- deterministic seeded transport drop/duplicate/reorder/delay injection;\n"
    "- explicit SpaceWire-side EEP injection with separate fault-domain diagnostics.\n\n"
    "Broader shared-contract coverage, stronger multi-process/network-namespace examples, capture tooling and final platform-scope decisions remain v0.2 work.\n",
)
replace(
    "docs/api.md",
    "Statistics are backend-independent counters intended for diagnostics and verification. Backend-specific counters may later be available through extension APIs without contaminating the common structure.\n\n## Blocking and timeouts\n",
    "Statistics are backend-independent counters intended for diagnostics and verification.\n\n"
    "Fault-capable backends additionally expose:\n\n"
    "```text\n"
    "spw_port_get_fault_statistics\n"
    "spw_port_clear_fault_statistics\n"
    "```\n\n"
    "`spw_fault_statistics_t` deliberately separates VSPW-TP transport drop/duplicate/reorder/delay counters from SpaceWire-visible EEP injection. Backends without `SPW_CAP_FAULT_INJECTION` return `SPW_ERR_UNSUPPORTED` for these operations. The existing `spw_statistics_t` layout remains unchanged.\n\n"
    "## Blocking and timeouts\n",
)

Path("docs/fault-injection.md").write_text('''# Deterministic fault injection\n\nSpWKit v0.2 provides bounded deterministic fault injection in the distributed UDP backend. The design keeps two fault domains explicit: VSPW-TP transport faults manipulate carrier datagrams, while SpaceWire-visible faults manipulate logical SpaceWire events.\n\n## Configuration model\n\n`spw_udp_config_t` contains a fixed array of eight `spw_udp_fault_rule_t` entries and a `fault_seed`. Disabled rules are zero-cost configuration entries and no dynamic rule allocation is required.\n\nEach enabled rule defines an action, target, probability in units of 1/10000, optional maximum firing count, and delay for transport-delay actions. Rules are evaluated in array order. Each rule has a deterministic PRNG stream derived from the configured seed, so an identical configuration and event stream reproduce the same decisions.\n\nA probability of `SPW_UDP_FAULT_PROBABILITY_SCALE` (10000) means always fire. `max_events == 0` means unlimited firings; a non-zero value bounds how many times the rule may inject a fault.\n\n## Transport fault domain\n\nTransport actions operate after VSPW-TP framing and may target DATA, TIME_CODE, ACK or KEEPALIVE/control datagrams:\n\n- `SPW_UDP_FAULT_ACTION_TRANSPORT_DROP` suppresses the selected datagram;\n- `SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE` sends it twice;\n- `SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER` performs a bounded adjacent swap using one fixed datagram-sized holding slot;\n- `SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY` delays the selected datagram by `delay_us`.\n\nThe normal reliability layer remains responsible for retry and duplicate suppression. In particular, a dropped ACK can cause complete-message retransmission, while the receiver still exposes the logical packet only once.\n\nTransport faults do **not** synthesize SpaceWire EEP. A network failure is not silently promoted into a SpaceWire link/packet error.\n\n## SpaceWire fault domain\n\n`SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP` is valid only for DATA. When it fires for an outgoing EOP packet, the logical terminator becomes EEP before VSPW-TP framing. The receiver therefore observes EEP through the normal `spw_port_receive()` API.\n\nThis explicit action is the only v0.2 fault rule currently intended to alter an application-visible SpaceWire packet. Additional SpaceWire-side fault types can extend this domain without overloading transport-loss semantics.\n\n## Diagnostics\n\nFault-capable backends advertise `SPW_CAP_FAULT_INJECTION`. `spw_port_get_fault_statistics()` returns `spw_fault_statistics_t`, which contains separate counters for transport drops, duplicates, reorders, delays and SpaceWire EEP injections.\n\n`spw_port_clear_fault_statistics()` clears these diagnostics without modifying the deterministic rule schedule. Explicit port `reset()` restarts the deterministic injector state.\n\n## Bounds and portability\n\nThe implementation uses fixed rule storage and one fixed maximum-UDP-datagram reorder slot. It introduces no mandatory heap allocation, thread, socket type or vendor handle into the common application API. The deterministic decision engine is socket-independent; only application of transport actions belongs to the POSIX UDP backend.\n\n## Verification\n\nThe v0.2 test suite covers deterministic rule replay, rule validation, dropped-ACK recovery, duplicate suppression, arbitrary-order reassembly under injected adjacent reordering, transport-delay timeout behavior, explicit SpaceWire EEP injection, and fault-domain statistics.\n''')
