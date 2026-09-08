#!/usr/bin/env python3
from pathlib import Path

p = Path('benchmarks/run_profile_campaign.sh')
s = p.read_text()

old = '''# Child benchmark runners emit machine-readable calibration JSON on stderr.
# Keep those records in their artifact files, but do not spray JSON blobs into
# the human-facing campaign console. Other diagnostics and errors still pass.
exec 3>&2
exec 2> >(grep -vE 'counter floor: \\{.*\\}$' >&3)

'''
s = s.replace(old, '', 1)

old = '''         "$output_dir/backends" \\
         "$output_dir/backend-calibration"
: > "$output_dir/results.jsonl"
'''
new = '''         "$output_dir/backends" \\
         "$output_dir/backend-calibration" \\
         "$output_dir/logs"
: > "$output_dir/results.jsonl"
'''
if old not in s:
    raise SystemExit('output directory block not found')
s = s.replace(old, new, 1)

marker = ': > "$output_dir/calibration.jsonl"\n\n'
helper = r'''run_campaign_step() {
  local label="$1"
  local log_file="$2"
  shift 2

  if "$@" >"$log_file" 2>&1; then
    return 0
  else
    local status=$?
    printf '\nERROR: profiling campaign step failed: %s (exit %d)\n' "$label" "$status" >&2
    printf 'Child log: %s\n' "$log_file" >&2
    printf '%s\n' '---------------- child log ----------------' >&2
    cat "$log_file" >&2 || true
    printf '%s\n' '-------------- end child log --------------' >&2
    printf 'Partial results preserved at: %s\n' "$output_dir" >&2
    return "$status"
  fi
}

'''
if marker not in s:
    raise SystemExit('helper insertion point not found')
s = s.replace(marker, marker + helper, 1)

replacements = [
    ('  "$ROOT_DIR/benchmarks/run_profile_benchmark.sh" \\\n', '  run_campaign_step "DRIVER layer $case_name" "$output_dir/logs/driver-layer-$case_name.log" \\\n    "$ROOT_DIR/benchmarks/run_profile_benchmark.sh" \\\n'),
    ('"$ROOT_DIR/benchmarks/run_native_comparison.sh" \\\n', 'run_campaign_step "DRIVER copied TX native comparison" "$output_dir/logs/native-tx-comparison.log" \\\n  "$ROOT_DIR/benchmarks/run_native_comparison.sh" \\\n'),
    ('bash "$ROOT_DIR/benchmarks/run_zero_copy_comparison.sh" \\\n', 'run_campaign_step "DRIVER copied vs zero-copy TX" "$output_dir/logs/zero-copy-tx.log" \\\n  bash "$ROOT_DIR/benchmarks/run_zero_copy_comparison.sh" \\\n'),
    ('bash "$ROOT_DIR/benchmarks/run_native_receive_comparison.sh" \\\n', 'run_campaign_step "DRIVER copied RX native comparison" "$output_dir/logs/native-rx-comparison.log" \\\n  bash "$ROOT_DIR/benchmarks/run_native_receive_comparison.sh" \\\n'),
    ('bash "$ROOT_DIR/benchmarks/run_zero_copy_receive_comparison.sh" \\\n', 'run_campaign_step "DRIVER copied vs zero-copy RX" "$output_dir/logs/zero-copy-rx.log" \\\n  bash "$ROOT_DIR/benchmarks/run_zero_copy_receive_comparison.sh" \\\n'),
    ('  bash "$ROOT_DIR/benchmarks/run_host_backend_benchmark.sh" \\\n', '  run_campaign_step "$backend/$direction backend" "$output_dir/logs/${case_name}.log" \\\n    bash "$ROOT_DIR/benchmarks/run_host_backend_benchmark.sh" \\\n'),
    ('  bash "$ROOT_DIR/benchmarks/run_udp_comparison.sh" \\\n', '  run_campaign_step "UDP $direction comparison" "$output_dir/logs/udp_${direction}.log" \\\n    bash "$ROOT_DIR/benchmarks/run_udp_comparison.sh" \\\n'),
    ('    bash "$ROOT_DIR/benchmarks/run_device_comparison.sh" \\\n', '    run_campaign_step "DEVICE $direction comparison" "$output_dir/logs/device_${direction}.log" \\\n      bash "$ROOT_DIR/benchmarks/run_device_comparison.sh" \\\n'),
]
for old, new in replacements:
    if old not in s:
        raise SystemExit(f'command pattern not found: {old!r}')
    s = s.replace(old, new, 1)

# Remove the old campaign-level stdout discard together with the preceding
# line continuation. Leaving the continuation behind would accidentally join
# the next shell statement to the child command.
s = s.replace(' \\\n    > /dev/null\n', '\n')
s = s.replace(' \\\n  > /dev/null\n', '\n')
if '> /dev/null' in s:
    raise SystemExit('unhandled stdout discard remains in campaign')

p.write_text(s)
