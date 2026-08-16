from pathlib import Path

p = Path("tests/device/run_spwmon.sh")
text = p.read_text()

old = '''wait_for() {
  local description="$1"
  local command="$2"
  for _ in $(seq 1 200); do
    if eval "$command"; then
      return 0
    fi
    sleep 0.02
  done
  echo "timed out waiting for $description" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}
'''
new = '''wait_socket() {
  for _ in $(seq 1 200); do
    [[ -S "$socket" ]] && return 0
    sleep 0.02
  done
  echo "timed out waiting for daemon socket" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}

wait_grep() {
  local description="$1"
  local pattern="$2"
  local file="$3"
  for _ in $(seq 1 200); do
    if [[ -f "$file" ]] && grep -q "$pattern" "$file"; then
      return 0
    fi
    sleep 0.02
  done
  echo "timed out waiting for $description" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}

wait_recovered_run() {
  for _ in $(seq 1 200); do
    if [[ -f "$tmpdir/monitor.log" ]]; then
      local count
      count="$(grep -c '\"state\":\"RUN\"' "$tmpdir/monitor.log" || true)"
      if [[ "$count" -ge 2 ]]; then
        return 0
      fi
    fi
    sleep 0.02
  done
  echo "timed out waiting for recovered monitor RUN" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}
'''
if old not in text:
    raise SystemExit("wait_for helper not found")
text = text.replace(old, new, 1)

replacements = {
    'wait_for "daemon socket" "[[ -S \'$socket\' ]]"': 'wait_socket',
    'wait_for "port 0 RUN" "grep -q \'^RUN$\' \'$tmpdir/p0.log\'"': 'wait_grep "port 0 RUN" "^RUN$" "$tmpdir/p0.log"',
    'wait_for "port 1 RUN" "grep -q \'^RUN$\' \'$tmpdir/p1.log\'"': 'wait_grep "port 1 RUN" "^RUN$" "$tmpdir/p1.log"',
    'wait_for "initial monitor RUN" "grep -q \'\\"state\\":\\"RUN\\"\' \'$tmpdir/monitor.log\'"': 'wait_grep "initial monitor RUN" \'"state":"RUN"\' "$tmpdir/monitor.log"',
    'wait_for "ERROR_WAIT snapshot" "grep -q \'\\"state\\":\\"ERROR_WAIT\\"\' \'$tmpdir/monitor.log\'"': 'wait_grep "ERROR_WAIT snapshot" \'"state":"ERROR_WAIT"\' "$tmpdir/monitor.log"',
    'wait_for "replacement port 1 RUN" "grep -q \'^RUN$\' \'$tmpdir/p1b.log\'"': 'wait_grep "replacement port 1 RUN" "^RUN$" "$tmpdir/p1b.log"',
    'wait_for "recovered monitor RUN" "[[ $(grep -c \'\\"state\\":\\"RUN\\"\' \'$tmpdir/monitor.log\') -ge 2 ]]"': 'wait_recovered_run',
}
for old_line, new_line in replacements.items():
    if old_line not in text:
        raise SystemExit(f"expected line not found: {old_line}")
    text = text.replace(old_line, new_line, 1)

p.write_text(text)
print("spwmon test harness fixed")
