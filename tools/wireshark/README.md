# VSPW-TP capture and Wireshark tooling

`vspw_tp.lua` is a development/integration dissector for the SpWKit VSPW-TP v1 UDP wire format. It is deliberately outside `libspwkit`: installing or running SpWKit does not require Wireshark, tshark, libpcap, Lua, or tcpdump.

The dissector tracks the wire contract documented in `docs/vspw-tp.md`:

- 40-byte network-order v1 header;
- DATA, TIME_CODE, LINK_CONTROL, KEEPALIVE and ACK message types;
- EOP/EEP, fragment-boundary and ACK-required flags;
- virtual link ID;
- 64-bit sender session ID;
- transport sequence and logical message ID;
- fragment offset and complete logical payload size;
- TIME_CODE `{time_count, control_flags}` payload;
- ACK acknowledged-sender-session payload;
- structural validation/expert information for unsupported or malformed frames.

## Capture UDP traffic

For peers using ports 42000 and 42001:

```bash
sudo tcpdump -i any -s 0 -w vspw.pcap \
  'udp and (port 42000 or port 42001)'
```

On a known interface, prefer that interface over `any`:

```bash
sudo tcpdump -i eth0 -s 0 -w vspw.pcap \
  'udp and (port 42000 or port 42001)'
```

For a namespace-based integration setup:

```bash
sudo ip netns exec spw-a tcpdump -i any -s 0 -w /tmp/vspw-a.pcap udp
```

The capture point is ordinary IP/UDP. No application instrumentation or private SpWKit API is needed.

## Load the Lua dissector

Launch Wireshark explicitly with the repository copy:

```bash
wireshark -X lua_script:tools/wireshark/vspw_tp.lua vspw.pcap
```

For command-line inspection:

```bash
tshark \
  -X lua_script:tools/wireshark/vspw_tp.lua \
  -r vspw.pcap
```

VSPW-TP ports are configurable, so the dissector does not own a hard-coded UDP port. It registers a UDP heuristic based on the `VSPW` magic and also registers for Wireshark **Decode As...**. Decode As is useful when deliberately debugging corrupted magic/version/header data.

CLI Decode As example for UDP port 42000:

```bash
tshark \
  -X lua_script:tools/wireshark/vspw_tp.lua \
  -d udp.port==42000,vspw \
  -r vspw.pcap
```

## Useful display filters

All VSPW-TP frames:

```text
vspw
```

DATA for one virtual link:

```text
vspw.type == 1 && vspw.link_id == 42
```

One logical reliable message from one sender session:

```text
vspw.session_id == 0x1111222233334444 && vspw.message_id == 100
```

Fragment boundaries:

```text
vspw.flag.fragment_start || vspw.flag.fragment_end
```

EEP packets/fragments:

```text
vspw.flag.eep
```

ACKs:

```text
vspw.type == 5
```

TIME_CODE events:

```text
vspw.type == 2
```

Structurally rejected/unsupported VSPW frames:

```text
vspw.valid == false
```

Wireshark expert information also marks malformed/unsupported frames and identifies fragmented DATA as a reassembly note.

## Relating transport frames to SpaceWire packets

VSPW-TP fragmentation is transport-internal. An application still receives one `spw_packet_t` only after complete backend reassembly.

For DATA, use this tuple to correlate fragments/retries belonging to the same logical event:

```text
sender session ID + logical message ID
```

`sequence` identifies an individual transport datagram and therefore changes across fragments and retransmissions. `message_id` stays with the logical DATA/TIME_CODE event. The current backend repeats the packet EOP/EEP terminator flag on every DATA fragment, while `FRAGMENT_START` and `FRAGMENT_END` identify the first and final transport fragments.

A complete-message retry after ACK loss reuses the logical `message_id` but emits new transport sequence values. Seeing repeated DATA fragments in a PCAP therefore does not imply duplicate application delivery; the receiver's reliability layer suppresses duplicate logical messages.

KEEPALIVE advertises the sender transport session. ACK contains both the ACK sender session in the fixed header and the acknowledged original-sender session in its 8-byte payload. Those two session identities are what prevent delayed stale ACKs from completing a new process incarnation.

Transport loss/reordering remains transport behavior. It does not imply SpaceWire EEP unless an explicit SpaceWire-side EEP was transmitted/injected.

## Automated validation and deterministic sample capture

`validate_dissector.py` builds a small classic-PCAP capture in memory using only the Python standard library, loads the actual Lua dissector through tshark, and verifies:

- heuristic VSPW recognition without claiming unrelated UDP;
- valid/unsupported frame handling;
- KEEPALIVE;
- two fragments of one DATA logical message;
- EOP/fragment flags;
- ACK and acknowledged session ID;
- TIME_CODE decoding.

Run locally when tshark is installed:

```bash
python3 tools/wireshark/validate_dissector.py
```

Write the deterministic sample capture for manual inspection as well:

```bash
python3 tools/wireshark/validate_dissector.py \
  --output /tmp/vspw_tp_sample.pcap
```

The dedicated tooling CI gate installs tshark only in the CI job and runs this validator. Nothing from this directory is linked into the SpWKit library or installed as a runtime dependency.
