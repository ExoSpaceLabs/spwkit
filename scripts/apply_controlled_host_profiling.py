#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


campaign_path = ROOT / "benchmarks" / "run_profile_campaign.sh"
text = campaign_path.read_text()

text = replace_once(
    text,
    'settle_seconds="1"\nresult_type="host"\n',
    'settle_seconds="1"\nsettle_explicit=0\ncontrolled_cpu=""\nprecondition_seconds="0"\ngovernor_mode="keep"\nresult_type="host"\n',
    "campaign defaults",
)

text = replace_once(
    text,
    '  --settle-seconds N    pause after each clean build before timing (default: 1)\n  --type NAME           result target/type, e.g. host, github-hosted, stm32h755\n',
    '  --settle-seconds N    pause after each clean build before timing (default: 1)\n  --controlled-cpu CPU  Linux logical CPU number, or auto; pins each child process tree\n  --precondition-seconds N\n                        busy-warm the selected CPU before each campaign step\n  --governor MODE       keep or performance; performance is attempted when writable\n  --type NAME           result target/type, e.g. host, github-hosted, stm32h755\n',
    "campaign usage",
)

text = replace_once(
    text,
    '    --settle-seconds) settle_seconds="$2"; shift 2 ;;\n    --type) result_type="$2"; shift 2 ;;\n',
    '    --settle-seconds) settle_seconds="$2"; settle_explicit=1; shift 2 ;;\n    --controlled-cpu) controlled_cpu="$2"; shift 2 ;;\n    --precondition-seconds) precondition_seconds="$2"; shift 2 ;;\n    --governor) governor_mode="$2"; shift 2 ;;\n    --type) result_type="$2"; shift 2 ;;\n',
    "campaign option parsing",
)

text = replace_once(
    text,
    'if ! [[ "$warmup" =~ ^[0-9]+$ && "$iterations" =~ ^[0-9]+$ && "$counter_hz" =~ ^[0-9]+$ && "$settle_seconds" =~ ^[0-9]+$ ]]; then\n  echo "warmup, iterations, counter-hz and settle-seconds must be non-negative integers" >&2\n  exit 2\nfi\n',
    'if ! [[ "$warmup" =~ ^[0-9]+$ && "$iterations" =~ ^[0-9]+$ && "$counter_hz" =~ ^[0-9]+$ && "$settle_seconds" =~ ^[0-9]+$ && "$precondition_seconds" =~ ^[0-9]+$ ]]; then\n  echo "warmup, iterations, counter-hz, settle-seconds and precondition-seconds must be non-negative integers" >&2\n  exit 2\nfi\nif [[ "$governor_mode" != "keep" && "$governor_mode" != "performance" ]]; then\n  echo "governor must be keep or performance" >&2\n  exit 2\nfi\n',
    "campaign numeric validation",
)

anchor = 'git_sha="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || printf unknown)"\n'
control_block = r'''controlled_mode=0
selected_cpu=""
governor_path=""
governor_before="unknown"
governor_effective="unknown"
governor_changed=0
governor_change_status="not-requested"
scaling_driver="unknown"
auto_cpu_policy="none"

restore_profile_governor() {
  if (( governor_changed )) && [[ -n "$governor_path" && -w "$governor_path" ]]; then
    printf '%s\n' "$governor_before" > "$governor_path" || true
  fi
}
trap restore_profile_governor EXIT

if [[ -n "$controlled_cpu" ]]; then
  if [[ "$(uname -s)" != "Linux" ]]; then
    echo "controlled CPU profiling is currently supported only on Linux" >&2
    exit 2
  fi
  if ! command -v taskset >/dev/null 2>&1; then
    echo "controlled CPU profiling requires taskset (util-linux)" >&2
    exit 2
  fi

  if [[ "$controlled_cpu" == "auto" ]]; then
    selected_cpu="$(python3 - <<'PY'
import os
from pathlib import Path

allowed = sorted(os.sched_getaffinity(0))
if not allowed:
    raise SystemExit("no CPU is available in the process affinity mask")

def max_khz(cpu):
    path = Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/cpuinfo_max_freq")
    try:
        return int(path.read_text().strip())
    except (OSError, ValueError):
        return -1

print(max(allowed, key=lambda cpu: (max_khz(cpu), -cpu)))
PY
)"
    auto_cpu_policy="highest-cpuinfo-max-freq-among-allowed"
  elif [[ "$controlled_cpu" =~ ^[0-9]+$ ]]; then
    selected_cpu="$controlled_cpu"
    auto_cpu_policy="explicit"
  else
    echo "controlled-cpu must be a logical CPU number or auto" >&2
    exit 2
  fi

  if ! taskset -c "$selected_cpu" true >/dev/null 2>&1; then
    echo "logical CPU $selected_cpu is not available in this process affinity mask" >&2
    exit 2
  fi

  controlled_mode=1
  if (( settle_explicit == 0 )); then
    settle_seconds=0
  fi

  governor_path="/sys/devices/system/cpu/cpu${selected_cpu}/cpufreq/scaling_governor"
  driver_path="/sys/devices/system/cpu/cpu${selected_cpu}/cpufreq/scaling_driver"
  if [[ -r "$governor_path" ]]; then
    governor_before="$(cat "$governor_path")"
    governor_effective="$governor_before"
  fi
  if [[ -r "$driver_path" ]]; then
    scaling_driver="$(cat "$driver_path")"
  fi

  if [[ "$governor_mode" == "performance" ]]; then
    governor_change_status="unavailable"
    if [[ -e "$governor_path" ]]; then
      if [[ -w "$governor_path" ]]; then
        if printf '%s\n' performance > "$governor_path" 2>/dev/null; then
          governor_effective="$(cat "$governor_path" 2>/dev/null || printf unknown)"
          governor_change_status="applied"
          if [[ "$governor_effective" != "$governor_before" ]]; then
            governor_changed=1
          fi
        else
          governor_change_status="write-failed"
        fi
      else
        governor_change_status="not-writable"
      fi
    fi
  else
    governor_change_status="kept"
  fi
fi

profile_precondition_cpu() {
  if (( controlled_mode == 0 || precondition_seconds == 0 )); then
    return 0
  fi
  taskset -c "$selected_cpu" python3 - "$precondition_seconds" <<'PY'
import sys
import time

seconds = int(sys.argv[1])
deadline = time.monotonic() + seconds
value = 1
while time.monotonic() < deadline:
    value = ((value * 1664525) + 1013904223) & 0xffffffff
if value == -1:
    print(value)
PY
}

'''
text = replace_once(text, anchor, control_block + anchor, "controlled-host setup")

