# proto-os

Prototype AArch64 OS for a dissertation comparing monolithic and microkernel styles. This repo focuses on bring-up on QEMU AArch64 `virt`, then later Raspberry Pi 4.

## Milestones (current)
- M0: Boot to EL1, UART output.
- M1: Exception vectors installed.
- M2: 1ms timer interrupt heartbeat via GIC + generic timer.

MMU, EL0/user, syscalls, and IPC are placeholders only.

## Quick start (QEMU)
From `proto-os/`:

```
make mono-qemu
```

Expected output includes:
- `BOOT`
- `[boot] proto-os (MONO)`
- `[tick] 1000` about once per second

For microkernel flavor banner only:

```
make micro-qemu
```

## Debug
```
./scripts/run_qemu_gdb.sh
```

GDB hint is printed by the script.

## Notes
- Kernel load/link address: `0x40000000`
- UART: PL011 at `0x09000000` (QEMU virt)
