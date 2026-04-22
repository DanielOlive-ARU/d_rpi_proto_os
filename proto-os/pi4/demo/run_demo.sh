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

  set +e
  timeout "$DEMO_DURATION" qemu-system-aarch64 "${args[@]}"
  local rc=$?
  set -e

  # rc 124 = timeout killed it (expected); rc 0 = clean exit; anything
  # else is a real qemu failure worth showing.
  if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ] && [ "$rc" -ne 143 ]; then
    echo
    echo "(qemu exited rc=$rc — see any error text above)"
  fi
}

trap 'echo; echo "demo loop stopped"; exit 0' INT TERM

while true; do
  print_banner
  run_once
  echo
  echo "(restarting in ${PAUSE_BETWEEN}s — Ctrl+C to stop)"
  sleep "$PAUSE_BETWEEN"
done
