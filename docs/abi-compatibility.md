# Public ABI and structure compatibility

This document defines the compatibility rules that SpWKit is freezing before
v1.0. The authoritative binary interface is the installed C11 API. The
optional C++17 wrapper is header-only and delegates to that C API; it does not
define a second shared-library ABI.

## Compatibility families

SpWKit follows SemVer for package compatibility:

| Release line | Shared-library SONAME family | CMake package compatibility |
|---|---|---|
| `0.x` | `0.<minor>` | same minor only |
| `1.x` and later | `<major>` | same major |

A pre-1 minor release may intentionally break ABI. Therefore `0.6` and
`0.7` must not be presented as one binary-compatible family merely because
their SemVer major number is zero. Patch releases within one pre-1 minor line
remain in the same compatibility family.

Once v1.0 is published, the 1.x C ABI is a same-major compatibility promise.

## Type classes

Every installed C type belongs to one of the following classes.

### Opaque handles

`spw_port_t` and `spw_buffer_t` are incomplete public types. Their layout is
private to libspwkit and may change without affecting the public ABI. Callers
must never allocate them by size or inspect their representation.

### Append-only size/version structures

These structures carry `struct_size` and `version` and form explicit
extension points:

| Type | Stable prefix macro |
|---|---|
| `spw_port_config_t` | `SPW_PORT_CONFIG_MIN_SIZE` |
| `spw_simulator_config_t` | `SPW_SIMULATOR_CONFIG_MIN_SIZE` |
| `spw_udp_config_t` | `SPW_UDP_CONFIG_MIN_SIZE` |
| `spw_device_config_t` | `SPW_DEVICE_CONFIG_MIN_SIZE` |
| `spw_driver_ops_t` | `SPW_DRIVER_OPS_MIN_SIZE` |
| `spw_driver_config_t` | `SPW_DRIVER_CONFIG_MIN_SIZE` |
| `spw_raw_ethernet_io_ops_t` | `SPW_RAW_ETHERNET_IO_OPS_MIN_SIZE` |
| `spw_raw_ethernet_config_t` | `SPW_RAW_ETHERNET_CONFIG_MIN_SIZE` |
| `spw_runtime_ops_t` | `SPW_RUNTIME_OPS_MIN_SIZE` |

For the 1.x ABI:

1. existing fields, offsets, types and meanings do not change;
2. compatible extensions may only append fields after the stable prefix;
3. callers set `struct_size` to the number of bytes their compiled structure
   actually provides;
4. a library accepts `struct_size >= *_MIN_SIZE` and ignores unknown tail
   bytes;
5. when a containing API also carries a byte count, such as
   `backend_config_size`, that byte count must be at least `struct_size`;
6. a future library must not read an appended field unless the caller's
   `struct_size` reaches the complete field;
7. fields appended compatibly within 1.x use zero as their absent/default
   semantic unless that extension explicitly defines another backward-safe
   default;
8. the schema `version` is not bumped for a compatible append-only 1.x
   extension. A version change denotes an incompatible schema and is rejected
   with `SPW_ERR_UNSUPPORTED`;
9. internal copies of public configuration structures are zero-filled and
   bounded by the caller's declared size, so an older prefix does not cause a
   read beyond caller-owned storage.

The current baseline places each stable prefix at the complete end of its
structure, with no hidden tail-padding extension slot. This is checked by the
public ABI contract test. Future fields therefore begin after the published
baseline rather than accidentally occupying bytes that an older compiler
considered padding.

### Fixed-layout 1.x value and output structures

The following types are deliberately **not** append-only extension points:

- `spw_capabilities_t`;
- `spw_packet_t`;
- `spw_buffer_view_t`;
- `spw_time_code_t`;
- `spw_statistics_t`;
- `spw_fault_statistics_t`;
- `spw_port_workspace_requirements_t`;
- `spw_udp_fault_rule_t`;
- `spw_driver_buffer_t`.

Their layout is frozen for the 1.x ABI. In particular, output structures are
never enlarged in place during 1.x because an older binary may have allocated
only the original object size. New statistics, capabilities, packet metadata,
or descriptor fields must be introduced through a new type/function or a new
major-version contract instead of writing beyond caller-owned storage.

`spw_udp_fault_rule_t` is embedded as a fixed-size array inside the UDP
configuration and is therefore also fixed for 1.x. New fault-rule schemas need
a new configuration/API surface rather than in-place rule growth.

`spw_driver_buffer_t` crosses the application/vendor driver callback
boundary and is fixed for the same reason.

### Scalar and enum-like domains

The representation of public scalar aliases is fixed for 1.x, including
`spw_result_t`, `spw_timeout_us_t`, `spw_backend_id_t`,
`spw_capability_bits_t`, `spw_ready_events_t`, `spw_link_state_t`,
`spw_terminator_t`, UDP fault action/target types, raw-Ethernet readiness,
driver synchronization direction and driver buffer tokens.

New named values may be added to a domain only where existing callers can
safely ignore or reject an unknown value according to that API's documented
semantics. Existing numeric values are never renumbered in 1.x.

## Source and binary behavior

A current library rejects:

- a size/version structure shorter than its published `*_MIN_SIZE`;
- an unsupported schema version;
- a backend configuration whose advertised `struct_size` exceeds the
  `backend_config_size` supplied by the caller.

A current library accepts a known stable prefix with additional unknown tail
bytes and ignores that tail. This makes a newer-header caller safe with an
older 1.x library when it uses only compatible append-only extensions.

Conversely, a future 1.x library must treat missing appended fields from an
older caller as their documented default and must not access beyond the older
caller's `struct_size`.

These rules govern ABI compatibility only. Normal semantic validation still
applies after a structure has passed the size/version boundary checks.

## Release gates

The ABI policy is executable in CI:

- `public_abi_struct_contract` checks the stable prefix boundaries,
  accepted future tails, rejected undersized structures, rejected unknown
  versions and consistency between `struct_size` and
  `backend_config_size`;
- `package_version_compatibility_policy` loads the generated CMake package
  version file and verifies the selected SemVer compatibility family;
- the current stable-prefix macros are asserted to end exactly at
  `sizeof(type)`, preventing an accidental tail-padding extension hole.

The source/ABI/exported-symbol comparison gates tracked by #213 build on this
policy and provide cross-release regression enforcement. This document defines
what those gates must preserve.
