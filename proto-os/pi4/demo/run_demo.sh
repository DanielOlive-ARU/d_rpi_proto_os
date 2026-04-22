#!/usr/bin/env bash
#
# Pi 4 expo demo — repeating MICRO demo runner.
#
# Loops the proto-os MICRO kernel under QEMU virt cortex-a57. Between each
# iteration the screen is cleared, a brief banner explains what to look
# for, then QEMU runs for DEMO_DURATION seconds (default 15) before the
# loop pauses PAUSE_BETWEEN seconds (default 5) and restarts.
#
# Stop with Ctrl+C. Skip to the next iteration with Ctrl+A then X
# (which exits the QEMU instance; the loop will restart it).
#
# Offline-safe — no apt, no network. setup_pi.sh must have been run
# once previously to install qemu-system-arm and build the kernel.
#
# Defaults to TCG accel (KVM did not run on this Pi). To try KVM later,
# set ACCEL=kvm CPU=host before running:
#     ACCEL=kvm CPU=host ./run_demo.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAKE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
KERNEL_ELF="$MAKE_DIR/build/kernel.elf"

DEMO_DURATION="${DEMO_DURATION:-15}"
PAUSE_BETWEEN="${PAUSE_BETWEEN:-5}"
ACCEL="${ACCEL:-tcg}"
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
  proto-os MICRO demo  (QEMU virt cortex-a57 on Raspberry Pi 4)
================================================================
  Watch for, in order:
    BOOT
    [boot] proto-os (MICRO)              kernel boot, MICRO flavour
    [uart] ready                         user-space UART server up
    [sup] ready                          supervisor task up
    A                                    visible writer task (task_a)
    [fault] task_b dead esr=0xf2...      task_b deliberately crashes
    [sup] restarted uart                 supervisor restarts the service
    [uart] ready                         service back up, no reboot
    A continues                          uninterrupted recovery

  Loop runs for ~15s, pauses ~5s, restarts. Ctrl+C to stop.
================================================================

EOF
  sleep 2
}

run_once() {
  timeout "$DEMO_DURATION" qemu-system-aarch64 \
    -M virt \
    -cpu "$CPU" \
    -m 512M \
    -nographic \
    -serial stdio \
    -monitor none \
    -accel "$ACCEL" \
    -kernel "$KERNEL_ELF" \
    || true
}

trap 'echo; echo "demo loop stopped"; exit 0' INT TERM

while true; do
  print_banner
  run_once
  echo
  echo "(restarting in ${PAUSE_BETWEEN}s — Ctrl+C to stop)"
  sleep "$PAUSE_BETWEEN"
done
