#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    path.write_text(text.replace(old, new, 1))


profile = Path('integrations/stm32h755_dma/profile_impl.inc')
replace_once(
    profile,
    '''static int __attribute__((unused)) profile_copied_once(spw_port_t* port,
                               size_t length,
                               int measure_rx,
                               uint32_t* out_cycles) {
    spw_packet_t tx = {
        g_profile_tx_data, length, length, SPW_TERMINATOR_EOP
    };
    spw_packet_t rx = {
        g_profile_rx_data, 0u, sizeof(g_profile_rx_data), SPW_TERMINATOR_EOP
    };

    spw_profile_reset();
    if (spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    if (!measure_rx && !profile_capture_delta(out_cycles)) {
        return 0;
    }
    if (spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        rx.length != length) {
        return 0;
    }
    if (measure_rx && !profile_capture_delta(out_cycles)) {
        return 0;
    }
    return 1;
}
''',
    '''static int __attribute__((unused)) profile_copied_once(spw_port_t* port,
                               size_t length,
                               int measure_rx,
                               uint32_t* out_cycles) {
    spw_packet_t tx = {
        g_profile_tx_data, length, length, SPW_TERMINATOR_EOP
    };
    spw_packet_t rx = {
        g_profile_rx_data, 0u, sizeof(g_profile_rx_data), SPW_TERMINATOR_EOP
    };
    spw_result_t result;

    spw_profile_reset();
    result = spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return 0x10 + (int)(-result & 0x0f);
    }
    if (!measure_rx && !profile_capture_delta(out_cycles)) {
        return 0x20;
    }
    result = spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return 0x30 + (int)(-result & 0x0f);
    }
    if (rx.length != length) {
        return 0x40;
    }
    if (measure_rx && !profile_capture_delta(out_cycles)) {
        return 0x50;
    }
    return 0;
}
''',
    'copied operation diagnostics',
)
replace_once(
    profile,
    '''static int profile_operation_once(spw_port_t* port,
                                  size_t length,
                                  uint32_t* out_cycles) {
#if SPWKIT_STM32_PROFILE_CASE == 1
    return profile_copied_once(port, length, 0, out_cycles);
#elif SPWKIT_STM32_PROFILE_CASE == 2
    return profile_copied_once(port, length, 1, out_cycles);
#else
    return profile_zc_once(port, length, out_cycles);
#endif
}
''',
    '''static int profile_operation_once(spw_port_t* port,
                                  size_t length,
                                  uint32_t* out_cycles) {
#if SPWKIT_STM32_PROFILE_CASE == 1
    return profile_copied_once(port, length, 0, out_cycles);
#elif SPWKIT_STM32_PROFILE_CASE == 2
    return profile_copied_once(port, length, 1, out_cycles);
#else
    return profile_zc_once(port, length, out_cycles) ? 0 : 0x7f;
#endif
}
''',
    'operation return convention',
)
replace_once(
    profile,
    '''        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_WARMUP; ++index) {
            if (!profile_operation_once(port, length, &cycles)) {
                return (int)(0x900u + row);
            }
        }
        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS; ++index) {
            if (!profile_operation_once(port, length, &cycles)) {
                return (int)(0xa00u + row);
            }
            g_stm32h755_spwkit_profile.rows[row].samples[index] = cycles;
        }
''',
    '''        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_WARMUP; ++index) {
            const int operation_result = profile_operation_once(port, length, &cycles);
            if (operation_result != 0) {
                return (int)(0x9000u | (row << 8u) | (uint32_t)operation_result);
            }
        }
        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS; ++index) {
            const int operation_result = profile_operation_once(port, length, &cycles);
            if (operation_result != 0) {
                return (int)(0xa000u | (row << 8u) | (uint32_t)operation_result);
            }
            g_stm32h755_spwkit_profile.rows[row].samples[index] = cycles;
        }
''',
    'run-profile failure encoding',
)

