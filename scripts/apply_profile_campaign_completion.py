#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_between(text: str, start_marker: str, end_marker: str, replacement: str) -> str:
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    return text[:start] + replacement + text[end:]


# -----------------------------------------------------------------------------
# benchmarks/summarize_profile_campaign.py
# -----------------------------------------------------------------------------
path = ROOT / "benchmarks/summarize_profile_campaign.py"
text = path.read_text()
text = text.replace(
    "import argparse\nimport json\nimport math\nimport platform\nfrom pathlib import Path\n",
    "import argparse\nimport json\nimport math\nimport os\nimport platform\nimport shlex\nimport subprocess\nfrom pathlib import Path\n",
)

new_coverage = r'''def command_version(command: str) -> str:
    try:
        completed = subprocess.run(
            [*shlex.split(command), "--version"],
            check=False,
            capture_output=True,
            text=True,
            timeout=3,
        )
    except (OSError, subprocess.SubprocessError, ValueError):
        return "unknown"
    combined = "\n".join(part for part in (completed.stdout, completed.stderr) if part)
    for line in combined.splitlines():
        if line.strip():
            return line.strip()
    return "unknown"


def cpu_model() -> str:
    system = platform.system()
    if system == "Linux":
        try:
            for line in Path("/proc/cpuinfo").read_text(errors="replace").splitlines():
                key, separator, value = line.partition(":")
                if separator and key.strip() in {"model name", "Hardware"} and value.strip():
                    return value.strip()
        except OSError:
            pass
    elif system == "Darwin":
        try:
            completed = subprocess.run(
                ["sysctl", "-n", "machdep.cpu.brand_string"],
                check=False,
                capture_output=True,
                text=True,
                timeout=3,
            )
            if completed.stdout.strip():
                return completed.stdout.strip()
        except (OSError, subprocess.SubprocessError):
            pass
    value = platform.processor() or os.environ.get("PROCESSOR_IDENTIFIER", "")
    return value.strip() or "unknown"


def process_affinity() -> str:
    if hasattr(os, "sched_getaffinity"):
        try:
            return ",".join(str(cpu) for cpu in sorted(os.sched_getaffinity(0))) or "unknown"
        except OSError:
            pass
    return "unknown"


def process_priority() -> str:
    if hasattr(os, "getpriority") and hasattr(os, "PRIO_PROCESS"):
        try:
            return str(os.getpriority(os.PRIO_PROCESS, 0))
        except OSError:
            pass
    return "unknown"


def discover_backend_capabilities():
    system = platform.system()
    udp_supported = system in {"Linux", "Darwin", "Windows"}
    device_supported = system == "Linux"

    def capability(platform_supported, build_enabled, benchmark_implemented, reason):
        return {
            "platform_supported": bool(platform_supported),
            "build_enabled": bool(build_enabled),
            "benchmark_implemented": bool(benchmark_implemented),
            "reason": reason,
        }

    return {
        "driver": capability(True, True, True, "portable DRIVER benchmark fixture is part of the campaign"),
        "loopback": capability(True, True, True, "LOOPBACK benchmark fixture is part of the campaign"),
        "simulator": capability(True, True, True, "SIMULATOR benchmark fixture is enabled by the campaign"),
        "udp": capability(
            udp_supported,
            udp_supported,
            True,
            "VSPW-TP/UDP benchmark is available on POSIX/Winsock hosts"
            if udp_supported else "VSPW-TP/UDP runtime is unsupported on this platform",
        ),
        "device": capability(
            device_supported,
            device_supported,
            True,
            "DEVICE/VSPD benchmark is available on Linux"
            if device_supported else "DEVICE/VSPD runtime is Linux-only",
        ),
    }


def finalize_campaign(root: Path, campaign):
    calibrations = load_jsonl(root / "calibration.jsonl")
    calibration = calibrations[0] if calibrations else {}
    calibration_platform = calibration.get("platform", {})
    calibration_compiler = calibration.get("compiler", {})
    calibration_counter = calibration.get("counter", {})
    compiler_command = os.environ.get("CC") or "cc"
    control = campaign.get("host_control", {})
    if control.get("enabled") and control.get("selected_cpu") is not None:
        measurement_affinity = f"cpu:{control['selected_cpu']}"
    else:
        measurement_affinity = process_affinity()

    campaign["schema"] = "spwkit.profile.campaign.v2"
    campaign["host_environment"] = {
        "os_name": platform.system() or "unknown",
        "kernel_release": platform.release() or "unknown",
        "architecture": platform.machine() or calibration_platform.get("architecture", "unknown"),
        "cpu_model": cpu_model(),
        "compiler": {
            "family": calibration_compiler.get("family", "unknown"),
            "command": compiler_command,
            "version": command_version(compiler_command),
        },
        "counter": {
            "kind": calibration_counter.get("kind", "unknown"),
            "width_bits": calibration_counter.get("width_bits", "unknown"),
            "frequency_hz": calibration_counter.get("frequency_hz", "unknown"),
        },
        "measurement_affinity": measurement_affinity,
        "orchestrator_affinity": process_affinity(),
        "process_priority": process_priority(),
    }
    campaign["backend_capabilities"] = discover_backend_capabilities()
    (root / "campaign.json").write_text(json.dumps(campaign, indent=2) + "\n")
    return campaign


def classify_coverage_status(capability, is_measured):
    if is_measured:
        return "measured", None
    if not capability["platform_supported"]:
        return "unsupported-platform", capability["reason"]
    if not capability["build_enabled"]:
        return "not-built", capability["reason"]
    if not capability["benchmark_implemented"]:
        return "not-implemented-benchmark", capability["reason"]
    return "not-implemented-benchmark", "benchmark result missing from completed campaign"


def coverage_entries(root: Path, campaign):
    capabilities = campaign["backend_capabilities"]

    def measured(path: str):
        return (root / path).exists() and (root / path).stat().st_size > 0

    def entry(backend, path_name, direction, result_path, measured_reason):
        is_measured = measured(result_path)
        status, fallback_reason = classify_coverage_status(capabilities[backend], is_measured)
        return {
            "backend": backend,
            "path": path_name,
            "direction": direction,
            "status": status,
            "reason": measured_reason if is_measured else fallback_reason,
        }

    entries = [
        entry("driver", "copied", "tx", "comparison/tx_api_native.jsonl",
              "same-process direct/provider comparison"),
        entry("driver", "copied", "rx", "comparison/rx_native_api.jsonl",
              "same-process direct/provider comparison"),
        entry("driver", "zero-copy", "tx", "comparison/driver_tx_copy_zero_copy.jsonl",
              "provider-owned DMA-buffer copied-vs-zero-copy comparison"),
        entry("driver", "zero-copy", "rx", "comparison/driver_rx_copy_zero_copy.jsonl",
              "provider-owned RX DMA-buffer copied-vs-zero-copy comparison"),
        entry("loopback", "standard", "tx", "backends/loopback_tx.jsonl",
              "complete public API operation"),
        entry("loopback", "standard", "rx", "backends/loopback_rx.jsonl",
              "complete public API operation"),
        entry("simulator", "standard", "tx", "backends/simulator_tx.jsonl",
              "complete public API operation"),
        entry("simulator", "standard", "rx", "backends/simulator_rx.jsonl",
              "complete public API operation"),
        entry("udp", "vspw-tp", "tx", "comparison/udp_tx.jsonl",
              "same-process direct UDP socket comparison"),
        entry("udp", "vspw-tp", "rx", "comparison/udp_rx.jsonl",
              "same-process direct UDP socket comparison"),
        entry("device", "vspd", "tx", "comparison/device_tx.jsonl",
              "same-vspwd-daemon direct VSPD comparison"),
        entry("device", "vspd", "rx", "comparison/device_rx.jsonl",
              "same-vspwd-daemon direct VSPD comparison"),
    ]
    counts = {}
    for item in entries:
        counts[item["status"]] = counts.get(item["status"], 0) + 1
    return {
        "schema": "spwkit.profile.coverage.v2",
        "entries": entries,
        "counts": counts,
    }
'''
text = replace_between(text, "def coverage_entries(root: Path):", "\n\ndef append_comparison", new_coverage.rstrip())

