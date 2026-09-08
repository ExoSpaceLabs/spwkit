#!/usr/bin/env python3
from pathlib import Path


def patch(path, old, new, label):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# Campaign integration
# ---------------------------------------------------------------------------
patch(
    "benchmarks/run_profile_campaign.sh",
    "The campaign includes paired DRIVER layers, direct/native DRIVER TX/RX,\nLOOPBACK/SIMULATOR TX/RX, VSPW-TP/UDP TX/RX, and on Linux DEVICE/VSPD TX/RX.\n",
    "The campaign includes paired DRIVER layers, direct/native DRIVER TX/RX,\nDRIVER copied-vs-zero-copy TX, LOOPBACK/SIMULATOR TX/RX, VSPW-TP/UDP TX/RX,\nand on Linux DEVICE/VSPD TX/RX.\n",
    "campaign usage",
)
patch(
    "benchmarks/run_profile_campaign.sh",
    "printf '  direct/native comparison: DRIVER copied TX + RX\\n' >&2\n",
    "printf '  direct/native comparison: DRIVER copied TX + RX\\n' >&2\nprintf '  copy-elimination comparison: DRIVER copied vs zero-copy TX\\n' >&2\n",
    "campaign banner",
)
anchor = '''rx_comparison_output="$output_dir/comparison/rx_native_api.jsonl"
'''
insert = '''zero_copy_comparison_output="$output_dir/comparison/driver_tx_copy_zero_copy.jsonl"
zero_copy_comparison_calibration="$output_dir/comparison/driver_tx_copy_zero_copy_calibration.json"
printf '\\n[campaign comparison] DRIVER copied vs zero-copy TX\\n' >&2
bash "$ROOT_DIR/benchmarks/run_zero_copy_comparison.sh" \\
  --warmup "$warmup" \\
  --iterations "$iterations" \\
  --payloads "$payloads" \\
  --counter-hz "$counter_hz" \\
  --settle-seconds "$settle_seconds" \\
  --build-dir "$campaign_build_root/driver-tx-copy-zero-copy" \\
  --output "$zero_copy_comparison_output" \\
  --calibration-output "$zero_copy_comparison_calibration" \\
  > /dev/null

rx_comparison_output="$output_dir/comparison/rx_native_api.jsonl"
'''
patch("benchmarks/run_profile_campaign.sh", anchor, insert, "zero-copy campaign execution")
patch(
    "benchmarks/run_profile_campaign.sh",
    "    'direct_native_comparison_cases': ['tx_api_native', 'rx_native_api'],\n",
    "    'direct_native_comparison_cases': ['tx_api_native', 'rx_native_api'],\n    'zero_copy_comparison_cases': ['driver_tx_copy_zero_copy'],\n",
    "campaign metadata",
)


# ---------------------------------------------------------------------------
# Summary + coverage
# ---------------------------------------------------------------------------
patch(
    "benchmarks/summarize_profile_campaign.py",
    "    rx_driver = measured(\"comparison/rx_native_api.jsonl\")\n",
    "    rx_driver = measured(\"comparison/rx_native_api.jsonl\")\n    zero_copy_tx = measured(\"comparison/driver_tx_copy_zero_copy.jsonl\")\n",
    "coverage zero-copy detection",
)
patch(
    "benchmarks/summarize_profile_campaign.py",
    '''        {"backend": "driver", "path": "zero-copy", "direction": "tx", "status": "not-implemented-benchmark", "reason": "tracked by #138/#159/#160"},
''',
    '''        {
            "backend": "driver",
            "path": "zero-copy",
            "direction": "tx",
            "status": "measured" if zero_copy_tx else "not-implemented-benchmark",
            "reason": "provider-owned DMA-buffer copied-vs-zero-copy comparison" if zero_copy_tx else "tracked by #138/#160",
        },
''',
    "coverage zero-copy TX entry",
)
summary_anchor = '''def append_backend(lines, title, backend_path: Path):
'''
summary_function = '''def append_zero_copy(lines, comparison_path: Path):
    rows = load_jsonl(comparison_path)
    if not rows:
        return
    lines.append("DRIVER TX: copied vs zero-copy DMA-buffer preparation")
    lines.append("----------------------------------------------------------------")
    lines.append("Payload   Copied med   ZC med   ZC-Copy   Delta %   Acquire   Submit")
    for row in rows:
        copied = row["copied_statistics"]
        zero_copy = row["zero_copy_statistics"]
        ownership = row["ownership_statistics"]
        delta = row["delta"]
        lines.append(
            f"{row['payload_bytes']:>6} B   "
            f"{fmt(copied['median']):>10}   "
            f"{fmt(zero_copy['median']):>6}   "
            f"{fmt(delta['median_ticks']):>7}   "
            f"{fmt(delta['median_percent']):>7}   "
            f"{fmt(ownership['acquire']['median']):>7}   "
            f"{fmt(ownership['submit']['median']):>6}"
        )
    lines.append("")


def append_backend(lines, title, backend_path: Path):
'''
patch("benchmarks/summarize_profile_campaign.py", summary_anchor, summary_function, "summary zero-copy renderer")
patch(
    "benchmarks/summarize_profile_campaign.py",
    '''    append_comparison(lines, "DRIVER copied RX: direct/provider vs SpWKit", root / "comparison" / "rx_native_api.jsonl")
''',
    '''    append_comparison(lines, "DRIVER copied RX: direct/provider vs SpWKit", root / "comparison" / "rx_native_api.jsonl")
    append_zero_copy(lines, root / "comparison" / "driver_tx_copy_zero_copy.jsonl")
''',
    "summary zero-copy section",
)


