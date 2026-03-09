# proto-os

Prototype AArch64 OS for a dissertation comparing monolithic and microkernel styles. Bring-up is QEMU AArch64 `virt` first, then Raspberry Pi 4.

## Milestones (current: M0-M10)
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
- M6: EL0 smoke-test plumbing (`eret` + EL0 `SVC` path) and EL0-only `SYS_write` bounds checks.
- M7: Persistent EL0 tasks on top of the existing scheduler:
  - two EL0 tasks (`task_a`, `task_b`) replace kernel A/B demo threads
  - deferred preemption unchanged (no context switch in IRQ/SVC)
  - EL0 tasks share one sandbox region (`0x40E00000-0x41000000`)
  - `TASK_BLOCKED` added in task model
  - M6 one-shot EL0 demo markers are retired
- M8: Baseline synchronous IPC (still no MONO/MICRO divergence):
  - static endpoint table with one active endpoint owner on `task_b`
  - EL0-only syscalls: `SYS_ipc_call=4`, `SYS_ipc_recv=5`, `SYS_ipc_reply=6`
  - fixed-size kernel-copy messages (`IPC_MSG_SIZE=256`)
  - one blocked caller + one blocked receiver + one pending request per endpoint
  - `TASK_BLOCKED` is now active for blocking IPC paths
- M9: First MONO/MICRO architectural split on `SYS_write`:
  - MONO: EL0 `SYS_write` uses direct kernel UART write
  - MICRO: EL0 `SYS_write` (except `task_b` uart_server) is routed via IPC to `EP_UART=1`
  - `task_b` runs a user-space uart_server loop and prints one-time `[uart] ready`
  - PL011 remains kernel-mediated (not mapped into EL0)
- M10: MICRO fault isolation + recovery demo:
  - adds supervisor task `task_c` in EL0 (`[sup] ready`)
  - `task_b` (`uart_server`) intentionally crashes once via `brk #0`
  - kernel fault path marks task dead, cleans IPC state, notifies supervisor
  - supervisor restarts `task_b` and logs `[sup] restarted uart`
  - service resumes without reboot (`[uart] ready` appears again in MICRO)

Full process isolation, capability model, and service split are staged for later milestones.

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
- one-time `[uart] ready`
- one-time `[sup] ready`
- `[tick] 1000` about once per second
- recurring `A` marker from EL0 writer task (`task_a`)

For microkernel flavor banner:

```bash
make micro-qemu
```

Expected output is otherwise the same, with:
- `[boot] proto-os (MICRO)`
- plus one-time recovery markers:
  - fault line for crashed `task_b`
  - `[sup] restarted uart`
  - second `[uart] ready` after restart

## Debug
```bash
./scripts/run_qemu_gdb.sh
```

The script prints a GDB attach hint.

## Notes
- Kernel load/link address: `0x40000000`
- UART: PL011 at `0x09000000` (QEMU virt)
