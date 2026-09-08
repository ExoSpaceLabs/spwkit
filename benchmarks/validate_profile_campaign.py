#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import re
from pathlib import Path

REQUIRED_STATS = {'min', 'median', 'mean', 'p95', 'p99', 'max', 'stddev'}


def load_json(path: Path):
    return json.loads(path.read_text())


def load_jsonl(path: Path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def validate_calibration(row):
    assert row['schema'] == 'spwkit.profile.calibration.v1'
    assert row['measurement_domain'] == 'instrumentation'
    assert row['unit'] == 'counter_ticks'
    assert row['method'] == 'back_to_back_counter_reads'
    assert REQUIRED_STATS == set(row['statistics'])
    assert row['counter']['width_bits'] in (32, 64)
    stats = row['statistics']
    assert stats['min'] <= stats['median'] <= stats['max']
    assert stats['min'] <= stats['p95'] <= stats['max']
    assert stats['min'] <= stats['p99'] <= stats['max']


def validate_driver_comparison(row, direction, fixture):
    assert row['schema'] == 'spwkit.profile.comparison.v1'
    assert row['measurement_domain'] == 'software'
    assert row['unit'] == 'counter_ticks'
    assert row['backend'] == 'driver'
    assert row['direction'] == direction
    assert row['spwkit_path'] == 'copied'
    assert row['native_path'] == 'direct-provider-call'
    assert row['provider_fixture'] == fixture
    assert row['sample_order'] == 'alternating-per-iteration'
    assert row['counter_floor_subtracted'] is False
    assert REQUIRED_STATS == set(row['native_statistics'])
    assert REQUIRED_STATS == set(row['spwkit_statistics'])
    assert row['delta']['definition'] == 'spwkit-minus-native'


def validate_udp_comparison(row, direction):
    assert row['schema'] == 'spwkit.profile.comparison.v1'
    assert row['measurement_domain'] == 'software'
    assert row['unit'] == 'counter_ticks'
    assert row['backend'] == 'udp'
    assert row['direction'] == direction
    assert row['spwkit_path'] == 'vspw-tp'
    assert row['native_path'] == 'direct-udp-socket'
    assert row['boundary'] == 'complete-public-api-operation'
    assert row['provider_fixture'] == 'same-process-loopback-udp'
    assert row['sample_order'] == 'alternating-per-iteration'
    assert row['counter_floor_subtracted'] is False
    assert REQUIRED_STATS == set(row['native_statistics'])
    assert REQUIRED_STATS == set(row['spwkit_statistics'])
    assert row['delta']['definition'] == 'spwkit-minus-native'
    assert 0 <= row['payload_bytes'] <= 4096
    assert 1 <= row['iterations'] <= 4096


def main():
    parser = argparse.ArgumentParser(description='Validate a SpWKit profiling campaign result set')
    parser.add_argument('result_dir', type=Path)
    parser.add_argument('--expected-type', default=None)
    parser.add_argument('--expected-measured', type=int, default=8)
    args = parser.parse_args()

    root = args.result_dir
    metadata = load_json(root / 'campaign.json')
    assert metadata['schema'] == 'spwkit.profile.campaign.v1'
    if args.expected_type is not None:
        assert metadata['result_type'] == args.expected_type
    assert re.fullmatch(r'\d{8}T\d{6}Z', metadata['timestamp_utc'])
    assert re.fullmatch(r'[0-9a-f]{8,}', metadata['git_short_sha'])
    expected_name = f"{metadata['result_type']}-{metadata['timestamp_utc']}-{metadata['git_short_sha']}"
    assert metadata['result_directory_name'] == expected_name
    assert root.name == expected_name
    assert metadata['build_type'] == 'Release'
    assert metadata['clean_rebuild_per_case'] is True
    assert metadata['serial_execution'] is True
    assert metadata['counter_floor_calibration_per_case'] is True
    assert metadata['direct_native_comparison'] is True
    assert metadata['direct_native_comparison_cases'] == ['tx_api_native', 'rx_native_api']
    assert metadata['udp_comparison_cases'] == ['udp_tx', 'udp_rx']
    assert metadata['direct_native_counter_floor_subtracted'] is False
    assert metadata['host_backend_cases'] == ['loopback_tx', 'loopback_rx', 'simulator_tx', 'simulator_rx']
    assert metadata['summary_file'] == 'summary.txt'
    assert metadata['coverage_file'] == 'coverage.json'
    assert metadata['cases']

    rows = load_jsonl(root / 'results.jsonl')
    calibrations = load_jsonl(root / 'calibration.jsonl')
    if not rows:
        raise SystemExit('benchmark campaign emitted no DRIVER layer rows')
    if len(calibrations) != len(metadata['cases']):
        raise SystemExit('benchmark campaign did not emit one calibration row per DRIVER layer case')

    for calibration in calibrations:
        validate_calibration(calibration)

    for row in rows:
        assert row['schema'] == 'spwkit.profile.v1'
        assert row['measurement_domain'] == 'software'
        assert row['unit'] == 'counter_ticks'
        assert row['backend'] == 'driver'
        assert row['direction'] == 'tx'
        assert row['path'] == 'copied'
        assert row['heap_used'] is False
        assert row['sample_storage'] == 'fixed'
        assert REQUIRED_STATS == set(row['statistics'])
        assert 0 <= row['payload_bytes'] <= 4096
        assert 1 <= row['iterations'] <= 4096

    tx_rows = load_jsonl(root / 'comparison' / 'tx_api_native.jsonl')
    rx_rows = load_jsonl(root / 'comparison' / 'rx_native_api.jsonl')
    udp_tx_rows = load_jsonl(root / 'comparison' / 'udp_tx.jsonl')
    udp_rx_rows = load_jsonl(root / 'comparison' / 'udp_rx.jsonl')
    if not tx_rows or not rx_rows or not udp_tx_rows or not udp_rx_rows:
        raise SystemExit('benchmark campaign emitted incomplete comparison results')

    for row in tx_rows:
        validate_driver_comparison(row, 'tx', 'shared-reference-provider-submit')
    for row in rx_rows:
        validate_driver_comparison(row, 'rx', 'shared-reference-provider-receive')
    for row in udp_tx_rows:
        validate_udp_comparison(row, 'tx')
    for row in udp_rx_rows:
        validate_udp_comparison(row, 'rx')

    comparison_calibrations = [
        root / 'comparison' / 'calibration.json',
        root / 'comparison' / 'rx_calibration.json',
        root / 'comparison' / 'udp_tx_calibration.json',
        root / 'comparison' / 'udp_rx_calibration.json',
    ]
    for path in comparison_calibrations:
        validate_calibration(load_json(path))

    expected_backend_cases = {
        'loopback_tx': ('loopback', 'tx'),
        'loopback_rx': ('loopback', 'rx'),
        'simulator_tx': ('simulator', 'tx'),
        'simulator_rx': ('simulator', 'rx'),
    }
    backend_row_count = 0
    for case_name, (backend, direction) in expected_backend_cases.items():
        values = load_jsonl(root / 'backends' / f'{case_name}.jsonl')
        if not values:
            raise SystemExit(f'backend case {case_name} emitted no rows')
        backend_row_count += len(values)
        validate_calibration(load_json(root / 'backend-calibration' / f'{case_name}.json'))
        for row in values:
            assert row['schema'] == 'spwkit.profile.backend.v1'
            assert row['measurement_domain'] == 'software'
            assert row['unit'] == 'counter_ticks'
            assert row['backend'] == backend
            assert row['direction'] == direction
            assert row['path'] == 'standard'
            assert row['boundary'] == 'complete-public-api-operation'
            assert row['carrier'] == 'in-memory-queue'
            assert row['counter_floor_subtracted'] is False
            assert row['heap_used'] is False
            assert row['sample_storage'] == 'fixed'
            assert REQUIRED_STATS == set(row['statistics'])

    coverage = load_json(root / 'coverage.json')
    assert coverage['schema'] == 'spwkit.profile.coverage.v1'
    assert len(coverage['entries']) == 12
    measured = {
        (entry['backend'], entry['path'], entry['direction'])
        for entry in coverage['entries']
        if entry['status'] == 'measured'
    }
    expected_measured = {
        ('driver', 'copied', 'tx'),
        ('driver', 'copied', 'rx'),
        ('loopback', 'standard', 'tx'),
        ('loopback', 'standard', 'rx'),
        ('simulator', 'standard', 'tx'),
        ('simulator', 'standard', 'rx'),
        ('udp', 'vspw-tp', 'tx'),
        ('udp', 'vspw-tp', 'rx'),
    }
    assert expected_measured <= measured
    assert coverage['counts'].get('measured', 0) >= args.expected_measured

    summary = (root / 'summary.txt').read_text()
    for heading in [
        'DRIVER copied TX: direct/provider vs SpWKit',
        'DRIVER copied RX: direct/provider vs SpWKit',
        'UDP VSPW-TP TX: direct socket vs SpWKit',
        'UDP VSPW-TP RX: direct socket vs SpWKit',
        'LOOPBACK TX: complete public API operation',
        'LOOPBACK RX: complete public API operation',
        'SIMULATOR TX: complete public API operation',
        'SIMULATOR RX: complete public API operation',
        'Hosted-backend coverage',
    ]:
        assert heading in summary
    assert f'Measured: {args.expected_measured}/12' in summary

    print(
        f"validated {len(metadata['cases'])} DRIVER layer case(s), "
        f"{len(tx_rows)} DRIVER TX comparison row(s), {len(rx_rows)} DRIVER RX comparison row(s), "
        f"{len(udp_tx_rows)} UDP TX row(s), {len(udp_rx_rows)} UDP RX row(s), "
        f"{backend_row_count} in-memory backend row(s), {args.expected_measured}/12 coverage in {root.name}"
    )


if __name__ == '__main__':
    main()
