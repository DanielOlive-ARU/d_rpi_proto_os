#!/usr/bin/env bash
# Validate that a benchmark log contains the full expected BENCH / BENCH_META
# line set. Exit 0 means the log is structurally complete; exit 1 means one or
# more required lines are missing. Non-zero failures= fields are flagged as a
# warning but do not fail the log (aggregation policy decides how to treat them).
#
# Usage: scripts/validate_bench_log.sh <log>
# Loop:  for f in results/<session>/*.log; do scripts/validate_bench_log.sh "$f"; done

set -euo pipefail

if [ $# -ne 1 ]; then
  echo "usage: $0 <log>" >&2
  exit 2
fi

log="$1"
[ -f "$log" ] || { echo "$log: not a file" >&2; exit 2; }

required=(
  'BENCH_META .*phase=start'
  'BENCH_META .*phase=end'
)

if grep -q 'BENCH_META .*phase=fault_injected' "$log"; then
  kind=recovery
  required+=('BENCH_META .*phase=fault_injected')
  required+=('BENCH test=recovery_window ')
else
  kind=latency
  required+=('BENCH test=sys_write ')
  required+=('BENCH test=ipc_roundtrip ')
fi

missing=()
for pat in "${required[@]}"; do
  grep -q -- "$pat" "$log" || missing+=("$pat")
done

nonzero=$(grep -oE 'failures=[0-9]+' "$log" | awk -F= '$2 != "0"' || true)

if [ ${#missing[@]} -eq 0 ]; then
  if [ -n "$nonzero" ]; then
    echo "$log: OK ($kind) WARN non-zero $(echo "$nonzero" | tr '\n' ' ')"
  else
    echo "$log: OK ($kind)"
  fi
  exit 0
fi

echo "$log: BAD ($kind) missing: ${missing[*]}"
exit 1
