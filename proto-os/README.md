# proto-os

Prototype AArch64 OS for a dissertation comparing monolithic and microkernel styles. Bring-up is QEMU AArch64 `virt` first, then Raspberry Pi 4.

## Milestones (current: M0-M3)
- M0: Boot to EL1, UART output.
- M1: Exception vectors installed.
- M2: 1ms timer IRQ heartbeat via GIC + generic timer.
- M3: Minimal EL1 syscall path via `SVC` with C dispatch:
  - `SYS_yield = 0`
  - `SYS_time_ticks = 1`
  - `SYS_write = 2`

EL0/user mode, MMU mappings, scheduler, and IPC are staged for later milestones.

## Recommended build locations
- WSL2: build/run from Linux home, usually `~/src/proto-os`.
- Avoid `/mnt/c/...` builds when possible (slower IO and less predictable executable-bit metadata).
- Raspberry Pi work: prefer native builds on the Pi when validating hardware paths.

## Clean rebuild (WSL2)
From `~/src/proto-os`:

```bash
make clean
make build
```

## Run without rebuilding
From `~/src/proto-os`:

```bash
./scripts/run_qemu.sh
```

Expected output includes:
- `BOOT`
- `[boot] proto-os (MONO)`
- `[svc] ok`
- `[svc] ticks <value>`
- `[tick] 1000` about once per second

For microkernel flavor banner only (same behavior at M0-M3):

```bash
make micro-qemu
```

## Debug
```bash
./scripts/run_qemu_gdb.sh
```

The script prints a GDB attach hint.

## Notes
- Kernel load/link address: `0x40000000`
- UART: PL011 at `0x09000000` (QEMU virt)
