# Design Notes (M0-M9)

## Scope
- Bring-up target is QEMU AArch64 `virt` for M0-M9.
- Implemented path: boot -> UART -> vectors -> timer IRQ heartbeat -> minimal SVC dispatch -> deferred scheduler -> MMU identity map -> caches on -> persistent EL0 tasks -> synchronous IPC baseline -> `SYS_write` service split.
- M9 is the first functional MONO/MICRO divergence.

## Build environment policy
- Preferred build location on Windows hosts: WSL2 Linux filesystem (for example `~/src/proto-os`).
- Avoid `/mnt/c/...` for regular builds due slower IO and timestamp/file-mode quirks.
- Raspberry Pi validation should use native Pi builds once hardware milestones are active.

## Syscall ABI (current)
- Trap source: `SVC #0`, handled by EL1 synchronous vector entry and C dispatch.
- Number in `x8`, args in `x0..x4` depending on syscall, return in `x0`.
- Implemented syscall numbers:
  - `SYS_yield = 0`
  - `SYS_time_ticks = 1`
  - `SYS_write = 2`
  - `SYS_exit = 3`
  - `SYS_ipc_call = 4`
  - `SYS_ipc_recv = 5`
  - `SYS_ipc_reply = 6`
- EL0-only bounds checks are enforced for `SYS_write` (overflow-safe, strict zero-length check).
- IPC buffers use the same EL0 sandbox bounds-checking model.

## Scheduler and EL0 task model (current M9)
- Existing EL1 scheduler/context-switch core is retained.
- Deferred preemption remains unchanged:
  - timer IRQ updates tick/quantum and sets reschedule pending
  - no context switch in IRQ or SVC paths
  - switching occurs only at safe thread-context points
- Two persistent EL0 tasks now implement writer/server split:
  - `task_a` calls `SYS_write("A\n")` periodically, then yields
  - `task_b` runs `uart_server`: prints one-time `[uart] ready`, receives endpoint messages, writes payload, sends ACK reply
- Idle thread/path remains in EL1 and uses `WFI`.
- Task states include `TASK_RUNNABLE`, `TASK_RUNNING`, `TASK_BLOCKED`, `TASK_DEAD`.
- `TASK_BLOCKED` is active for blocking IPC paths (`ipc_call` and `ipc_recv` slow path).
- In MONO, `task_b` blocks in `ipc_recv` after `[uart] ready`; this is expected in M9.
- EL0 synchronous non-SVC faults kill the current EL0 task and log a compact one-line fault message.

## EL1 <-> EL0 return mechanics (current)
- Sync vectors restore `ELR_EL1` and `SPSR_EL1` from trap frame before `eret`.
- Yield/exit/fault redirection rewrites trap-frame `ELR/SPSR` to a shared EL1 continuation function.
- IPC blocking paths reuse the same redirection mechanism with `TASK_RETURN_IPC_BLOCK`.
- Continuation runs in normal thread context, reaches scheduler safe points, then resumes EL0 via saved user context.
- EL0 resume path masks IRQ while programming `ELR_EL1/SPSR_EL1` to avoid race-clobber before `eret`.

## Memory mapping (current + future)
- MMU is enabled with identity mapping only; no higher-half mapping.
- Translation uses TTBR0-only (`EPD1=1`), 4KiB granule, 2MiB block mappings.
- Current mapped regions:
  - Kernel RAM window `0x40000000` + 16MiB
  - GIC MMIO block `0x08000000`
  - PL011 MMIO block `0x09000000`
- EL0 sandbox (shared by current EL0 tasks): `0x40E00000-0x41000000`.
- Permission hardening:
  - kernel normal-memory blocks are EL0 non-executable
  - EL0 sandbox block is EL0 RW and EL1 non-executable
  - device mappings unchanged
- Cache policy (M5.1):
  - normal memory WB/WA (`MAIR_EL1`, `TCR_EL1.IRGN0/ORGN0`)
  - `SCTLR_EL1.C=1`, `SCTLR_EL1.I=1`

## Fixed user VA plan (future)
- User VA window: `0x40000000-0x40200000` (2MiB).
- User stack top: `0x40020000` (downward growth).
- User apps remain planned as flat, position-dependent images initially.

## IPC baseline (current M9)
- Model: synchronous call/reply with fixed-size kernel-copy messages (`IPC_MSG_SIZE=256`).
- Endpoint table is static in-kernel.
- Active endpoint:
  - `EP_UART = 1` owned by `task_b`
- Per-endpoint state is intentionally minimal:
  - one pending request slot
  - one blocked caller awaiting reply
  - one blocked receiver awaiting request
- Endpoint state includes `caller_result_override` to preserve return semantics for internally routed `SYS_write` calls in MICRO.
- No capabilities, dynamic registration, or deep queues in M9.

## MONO vs MICRO split (M9)
- MONO:
  - `SYS_write` remains direct kernel UART path.
- MICRO:
  - `SYS_write` from normal EL0 tasks routes through `EP_UART` via IPC to `task_b` uart_server.
  - `SYS_write` from EL1 and from `task_b` uses direct kernel UART path (prevents recursion, preserves boot/log path).
  - Routed write requests are limited to `len <= IPC_MSG_SIZE`.

## IPC roadmap (future)
- Add supervisor and richer endpoint ownership policy in later milestones.
- Add owner/pid policy and capability model in later milestones.

## Fault policy roadmap
- EL1 faults: panic and print ESR/FAR/ELR/SPSR.
- EL0 faults: kill task/process, log one-line summary, supervisor notification/restart later.
