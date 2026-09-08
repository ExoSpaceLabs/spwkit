#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise SystemExit(f"{label} not found")
    return text.replace(old, new, 1)


campaign_path = Path("benchmarks/run_profile_campaign.sh")
campaign = campaign_path.read_text()
campaign = replace_once(
    campaign,
    "set -euo pipefail\n\nROOT_DIR=",
    """set -euo pipefail

# Child benchmark runners emit machine-readable calibration JSON on stderr.
# Keep those records in their artifact files, but do not spray JSON blobs into
# the human-facing campaign console. Other diagnostics and errors still pass.
exec 3>&2
exec 2> >(grep -vE 'counter floor: \\{.*\\}$' >&3)

ROOT_DIR=""",
    "campaign stderr insertion point",
)
campaign = replace_once(
    campaign,
    """printf '\\n' >&2
python3 \"$ROOT_DIR/benchmarks/summarize_profile_campaign.py\" \"$output_dir\" >&2
printf 'Campaign complete: %s\\n' \"$output_dir\" >&2
printf '%s\\n' \"$output_dir\"
""",
    """printf '\\n==================== PROFILING RESULTS ====================\\n' >&2
python3 \"$ROOT_DIR/benchmarks/summarize_profile_campaign.py\" \"$output_dir\" >&2
printf '===========================================================\\n' >&2
printf 'Human-readable summary : %s/summary.txt\\n' \"$output_dir\" >&2
printf 'Machine-readable data  : %s/*.json, %s/*.jsonl\\n' \"$output_dir\" \"$output_dir\" >&2
printf 'Campaign complete      : %s\\n' \"$output_dir\" >&2
printf '%s\\n' \"$output_dir\"
""",
    "campaign summary footer",
)
campaign_path.write_text(campaign)

summary_path = Path("benchmarks/summarize_profile_campaign.py")
summary = summary_path.read_text()
summary = replace_once(
    summary,
    "import json\nimport platform\n",
    "import json\nimport math\nimport platform\n",
    "summary imports",
)
summary = replace_once(
    summary,
    '''def fmt(value, digits=3):
    if value is None:
        return "n/a"
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"
''',
    '''def rounded_tick(value):
    """Round a counter-tick statistic to the nearest representable tick."""
    numeric = float(value)
    if numeric >= 0:
        return math.floor(numeric + 0.5)
    return math.ceil(numeric - 0.5)


def fmt_ticks(value):
    if value is None:
        return "n/a"
    return str(rounded_tick(value))


def fmt_percent(value):
    if value is None:
        return "n/a"
    return f"{float(value):.1f}"
''',
    "summary formatter block",
)
summary = summary.replace("fmt(", "fmt_ticks(")
summary = summary.replace("fmt_ticks(delta['median_percent'])", "fmt_percent(delta['median_percent'])")
summary = replace_once(
    summary,
    '''    lines.append(f"Build      : {campaign['build_type']} / clean serial cases")
    lines.append("")
''',
    '''    lines.append(f"Build      : {campaign['build_type']} / clean serial cases")
    lines.append("Units      : architectural counter ticks")
    lines.append("Display    : tick values rounded to nearest integer; JSON retains full precision")
    lines.append("")
''',
    "summary header insertion point",
)
summary = replace_once(
    summary,
    '    case_files = sorted((root / "cases").glob("*.jsonl"))\n',
    '''    case_files = [
        root / "cases" / f"{case_name}.jsonl"
        for case_name in campaign.get("cases", [])
        if (root / "cases" / f"{case_name}.jsonl").exists()
    ]
''',
    "case ordering line",
)
summary = replace_once(
    summary,
    '    if campaign["result_type"] == "github-hosted":\n',
    '''    lines.append("Result artifacts")
    lines.append("----------------")
    lines.append("  summary.txt                    ordered human-readable result")
    lines.append("  comparison/*.jsonl             exact comparison data")
    lines.append("  cases/*.jsonl                  exact paired-range data")
    lines.append("  calibration/*.json             exact counter-floor calibration")
    lines.append("  coverage.json / campaign.json  campaign metadata and coverage")
    lines.append("")

    if campaign["result_type"] == "github-hosted":
''',
    "artifact section insertion point",
)
summary_path.write_text(summary)
print("profiling output cleanup applied")
