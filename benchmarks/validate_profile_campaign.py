#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import platform
import re
from pathlib import Path

import summarize_profile_campaign as campaign_summary

REQUIRED_STATS = {'min', 'median', 'mean', 'p95', 'p99', 'max', 'stddev'}
ALLOWED_COVERAGE_STATUSES = {'measured', 'unsupported-platform', 'not-built', 'not-implemented-benchmark'}
REQUIRED_BACKEND_CAPABILITIES = {'driver', 'loopback', 'simulator', 'udp', 'device'}


def load_json(path: Path):
    return json.loads(path.read_text())


def load_jsonl(path: Path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]



def validate_classifier_contract():
    measured = {
        "platform_supported": True,
        "build_enabled": True,
        "benchmark_implemented": True,
        "reason": "test",
    }
    unsupported = {**measured, "platform_supported": False}
    not_built = {**measured, "build_enabled": False}
    not_implemented = {**measured, "benchmark_implemented": False}
    assert campaign_summary.classify_coverage_status(measured, True)[0] == "measured"
    assert campaign_summary.classify_coverage_status(unsupported, False)[0] == "unsupported-platform"
    assert campaign_summary.classify_coverage_status(not_built, False)[0] == "not-built"
    assert campaign_summary.classify_coverage_status(not_implemented, False)[0] == "not-implemented-benchmark"


def validate_campaign_environment(metadata):
    environment = metadata['host_environment']
    for key in ('os_name', 'kernel_release', 'architecture', 'cpu_model',
                'measurement_affinity', 'orchestrator_affinity', 'process_priority'):
        assert isinstance(environment[key], str) and environment[key]
    compiler = environment['compiler']
    for key in ('family', 'command', 'version'):
        assert isinstance(compiler[key], str) and compiler[key]
    counter = environment['counter']
    assert isinstance(counter['kind'], str) and counter['kind']
    assert counter['width_bits'] == 'unknown' or counter['width_bits'] in (32, 64)
    assert counter['frequency_hz'] == 'unknown' or isinstance(counter['frequency_hz'], int)

    capabilities = metadata['backend_capabilities']
    assert set(capabilities) == REQUIRED_BACKEND_CAPABILITIES
    for capability in capabilities.values():
        assert isinstance(capability['platform_supported'], bool)
        assert isinstance(capability['build_enabled'], bool)
        assert isinstance(capability['benchmark_implemented'], bool)
        assert isinstance(capability['reason'], str) and capability['reason']

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


def validate_zero_copy_comparison(row):
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


def validate_zero_copy_rx_comparison(row):
    assert row['schema'] == 'spwkit.profile.zero-copy-rx-comparison.v1'
    assert row['measurement_domain'] == 'software'
    assert row['unit'] == 'counter_ticks'
    assert row['backend'] == 'driver'
    assert row['direction'] == 'rx'
    assert row['fixture'] == 'provider-owned-dma-buffer-reference'
    assert row['boundary'] == 'provider-data-ready-to-application-visibility'
    assert row['copied_path'] == 'provider-storage-to-caller-buffer-copy'
    assert row['zero_copy_path'] == 'acquire-provider-storage-directly'
    assert row['release_outside_visibility_interval'] is True
    assert row['provider_storage_shared'] is True
    assert row['sync_hook_present'] is False
    assert row['sample_order'] == 'alternating-per-iteration'
    assert row['counter_floor_subtracted'] is False
    assert REQUIRED_STATS == set(row['copied_visibility_statistics'])
    assert REQUIRED_STATS == set(row['zero_copy_visibility_statistics'])
    assert set(row['ownership_statistics']) == {'copied_api_call', 'acquire', 'release'}
    for stats in row['ownership_statistics'].values():
        assert REQUIRED_STATS == set(stats)
    assert row['delta']['definition'] == 'zero-copy-minus-copied'
    assert isinstance(row['delta']['zero_copy_faster_by_median'], bool)
    assert set(row['effective_software_throughput']) == {'copied', 'zero_copy'}
    assert 0 <= row['payload_bytes'] <= 4096
    assert 1 <= row['iterations'] <= 4096


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