old_render = '''    lines.append(f"Build      : {campaign['build_type']} / clean serial cases")\n    control = campaign.get("host_control", {})\n'''
new_render = '''    lines.append(f"Build      : {campaign['build_type']} / clean serial cases")\n    environment = campaign.get("host_environment", {})\n    compiler = environment.get("compiler", {})\n    counter = environment.get("counter", {})\n    lines.append(f"Host       : {environment.get('os_name', 'unknown')} {environment.get('kernel_release', 'unknown')} / {environment.get('architecture', 'unknown')}")\n    lines.append(f"CPU model  : {environment.get('cpu_model', 'unknown')}")\n    lines.append(f"Compiler   : {compiler.get('family', 'unknown')} / {compiler.get('version', 'unknown')}")\n    lines.append(f"Counter    : {counter.get('kind', 'unknown')} / {counter.get('width_bits', 'unknown')} bits / {counter.get('frequency_hz', 'unknown')} Hz")\n    lines.append(f"Affinity   : measurement={environment.get('measurement_affinity', 'unknown')} orchestrator={environment.get('orchestrator_affinity', 'unknown')}")\n    lines.append(f"Priority   : {environment.get('process_priority', 'unknown')}")\n    control = campaign.get("host_control", {})\n'''
if old_render not in text:
    raise SystemExit("summary render marker not found")
text = text.replace(old_render, new_render)

old_main = '''    campaign = load_json(root / "campaign.json")\n    coverage = coverage_entries(root)\n    (root / "coverage.json").write_text(json.dumps(coverage, indent=2) + "\\n")\n'''
new_main = '''    campaign = finalize_campaign(root, load_json(root / "campaign.json"))\n    coverage = coverage_entries(root, campaign)\n    (root / "coverage.json").write_text(json.dumps(coverage, indent=2) + "\\n")\n'''
if old_main not in text:
    raise SystemExit("summary main marker not found")
