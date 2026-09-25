# Public ABI and structure compatibility

This document defines the pre-1 compatibility policy being hardened for the
SpWKit 1.x C ABI.

## Compatibility families

SpWKit follows SemVer for binary/package compatibility:

- while the project major version is `0`, each minor line is a separate ABI
  family; for example, `0.7.x` may be compatible with other `0.7.x`
  releases but does not claim ABI compatibility with `0.8.x`;
- from `1.0.0` onward, the major version defines the ABI family and compatible
  `1.x` releases retain the 1.x public C ABI.

CMake expresses the same rule:

- pre-1 shared libraries use `SOVERSION 0.<minor>`;
- pre-1 package discovery uses `SameMinorVersion`;
- v1+ shared libraries use `SOVERSION <major>`;
- v1+ package discovery uses `SameMajorVersion`.

## Size-versioned input structures

The following public input/provider structures are append-only extensible:

| Type | Current contract generation | Published minimum extent |
|---|---:|---|
| `spw_port_config_t` | 1 | `SPW_PORT_CONFIG_V1_MIN_SIZE` |
| `spw_simulator_config_t` | 1 | `SPW_SIMULATOR_CONFIG_V1_MIN_SIZE` |
| `spw_udp_config_t` | 3 | `SPW_UDP_CONFIG_V3_MIN_SIZE` |
| `spw_device_config_t` | 1 | `SPW_DEVICE_CONFIG_V1_MIN_SIZE` |
| `spw_driver_ops_t` | 2 | `SPW_DRIVER_OPS_V2_MIN_SIZE` |
| `spw_driver_config_t` | 2 | `SPW_DRIVER_CONFIG_V2_MIN_SIZE` |
| `spw_raw_ethernet_io_ops_t` | 1 | `SPW_RAW_ETHERNET_IO_OPS_V1_MIN_SIZE` |
| `spw_raw_ethernet_config_t` | 1 | `SPW_RAW_ETHERNET_CONFIG_V1_MIN_SIZE` |
| `spw_runtime_ops_t` | 1 | `SPW_RUNTIME_OPS_V1_MIN_SIZE` |

For these structures:

1. `struct_size` is the number of caller-owned bytes available from the
   beginning of the structure.
2. A library accepts a known contract generation when `struct_size` is at
   least that generation's published minimum extent.
3. A larger `struct_size` is valid. The library reads only fields it knows.
4. Compatible 1.x additions are appended after the published minimum boundary.
   Existing fields are never reordered, removed, resized or repurposed.
5. Compatible appended fields do not require a structure-version bump. Missing
   appended fields use the documented default, normally zero/NULL unless the
   type explicitly defines another default.
6. A different/unknown `version` is not guessed or reinterpreted and returns
   `SPW_ERR_UNSUPPORTED`.
7. Internal copies are bounded by the caller-declared extent so an older,
   shorter structure is never over-read.

The current UDP generation number 3 and DRIVER generation number 2 predate the
1.0 ABI freeze. They are structure-contract generations, not SpWKit library
major versions.

Initializers continue to set `struct_size = sizeof(current_type)`; the
published minimum extent exists for compatibility validation, not as a reason
for new code to allocate truncated structures.

## Fixed-size public value structures

The following structures are fixed for the 1.x ABI and are **not** extended in
place:

- `spw_capabilities_t`;
- `spw_packet_t`;
- `spw_buffer_view_t`;
- `spw_time_code_t`;
- `spw_statistics_t`;
- `spw_fault_statistics_t`;
- `spw_port_workspace_requirements_t`;
- `spw_driver_buffer_t`;
- `spw_udp_fault_rule_t`.

If 1.x needs information that does not fit one of these types, SpWKit adds a
new type/function or another explicitly versioned query instead of growing the
existing object. This prevents a newer library from writing beyond storage
allocated by an older binary.

## Scalars and opaque handles

Public scalar/result/flag types such as `spw_result_t`,
`spw_timeout_us_t`, `spw_backend_id_t`, `spw_terminator_t`,
`spw_link_state_t`, capability/readiness bitsets and driver tokens retain
their declared representation for the 1.x ABI.

`spw_port_t` and `spw_buffer_t` remain opaque. Their private representation
may change without changing the public ABI.

## Compatibility test

`public_abi_structure_contract` is the executable guard for this policy. It
checks every published minimum extent at compile time and verifies that the
core port configuration:

- accepts its published historical extent;
- rejects an extent shorter than that contract;
- accepts extra trailing bytes;
- rejects an unknown structure generation.

The minimum-size constants intentionally name the historical contract extent.
When a compatible field is appended later, the existing constant and test stay
unchanged. The test therefore becomes an old-structure/new-library regression
test automatically.