def validate_device_comparison(row, direction):
    assert row['schema'] == 'spwkit.profile.comparison.v1'
    assert row['measurement_domain'] == 'software'
    assert row['unit'] == 'counter_ticks'
    assert row['backend'] == 'device'
    assert row['direction'] == direction
    assert row['spwkit_path'] == 'vspd'
    assert row['native_path'] == 'direct-vspd-seqpacket'
    assert row['boundary'] == 'complete-public-api-operation'
    assert row['provider_fixture'] == 'same-vspwd-daemon-peer-pair'
    assert row['carrier'] == 'af-unix-seqpacket'
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
    parser.add_argument('--expected-measured', type=int, default=12 if platform.system() == 'Linux' else 10)
    args = parser.parse_args()

    root = args.result_dir
    metadata = load_json(root / 'campaign.json')
    validate_classifier_contract()
    assert metadata['schema'] == 'spwkit.profile.campaign.v2'
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
    assert metadata['zero_copy_comparison_cases'] == ['driver_tx_copy_zero_copy', 'driver_rx_copy_zero_copy']
    assert metadata['udp_comparison_cases'] == ['udp_tx', 'udp_rx']
    if platform.system() == 'Linux':
        assert metadata['device_comparison_cases'] == ['device_tx', 'device_rx']
    else:
        assert metadata['device_comparison_cases'] == []
    assert metadata['direct_native_counter_floor_subtracted'] is False
    assert metadata['host_backend_cases'] == ['loopback_tx', 'loopback_rx', 'simulator_tx', 'simulator_rx']
    assert metadata['summary_file'] == 'summary.txt'
    assert metadata['coverage_file'] == 'coverage.json'
    assert metadata['cases']
    validate_campaign_environment(metadata)
    assert metadata['build_type'] == 'Release'
    assert isinstance(metadata['git_sha'], str) and metadata['git_sha']

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
    zero_copy_rows = load_jsonl(root / 'comparison' / 'driver_tx_copy_zero_copy.jsonl')
    zero_copy_rx_rows = load_jsonl(root / 'comparison' / 'driver_rx_copy_zero_copy.jsonl')
    udp_tx_rows = load_jsonl(root / 'comparison' / 'udp_tx.jsonl')
    udp_rx_rows = load_jsonl(root / 'comparison' / 'udp_rx.jsonl')
    if not tx_rows or not rx_rows or not zero_copy_rows or not zero_copy_rx_rows or not udp_tx_rows or not udp_rx_rows:
        raise SystemExit('benchmark campaign emitted incomplete comparison results')

    for row in tx_rows:
        validate_driver_comparison(row, 'tx', 'shared-reference-provider-submit')
    for row in rx_rows:
        validate_driver_comparison(row, 'rx', 'shared-reference-provider-receive')
    for row in zero_copy_rows:
        validate_zero_copy_comparison(row)
    for row in zero_copy_rx_rows:
        validate_zero_copy_rx_comparison(row)
    for row in udp_tx_rows:
        validate_udp_comparison(row, 'tx')
    for row in udp_rx_rows:
        validate_udp_comparison(row, 'rx')

    comparison_calibrations = [
        root / 'comparison' / 'calibration.json',
        root / 'comparison' / 'rx_calibration.json',
        root / 'comparison' / 'driver_tx_copy_zero_copy_calibration.json',
        root / 'comparison' / 'driver_rx_copy_zero_copy_calibration.json',
        root / 'comparison' / 'udp_tx_calibration.json',
        root / 'comparison' / 'udp_rx_calibration.json',
    ]

    device_tx_rows = []
    device_rx_rows = []
    if platform.system() == 'Linux':
        device_tx_rows = load_jsonl(root / 'comparison' / 'device_tx.jsonl')
        device_rx_rows = load_jsonl(root / 'comparison' / 'device_rx.jsonl')
        if not device_tx_rows or not device_rx_rows:
            raise SystemExit('benchmark campaign emitted incomplete DEVICE comparison results')
        for row in device_tx_rows:
            validate_device_comparison(row, 'tx')
        for row in device_rx_rows:
            validate_device_comparison(row, 'rx')
        comparison_calibrations.extend([
            root / 'comparison' / 'device_tx_calibration.json',
            root / 'comparison' / 'device_rx_calibration.json',
        ])

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
    assert coverage['schema'] == 'spwkit.profile.coverage.v2'
    assert len(coverage['entries']) == 12
    assert set(coverage['counts']) <= ALLOWED_COVERAGE_STATUSES
    assert sum(coverage['counts'].values()) == len(coverage['entries'])
    for entry in coverage['entries']:
        assert entry['status'] in ALLOWED_COVERAGE_STATUSES
        capability = metadata['backend_capabilities'][entry['backend']]
        if entry['status'] == 'measured':
            assert capability['platform_supported']
            assert capability['build_enabled']
            assert capability['benchmark_implemented']
        elif entry['status'] == 'unsupported-platform':
            assert not capability['platform_supported']
        elif entry['status'] == 'not-built':
            assert capability['platform_supported']
            assert not capability['build_enabled']
        elif entry['status'] == 'not-implemented-benchmark':
            assert capability['platform_supported']
            assert capability['build_enabled']
            assert not capability['benchmark_implemented']
    measured = {
        (entry['backend'], entry['path'], entry['direction'])
        for entry in coverage['entries']
        if entry['status'] == 'measured'
    }
    expected_measured = {
        ('driver', 'copied', 'tx'),
        ('driver', 'copied', 'rx'),
        ('driver', 'zero-copy', 'tx'),
        ('driver', 'zero-copy', 'rx'),
        ('loopback', 'standard', 'tx'),
        ('loopback', 'standard', 'rx'),
        ('simulator', 'standard', 'tx'),
        ('simulator', 'standard', 'rx'),
        ('udp', 'vspw-tp', 'tx'),
        ('udp', 'vspw-tp', 'rx'),
    }
    if platform.system() == 'Linux':
        expected_measured |= {
            ('device', 'vspd', 'tx'),
            ('device', 'vspd', 'rx'),
        }
    assert expected_measured <= measured
    assert coverage['counts'].get('measured', 0) >= args.expected_measured

    summary = (root / 'summary.txt').read_text()
    headings = [
        'DRIVER copied TX: direct/provider vs SpWKit',
        'DRIVER copied RX: direct/provider vs SpWKit',
        'DRIVER TX: copied vs zero-copy DMA-buffer preparation',
        'DRIVER RX: copied vs zero-copy DMA-buffer visibility',
        'UDP VSPW-TP TX: direct socket vs SpWKit',
        'UDP VSPW-TP RX: direct socket vs SpWKit',
        'LOOPBACK TX: complete public API operation',
        'LOOPBACK RX: complete public API operation',
        'SIMULATOR TX: complete public API operation',
        'SIMULATOR RX: complete public API operation',
        'Hosted-backend coverage',
    ]
    if platform.system() == 'Linux':
        headings.extend([
            'DEVICE VSPD TX: direct VSPD vs SpWKit',
            'DEVICE VSPD RX: direct VSPD vs SpWKit',
        ])
    for heading in headings:
        assert heading in summary
    assert f'Measured: {args.expected_measured}/12' in summary

    print(
        f"validated {len(metadata['cases'])} DRIVER layer case(s), "
        f"{len(tx_rows)} DRIVER TX comparison row(s), {len(rx_rows)} DRIVER RX comparison row(s), "
        f"{len(zero_copy_rows)} DRIVER copied/zero-copy TX row(s), "
        f"{len(zero_copy_rx_rows)} DRIVER copied/zero-copy RX row(s), "
        f"{len(udp_tx_rows)} UDP TX row(s), {len(udp_rx_rows)} UDP RX row(s), "
        f"{len(device_tx_rows)} DEVICE TX row(s), {len(device_rx_rows)} DEVICE RX row(s), "
        f"{backend_row_count} in-memory backend row(s), {args.expected_measured}/12 coverage in {root.name}"
    )


if __name__ == '__main__':
    main()
