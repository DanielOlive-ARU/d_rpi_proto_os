#!/usr/bin/env bash
# Summarise a benchmark session directory. For each benchmark, collect the
# per-run median_cycles (or per-run cycles for recovery_window) across all
# matching logs and report n / min / median / max of that distribution.
#
# Primary aggregate is median-of-run-medians for latency, and median-of-samples
# for recovery_window. Logs that fail validate_bench_log.sh should be excluded
# before running this; this tool does not filter invalid logs itself.
#
# Usage: scripts/aggregate_bench.sh <session_dir>

set -euo pipefail

if [ $# -ne 1 ]; then
  echo "usage: $0 <session_dir>" >&2
  exit 2
fi

session="$1"
[ -d "$session" ] || { echo "$session: not a directory" >&2; exit 2; }

# $1 = glob pattern, $2 = test name, $3 = field name to extract
extract() {
  local glob="$1" test="$2" field="$3"
  local f
  # shellcheck disable=SC2086
  for f in $glob; do
    [ -f "$f" ] || continue
    grep "BENCH test=$test " "$f" | grep -oE "(^| )$field=[0-9]+" | awk -F= '{print $2}'
  done
}

summarise() {
  sort -n | awk '
    { a[NR]=$1 }
    END {
      n=NR
      if (n==0) { print "  (no samples)"; exit }
      min=a[1]; max=a[n]
      if (n%2==1) { med=a[(n+1)/2] } else { med=(a[n/2]+a[n/2+1])/2 }
      printf "  n=%d min=%d median=%d max=%d\n", n, min, med, max
    }
  '
}

report() {
  local label="$1" glob="$2" test="$3" field="$4"
  echo "$label"
  extract "$glob" "$test" "$field" | summarise
}

echo "session: $session"
echo

report "MONO sys_write (per-run median_cycles)"     "$session/mono_latency_run_*.log"  sys_write      median_cycles
report "MONO ipc_roundtrip (per-run median_cycles)" "$session/mono_latency_run_*.log"  ipc_roundtrip  median_cycles
echo
report "MICRO sys_write (per-run median_cycles)"     "$session/micro_latency_run_*.log" sys_write      median_cycles
report "MICRO ipc_roundtrip (per-run median_cycles)" "$session/micro_latency_run_*.log" ipc_roundtrip  median_cycles
echo
report "MICRO recovery_window (per-run cycles)"      "$session/micro_recovery_run_*.log" recovery_window cycles
