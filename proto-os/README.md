# proto-os

Prototype AArch64 OS for a dissertation comparing monolithic and microkernel styles. Bring-up is QEMU AArch64 `virt` first, then Raspberry Pi 4.

## Milestones (current: M0-M6)
- M0: Boot to EL1, UART output.
- M1: Exception vectors installed.
- M2: 1ms timer IRQ heartbeat via GIC + generic timer.
- M3: Minimal EL1 syscall path via `SVC` with C dispatch:
  - `SYS_yield = 0`
  - `SYS_time_ticks = 1`
  - `SYS_write = 2`
- M4: Minimal EL1 kernel threads and scheduler:
  - context switch saves/restores `x19-x29`, `sp`, `lr`
  - two runnable threads (`A`, `B`) plus idle thread
  - 10-tick quantum driven by the existing 1ms timer
  - deferred preemption (`timer IRQ` sets reschedule pending; switch happens at safe thread-context point)
- M5: MMU enabled with identity kernel mapping:
  - TTBR0-only translation (`EPD1=1`)
  - 4KiB granule with 2MiB block mappings
  - kernel + required MMIO mapped
- M5.1: Cache policy enabled on top of M5 mapping:
  - normal memory set to cacheable WB/WA attributes
  - `SCTLR_EL1.C=1` and `SCTLR_EL1.I=1`
  - device mappings/attributes unchanged
- M6: One-shot EL0 user-mode smoke test:
  - `eret` entry to EL0 and EL0 `SVC` handling in EL1
  - `SYS_exit = 3` redirects exception return to a stable EL1 continuation target that resumes boot without relying on EL0 `x30`
  - EL0-only bounds checks for `SYS_write` (overflow-safe range checks)
  - EL0 sandbox in the last 2MiB of the 16MiB identity map

Full EL0 process model, per-process mappings, IPC, and service model are staged for later milestones.

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
- `[mmu] enabled identity map`
- `[mmu] caches on`
- `hello from el0` (exactly once)
- `[el0] returned to el1` (exactly once)
- `[tick] 1000` about once per second
- interleaved `A` / `B` markers over time

For microkernel flavor banner:

```bash
make micro-qemu
```

Expected output is otherwise the same, with:
- `[boot] proto-os (MICRO)`

## Debug
```bash
./scripts/run_qemu_gdb.sh
```

The script prints a GDB attach hint.

## Notes
- Kernel load/link address: `0x40000000`
- UART: PL011 at `0x09000000` (QEMU virt)