runner = Path('scripts/stm32h755_profile_campaign.sh')
replace_once(
    runner,
    '''fail_campaign() {
  local status="$1"
  shift
  cleanup_openocd
  printf '\\nERROR: %s\\n' "$*" >&2
  if [[ -d "$output_dir" ]]; then
    local failure_archive="${output_dir}.failed.tar"
    tar -cf "$failure_archive" -C "$(dirname "$output_dir")" "$(basename "$output_dir")" >/dev/null 2>&1 || true
    printf 'Partial results : %s\\n' "$output_dir" >&2
    [[ -s "$failure_archive" ]] && printf 'Failure archive : %s\\n' "$failure_archive" >&2
  fi
  exit "$status"
}
''',
    '''archive_results() {
  local archive="$1"
  rm -f -- "$archive"
  if ! tar -cf "$archive" -C "$(dirname "$output_dir")" "$(basename "$output_dir")"; then
    printf 'ERROR: tar failed while creating %s\\n' "$archive" >&2
    rm -f -- "$archive"
    return 1
  fi
  if [[ ! -s "$archive" ]]; then
    printf 'ERROR: tar reported success but archive is missing/empty: %s\\n' "$archive" >&2
    rm -f -- "$archive"
    return 1
  fi
  printf 'Archive       : %s\\n' "$archive" >&2
}

fail_campaign() {
  local status="$1"
  shift
  cleanup_openocd
  printf '\\nERROR: %s\\n' "$*" >&2

  if [[ -n "${raw_build:-}" && -s "${raw_build:-}" && -n "${raw_out:-}" ]]; then
    cp "$raw_build" "$raw_out" || printf 'WARNING: could not preserve raw evidence at %s\\n' "$raw_out" >&2
  fi

  if [[ -d "$output_dir" ]]; then
    local failure_archive="${output_dir}.failed.tar"
    printf 'Partial results : %s\\n' "$output_dir" >&2
    if ! archive_results "$failure_archive"; then
      printf 'ERROR: automatic failure archive creation failed; results remain in %s\\n' "$output_dir" >&2
    fi
  fi
  exit "$status"
}
''',
    'verified archive creation',
)
replace_once(
    runner,
    '''  if (( gdb_rc == 124 )); then
    fail_campaign 124 "GDB timed out after ${DEBUG_TIMEOUT}s for $case_name"
  fi
  if (( gdb_rc != 0 )) || ! grep -q '^RESULT: PASS$' "$gdb_log"; then
    status="$gdb_rc"
    (( status == 0 )) && status=1
    fail_campaign "$status" "Physical profile case $case_name failed"
  fi
  [[ -s "$raw_build" ]] || fail_campaign 1 "Bulk profile evidence dump missing for $case_name"
  cp "$raw_build" "$raw_out"
''',
    '''  [[ -s "$raw_build" ]] && cp "$raw_build" "$raw_out"

  if (( gdb_rc == 124 )); then
    fail_campaign 124 "GDB timed out after ${DEBUG_TIMEOUT}s for $case_name"
  fi
  if (( gdb_rc != 0 )) || ! grep -q '^RESULT: PASS$' "$gdb_log"; then
    status="$gdb_rc"
    (( status == 0 )) && status=1
    fail_campaign "$status" "Physical profile case $case_name failed"
  fi
  [[ -s "$raw_out" ]] || fail_campaign 1 "Bulk profile evidence dump missing for $case_name"
''',
    'preserve raw before validation',
)
replace_once(
    runner,
    '''archive="${output_dir}.tar"
tar -cf "$archive" -C "$(dirname "$output_dir")" "$(basename "$output_dir")"
printf 'Human summary : %s/summary.txt\\n' "$output_dir" >&2
printf 'Machine data  : %s/cases/*.json\\n' "$output_dir" >&2
printf 'Raw evidence  : %s/cases/*.raw\\n' "$output_dir" >&2
printf 'Archive       : %s\\n' "$archive" >&2
printf '%s\\n' "$output_dir"
''',
    '''archive="${output_dir}.tar"
archive_results "$archive" || exit 1
printf 'Human summary : %s/summary.txt\\n' "$output_dir" >&2
printf 'Machine data  : %s/cases/*.json\\n' "$output_dir" >&2
printf 'Raw evidence  : %s/cases/*.raw\\n' "$output_dir" >&2
printf '%s\\n' "$output_dir"
''',
    'verified success archive',
)