text = text.replace(old_main, new_main)
path.write_text(text)


# -----------------------------------------------------------------------------
# benchmarks/validate_profile_campaign.py
# -----------------------------------------------------------------------------
path = ROOT / "benchmarks/validate_profile_campaign.py"
text = path.read_text()
text = text.replace(
    "import re\nfrom pathlib import Path\n\nREQUIRED_STATS = {'min', 'median', 'mean', 'p95', 'p99', 'max', 'stddev'}\n",
    "import re\nfrom pathlib import Path\n\nimport summarize_profile_campaign as campaign_summary\n\nREQUIRED_STATS = {'min', 'median', 'mean', 'p95', 'p99', 'max', 'stddev'}\nALLOWED_COVERAGE_STATUSES = {'measured', 'unsupported-platform', 'not-built', 'not-implemented-benchmark'}\nREQUIRED_BACKEND_CAPABILITIES = {'driver', 'loopback', 'simulator', 'udp', 'device'}\n",
)

insert_marker = "\ndef validate_calibration(row):\n"
helpers = r'''
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

'''
if insert_marker not in text:
    raise SystemExit("validator calibration marker not found")
text = text.replace(insert_marker, "\n" + helpers + "def validate_calibration(row):\n", 1)

text = text.replace(
    "    assert metadata['schema'] == 'spwkit.profile.campaign.v1'\n",
    "    validate_classifier_contract()\n    assert metadata['schema'] == 'spwkit.profile.campaign.v2'\n",
    1,
)
metadata_marker = "    assert metadata['cases']\n\n    rows = load_jsonl(root / 'results.jsonl')\n"
metadata_replacement = "    assert metadata['cases']\n    validate_campaign_environment(metadata)\n    assert metadata['build_type'] == 'Release'\n    assert isinstance(metadata['git_sha'], str) and metadata['git_sha']\n\n    rows = load_jsonl(root / 'results.jsonl')\n"
if metadata_marker not in text:
    raise SystemExit("validator metadata marker not found")
text = text.replace(metadata_marker, metadata_replacement, 1)

coverage_old = '''    coverage = load_json(root / 'coverage.json')\n    assert coverage['schema'] == 'spwkit.profile.coverage.v1'\n    assert len(coverage['entries']) == 12\n    measured = {\n'''
coverage_new = '''    coverage = load_json(root / 'coverage.json')\n    assert coverage['schema'] == 'spwkit.profile.coverage.v2'\n    assert len(coverage['entries']) == 12\n    assert set(coverage['counts']) <= ALLOWED_COVERAGE_STATUSES\n    assert sum(coverage['counts'].values()) == len(coverage['entries'])\n    for entry in coverage['entries']:\n        assert entry['status'] in ALLOWED_COVERAGE_STATUSES\n        capability = metadata['backend_capabilities'][entry['backend']]\n        if entry['status'] == 'measured':\n            assert capability['platform_supported']\n            assert capability['build_enabled']\n            assert capability['benchmark_implemented']\n        elif entry['status'] == 'unsupported-platform':\n            assert not capability['platform_supported']\n        elif entry['status'] == 'not-built':\n            assert capability['platform_supported']\n            assert not capability['build_enabled']\n        elif entry['status'] == 'not-implemented-benchmark':\n            assert capability['platform_supported']\n            assert capability['build_enabled']\n            assert not capability['benchmark_implemented']\n    measured = {\n'''
if coverage_old not in text:
    raise SystemExit("validator coverage marker not found")
text = text.replace(coverage_old, coverage_new, 1)
path.write_text(text)


# -----------------------------------------------------------------------------
# benchmarks/README.md
# -----------------------------------------------------------------------------
path = ROOT / "benchmarks/README.md"
text = path.read_text()
old = "The campaign metadata repeats the result type, UTC timestamp, full commit SHA, short SHA and directory name so copied/archived result sets remain self-identifying.\n"
new = old + "\nThe finalized `campaign.json` also records OS/kernel, architecture, CPU model, compiler family/version, architectural counter kind/width/frequency, process priority, measurement/orchestrator affinity, cpufreq control state, and explicit backend capability/build availability. Unknown host values are written as `unknown` rather than inferred. `coverage.json` distinguishes `measured`, `unsupported-platform`, `not-built`, and `not-implemented-benchmark` states.\n"
if old not in text:
    raise SystemExit("benchmark README marker not found")
path.write_text(text.replace(old, new, 1))


# Remove this one-shot patcher and its workflow from the resulting commit.
(ROOT / "scripts/apply_profile_campaign_completion.py").unlink(missing_ok=True)
(ROOT / ".github/workflows/pre-v1-audit-bootstrap.yml").unlink(missing_ok=True)
