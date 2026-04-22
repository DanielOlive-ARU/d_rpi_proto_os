#!/usr/bin/env bash
#
# proto-os Pi 4 expo demo — repeating MICRO demo runner.
#
# Loops the proto-os MICRO kernel. Between each iteration the screen is
# cleared, a short banner explains what to look for, and the demo runs
# for DEMO_DURATION seconds (default 20) before the loop pauses
# PAUSE_BETWEEN seconds (default 5) and restarts.
#
# Stop with Ctrl+C. Skip to the next iteration with Ctrl+A then X.
#
# Offline-safe — no apt, no network. setup_pi.sh must have been run
# once previously to install dependencies and build the kernel.
#
# Optional environment overrides:
#   DEMO_DURATION  seconds per iteration (default 20)
#   PAUSE_BETWEEN  seconds between iterations (default 5)
#   ACCEL          QEMU accelerator. Unset = QEMU's default. Set to
#                  'kvm' to try hardware acceleration (requires CPU=host).
#   CPU            guest CPU model (default cortex-a57; use 'host' with KVM)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAKE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
KERNEL_ELF="$MAKE_DIR/build/kernel.elf"
PRESENTER="$SCRIPT_DIR/presenter.py"

DEMO_DURATION="${DEMO_DURATION:-20}"
PAUSE_BETWEEN="${PAUSE_BETWEEN:-5}"
ACCEL="${ACCEL:-}"
CPU="${CPU:-cortex-a57}"

if [ ! -f "$KERNEL_ELF" ]; then
  echo "ERROR: kernel not found at $KERNEL_ELF" >&2
  echo "Run setup_pi.sh first (requires internet)." >&2
  exit 1
fi

if ! command -v qemu-system-aarch64 >/dev/null 2>&1; then
  echo "ERROR: qemu-system-aarch64 not installed." >&2
  echo "Run setup_pi.sh first (requires internet)." >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERROR: python3 not found (required by the presenter layer)." >&2
  echo "On Raspberry Pi OS Lite it should be preinstalled; run 'sudo apt install python3'." >&2
  exit 1
fi

if [ ! -f "$PRESENTER" ]; then
  echo "ERROR: presenter not found at $PRESENTER" >&2
  exit 1
fi

print_banner() {
  clear
  cat <<'EOF'
================================================================
  proto-os — microkernel fault isolation demo
================================================================
  Watch for:
    [fault] task_b dead     uart_server crashes on purpose
    [sup] restarted uart    supervisor brings the service back up
    A continues             service restored without a reboot
================================================================

EOF
}

run_once() {
  local args=(-M virt -cpu "$CPU" -m 512M -nographic
              -serial stdio -monitor none
              -kernel "$KERNEL_ELF")
  if [ -n "$ACCEL" ]; then
    args+=(-accel "$ACCEL")
  fi

  # --foreground keeps timeout (and therefore qemu) in the same process
  # group as the shell, so signals propagate cleanly through the pipe
  # into presenter.py. The serial stream flows qemu -> pipe -> presenter
  # -> tty; when timeout kills qemu, qemu closes stdout, presenter sees
  # EOF and exits naturally.
  set +e
  timeout --foreground "$DEMO_DURATION" qemu-system-aarch64 "${args[@]}" \
    | python3 "$PRESENTER"
  # Snapshot PIPESTATUS into a local array in one step — any simple
  # command after the pipeline (including ``local rc=...``) overwrites
  # PIPESTATUS, so both elements must be captured atomically.
  local pipestat=("${PIPESTATUS[@]}")
  local rc=${pipestat[0]}
  local pres_rc=${pipestat[1]}
  set -e

  # Clear immediately to wipe qemu's 'terminating on signal' line and
  # any other ceremonial mentions from the visitor's view. Tradeoff:
  # the last ~1s of demo output disappears at iteration boundaries.
  clear

  # rc 124 = timeout killed it (expected); rc 0 = clean exit; rc 143
  # = SIGTERM (also expected). Anything else is a real failure worth
  # showing on the cleared screen so we can diagnose at the expo.
  if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ] && [ "$rc" -ne 143 ]; then
    echo
    echo "(demo exited rc=$rc)"
  fi
  if [ "$pres_rc" -ne 0 ]; then
    echo
    echo "(presenter exited rc=$pres_rc)"
  fi
}

trap 'echo; echo "demo loop stopped"; exit 0' INT TERM

while true; do
  print_banner
  run_once
  echo
  echo "(next demo in ${PAUSE_BETWEEN}s — Ctrl+C to stop)"
  sleep "$PAUSE_BETWEEN"
done