old_runner = '''run_campaign_step() {
  local label="$1"
  local log_file="$2"
  shift 2

  if "$@" >"$log_file" 2>&1; then
    return 0
  else
    local status=$?
    printf '\\nERROR: profiling campaign step failed: %s (exit %d)\\n' "$label" "$status" >&2
    printf 'Child log: %s\\n' "$log_file" >&2
    printf '%s\\n' '---------------- child log ----------------' >&2
    cat "$log_file" >&2 || true
    printf '%s\\n' '-------------- end child log --------------' >&2
    printf 'Partial results preserved at: %s\\n' "$output_dir" >&2
    return "$status"
  fi
}
'''
new_runner = '''run_campaign_step() {
  local label="$1"
  local log_file="$2"
  local status
  shift 2

  profile_precondition_cpu
  if (( controlled_mode )); then
    if taskset -c "$selected_cpu" "$@" >"$log_file" 2>&1; then
      return 0
    else
      status=$?
    fi
  else
    if "$@" >"$log_file" 2>&1; then
      return 0
    else
      status=$?
    fi
  fi

  printf '\\nERROR: profiling campaign step failed: %s (exit %d)\\n' "$label" "$status" >&2
  printf 'Child log: %s\\n' "$log_file" >&2
  printf '%s\\n' '---------------- child log ----------------' >&2
  cat "$log_file" >&2 || true
  printf '%s\\n' '-------------- end child log --------------' >&2
  printf 'Partial results preserved at: %s\\n' "$output_dir" >&2
  return "$status"
}
'''
text = replace_once(text, old_runner, new_runner, "campaign child wrapper")

text = replace_once(
    text,
    "printf '  serial execution: yes\\n' >&2\nprintf '  direct/native comparison: DRIVER copied TX + RX\\n' >&2\n",
    "printf '  serial execution: yes\\n' >&2\nif (( controlled_mode )); then\n  printf '  controlled host: CPU %s, precondition %ss, settle %ss\\n' \"$selected_cpu\" \"$precondition_seconds\" \"$settle_seconds\" >&2\n  printf '  cpufreq: driver=%s governor(before=%s effective=%s requested=%s status=%s)\\n' \"$scaling_driver\" \"$governor_before\" \"$governor_effective\" \"$governor_mode\" \"$governor_change_status\" >&2\nelse\n  printf '  controlled host: disabled\\n' >&2\nfi\nprintf '  direct/native comparison: DRIVER copied TX + RX\\n' >&2\n",
    "campaign control display",
)

