#!/usr/bin/env bash
set -euo pipefail

if [ ! -f Makefile ]; then
  echo "run from the proto-os/ directory" >&2
  exit 1
fi

if [ ! -f build/kernel.elf ]; then
  echo "build/kernel.elf not found; run: make mono-qemu or make micro-qemu" >&2
  exit 1
fi

if grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null; then
  if [[ "$PWD" == /mnt/* ]]; then
    echo "warning: WSL builds are recommended from ~/src/proto-os, not /mnt/* (faster IO, cleaner file modes)." >&2
  fi
fi

qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -nographic \
  -serial stdio -monitor none -kernel build/kernel.elf
