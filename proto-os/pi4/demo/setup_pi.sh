#!/usr/bin/env bash
#
# Pi 4 expo demo — one-time setup script.
#
# REQUIRES INTERNET. Run this on the Pi while it can reach the Debian /
# Raspberry Pi OS package mirrors. Once this completes, the demo can be
# run fully offline (no apt, no network).
#
# What it does:
#   - installs qemu-system-arm + an aarch64-linux-gnu cross-named toolchain
#     (the cross-named binaries are what the proto-os Makefile expects;
#     on a Pi they produce native aarch64 code just fine)
#   - builds the MICRO kernel ready for run_demo.sh to launch
#
# What it does NOT do:
#   - configure auto-login (do this manually if desired — see README)
#   - copy / symlink run_demo.sh anywhere outside the repo
#   - touch any other system configuration

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAKE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
KERNEL_FLAVOR="MICRO"

echo "==> Installing required packages (needs internet)"
sudo apt update
sudo apt install -y \
    qemu-system-arm \
    gcc-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu

echo
echo "==> Verifying tools"
qemu-system-aarch64 --version | head -1
aarch64-linux-gnu-gcc --version | head -1

echo
echo "==> Building $KERNEL_FLAVOR kernel from $MAKE_DIR"
cd "$MAKE_DIR"
make clean
make KERNEL_FLAVOR="$KERNEL_FLAVOR" BENCH_MODE=OFF build

echo
echo "==> Verifying build artefact"
ls -la "$MAKE_DIR/build/kernel.elf"

cat <<EOF

==> Setup complete.

    Repo:    $MAKE_DIR
    Kernel:  $MAKE_DIR/build/kernel.elf
    Runner:  $SCRIPT_DIR/run_demo.sh

Run the demo now:
    $SCRIPT_DIR/run_demo.sh

The runner does not need internet — it can be run any time after this
setup has completed once.

Auto-login is NOT configured. To configure manually after this script,
see "Manual auto-login" in README.md.
EOF