text = replace_once(
    text,
    'export SPWKIT_CAMPAIGN_DEVICE_CASES="${device_comparison_cases[*]}"\n',
    'export SPWKIT_CAMPAIGN_DEVICE_CASES="${device_comparison_cases[*]}"\nexport SPWKIT_CAMPAIGN_CONTROLLED_MODE="$controlled_mode"\nexport SPWKIT_CAMPAIGN_CONTROLLED_CPU_REQUEST="$controlled_cpu"\nexport SPWKIT_CAMPAIGN_SELECTED_CPU="$selected_cpu"\nexport SPWKIT_CAMPAIGN_PRECONDITION_SECONDS="$precondition_seconds"\nexport SPWKIT_CAMPAIGN_GOVERNOR_REQUEST="$governor_mode"\nexport SPWKIT_CAMPAIGN_GOVERNOR_BEFORE="$governor_before"\nexport SPWKIT_CAMPAIGN_GOVERNOR_EFFECTIVE="$governor_effective"\nexport SPWKIT_CAMPAIGN_GOVERNOR_STATUS="$governor_change_status"\nexport SPWKIT_CAMPAIGN_SCALING_DRIVER="$scaling_driver"\nexport SPWKIT_CAMPAIGN_AUTO_CPU_POLICY="$auto_cpu_policy"\n',
    "campaign control exports",
)

text = replace_once(
    text,
    "    'settle_seconds_after_build': int(os.environ['SPWKIT_CAMPAIGN_SETTLE_SECONDS']),\n}",
    "    'settle_seconds_after_build': int(os.environ['SPWKIT_CAMPAIGN_SETTLE_SECONDS']),\n    'host_control': {\n        'enabled': os.environ['SPWKIT_CAMPAIGN_CONTROLLED_MODE'] == '1',\n        'requested_cpu': os.environ['SPWKIT_CAMPAIGN_CONTROLLED_CPU_REQUEST'] or None,\n        'selected_cpu': int(os.environ['SPWKIT_CAMPAIGN_SELECTED_CPU']) if os.environ['SPWKIT_CAMPAIGN_SELECTED_CPU'] else None,\n        'auto_cpu_policy': os.environ['SPWKIT_CAMPAIGN_AUTO_CPU_POLICY'],\n        'precondition_seconds': int(os.environ['SPWKIT_CAMPAIGN_PRECONDITION_SECONDS']),\n        'requested_governor': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_REQUEST'],\n        'governor_before': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_BEFORE'],\n        'governor_effective': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_EFFECTIVE'],\n        'governor_change_status': os.environ['SPWKIT_CAMPAIGN_GOVERNOR_STATUS'],\n        'scaling_driver': os.environ['SPWKIT_CAMPAIGN_SCALING_DRIVER'],\n        'child_process_affinity': 'taskset-single-cpu' if os.environ['SPWKIT_CAMPAIGN_CONTROLLED_MODE'] == '1' else 'inherited',\n    },\n}",
    "campaign metadata",
)

campaign_path.write_text(text)

summary_path = ROOT / "benchmarks" / "summarize_profile_campaign.py"
summary = summary_path.read_text()
summary = replace_once(
    summary,
    '    lines.append(f"Build      : {campaign[\'build_type\']} / clean serial cases")\n    lines.append("Units      : architectural counter ticks")\n',
    '    lines.append(f"Build      : {campaign[\'build_type\']} / clean serial cases")\n    control = campaign.get("host_control", {})\n    if control.get("enabled"):\n        lines.append(f"CPU        : pinned logical CPU {control.get(\'selected_cpu\')} ({control.get(\'auto_cpu_policy\')})")\n        lines.append(f"Precond.   : {control.get(\'precondition_seconds\', 0)}s before each campaign step")\n        lines.append(f"Governor   : requested={control.get(\'requested_governor\')} effective={control.get(\'governor_effective\')} driver={control.get(\'scaling_driver\')} status={control.get(\'governor_change_status\')}")\n    else:\n        lines.append("CPU        : uncontrolled / inherited scheduler affinity")\n    lines.append("Units      : architectural counter ticks")\n',
    "summary control header",
)
summary = replace_once(
    summary,
    '    else:\n        lines.append("NOTE: Local timing is suitable for controlled-host characterization only when host conditions are recorded and kept stable.")\n    lines.append("Standalone counter-floor calibration is diagnostic and is not subtracted from measured intervals.")\n',
    '    else:\n        control = campaign.get("host_control", {})\n        if not control.get("enabled"):\n            lines.append("WARNING: Local campaign was not CPU-pinned; use run_controlled_profile_campaign.sh for reference characterization.")\n        elif control.get("requested_governor") == "performance" and control.get("governor_effective") != "performance":\n            lines.append("WARNING: performance governor was requested but not effective; cross-case absolute timing may still drift.")\n        else:\n            lines.append("NOTE: Controlled-host affinity and cpufreq state were recorded for this campaign.")\n    lines.append("Standalone counter-floor calibration is diagnostic and is not subtracted from measured intervals.")\n',
    "summary control note",
)
summary_path.write_text(summary)

wrapper = ROOT / "benchmarks" / "run_controlled_profile_campaign.sh"
wrapper.write_text(r'''#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

exec "$ROOT_DIR/benchmarks/run_profile_campaign.sh" \
  --controlled-cpu auto \
  --precondition-seconds 2 \
  --governor performance \
  --settle-seconds 0 \
  "$@"
''')
wrapper.chmod(0o755)

print("controlled-host profiling patch applied")
