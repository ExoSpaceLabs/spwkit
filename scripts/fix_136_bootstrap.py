#!/usr/bin/env python3
from pathlib import Path
p = Path('scripts/apply_136_bootstrap.py')
text = p.read_text()
old = '''    "    spw_packet_space_predicate_t predicate;\\n    bool ready;\\n\\n    if (backend->link == NULL) {",
    "    spw_packet_space_predicate_t predicate;\\n    bool ready;\\n\\n    SPW_PROFILE_TX_BACKEND_ENTRY();\\n    if (backend->link == NULL) {",
'''
new = '''    "    spw_simulator_packet_slot_t* slot;\\n    spw_packet_space_predicate_t predicate;\\n    bool ready;\\n\\n    if (backend->link == NULL) {",
    "    spw_simulator_packet_slot_t* slot;\\n    spw_packet_space_predicate_t predicate;\\n    bool ready;\\n\\n    SPW_PROFILE_TX_BACKEND_ENTRY();\\n    if (backend->link == NULL) {",
'''
if text.count(old) != 1:
    raise SystemExit(f'bootstrap simulator anchor block count={text.count(old)}')
p.write_text(text.replace(old, new, 1))