# ---------------------------------------------------------------------------
# Machine-readable validation
# ---------------------------------------------------------------------------
validator = Path("benchmarks/validate_profile_campaign.py")
text = validator.read_text()
old = "parser.add_argument('--expected-measured', type=int, default=10 if platform.system() == 'Linux' else 8)"
new = "parser.add_argument('--expected-measured', type=int, default=11 if platform.system() == 'Linux' else 9)"
if text.count(old) != 1:
    raise RuntimeError("validator default coverage mismatch")
text = text.replace(old, new, 1)

anchor = '''def validate_udp_comparison(row, direction):
'''
function = '''def validate_zero_copy_comparison(row):
    assert row['schema'] == 'spwkit.profile.zero-copy-comparison.v1'
    assert row['measurement_domain'] == 'software'
    assert row['unit'] == 'counter_ticks'
    assert row['backend'] == 'driver'
    assert row['direction'] == 'tx'
    assert row['fixture'] == 'provider-owned-dma-buffer-reference'
    assert row['boundary'] == 'application-preparation-to-provider-native-boundary'
    assert row['copied_path'] == 'caller-fill-plus-explicit-provider-copy'
    assert row['zero_copy_path'] == 'acquire-direct-fill-submit'
    assert row['completion_cleanup'] == 'reclaim-plus-release-outside-total'
    assert row['provider_storage_shared'] is True
    assert row['sync_hook_present'] is False
    assert row['sample_order'] == 'alternating-per-iteration'
    assert row['counter_floor_subtracted'] is False
    assert REQUIRED_STATS == set(row['copied_statistics'])
    assert REQUIRED_STATS == set(row['zero_copy_statistics'])
    assert set(row['ownership_statistics']) == {'acquire', 'submit', 'reclaim', 'release'}
    for stats in row['ownership_statistics'].values():
        assert REQUIRED_STATS == set(stats)
    assert row['delta']['definition'] == 'zero-copy-minus-copied'
    assert isinstance(row['delta']['zero_copy_faster_by_median'], bool)
    assert set(row['effective_software_throughput']) == {'copied', 'zero_copy'}
    assert 0 <= row['payload_bytes'] <= 4096
    assert 1 <= row['iterations'] <= 4096


def validate_udp_comparison(row, direction):
'''
if text.count(anchor) != 1:
    raise RuntimeError("validator function anchor mismatch")
text = text.replace(anchor, function, 1)

old = "    assert metadata['direct_native_comparison_cases'] == ['tx_api_native', 'rx_native_api']\n"
new = old + "    assert metadata['zero_copy_comparison_cases'] == ['driver_tx_copy_zero_copy']\n"
if text.count(old) != 1:
    raise RuntimeError("validator metadata anchor mismatch")
text = text.replace(old, new, 1)

old = '''    udp_tx_rows = load_jsonl(root / 'comparison' / 'udp_tx.jsonl')
'''
new = '''    zero_copy_rows = load_jsonl(root / 'comparison' / 'driver_tx_copy_zero_copy.jsonl')
    udp_tx_rows = load_jsonl(root / 'comparison' / 'udp_tx.jsonl')
'''
if text.count(old) != 1:
    raise RuntimeError("validator zero-copy load anchor mismatch")
text = text.replace(old, new, 1)

old = '''    if not tx_rows or not rx_rows or not udp_tx_rows or not udp_rx_rows:
        raise SystemExit('benchmark campaign emitted incomplete comparison results')
'''
new = '''    if not tx_rows or not rx_rows or not zero_copy_rows or not udp_tx_rows or not udp_rx_rows:
        raise SystemExit('benchmark campaign emitted incomplete comparison results')
'''
if text.count(old) != 1:
    raise RuntimeError("validator comparison completeness mismatch")
text = text.replace(old, new, 1)

old = '''    for row in rx_rows:
        validate_driver_comparison(row, 'rx', 'shared-reference-provider-receive')
'''
new = '''    for row in rx_rows:
        validate_driver_comparison(row, 'rx', 'shared-reference-provider-receive')
    for row in zero_copy_rows:
        validate_zero_copy_comparison(row)
'''
if text.count(old) != 1:
    raise RuntimeError("validator comparison validation anchor mismatch")
text = text.replace(old, new, 1)

old = '''        root / 'comparison' / 'rx_calibration.json',
'''
new = '''        root / 'comparison' / 'rx_calibration.json',
        root / 'comparison' / 'driver_tx_copy_zero_copy_calibration.json',
'''
if text.count(old) != 1:
    raise RuntimeError("validator calibration anchor mismatch")
text = text.replace(old, new, 1)

old = '''        ('driver', 'copied', 'rx'),
'''
new = '''        ('driver', 'copied', 'rx'),
        ('driver', 'zero-copy', 'tx'),
'''
if text.count(old) != 1:
    raise RuntimeError("validator measured set anchor mismatch")
text = text.replace(old, new, 1)

old = '''        'DRIVER copied RX: direct/provider vs SpWKit',
'''
new = '''        'DRIVER copied RX: direct/provider vs SpWKit',
        'DRIVER TX: copied vs zero-copy DMA-buffer preparation',
'''
if text.count(old) != 1:
    raise RuntimeError("validator summary heading anchor mismatch")
text = text.replace(old, new, 1)

old = '''        f"{len(tx_rows)} DRIVER TX comparison row(s), {len(rx_rows)} DRIVER RX comparison row(s), "
'''
new = '''        f"{len(tx_rows)} DRIVER TX comparison row(s), {len(rx_rows)} DRIVER RX comparison row(s), "
        f"{len(zero_copy_rows)} DRIVER copied/zero-copy TX row(s), "
'''
if text.count(old) != 1:
    raise RuntimeError("validator print anchor mismatch")
text = text.replace(old, new, 1)
validator.write_text(text)

print("zero-copy campaign integration applied")
