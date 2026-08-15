# Virtual backend

This directory contains the implemented process-local two-peer SpaceWire simulator used for same-host integration and contract verification.

The backend is selected as `SPW_BACKEND_SIMULATOR` through the normal public port configuration. Two endpoints with the same `link_id` and opposite A/B pairing labels form equal peers.

Implemented behavior includes:

- packet boundaries and EOP/EEP preservation;
- link start/stop/reset and peer recovery;
- time codes;
- bounded packet/time-code queues;
- immediate/finite/infinite waits;
- statistics;
- deterministic resource exhaustion;
- zero-copy ownership emulation using fixed aligned host memory.

The simulator does not depend on Ethernet transport and does not model LVDS/Data-Strobe electrical behavior or exact character timing.
