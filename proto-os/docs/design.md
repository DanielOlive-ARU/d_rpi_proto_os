# Design Notes (M0-M10)

## Scope
- Bring-up target is QEMU AArch64 `virt` for M0-M10.
- Implemented path: boot -> UART -> vectors -> timer IRQ heartbeat -> minimal SVC dispatch -> deferred scheduler -> MMU identity map -> caches on -> persistent EL0 tasks -> synchronous IPC baseline -> `SYS_write` service split -> MICRO crash/restart demo.
- M9 is the first functional MONO/MICRO divergence; M10 adds supervised service recovery in MICRO.

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
  - `SYS_supervise_wait = 7`
  - `SYS_task_restart = 8`
- EL0-only bounds checks are enforced for `SYS_write` (overflow-safe, strict zero-length check).
- IPC buffers use the same EL0 sandbox bounds-checking model.

## Scheduler and EL0 task model (current M10)
- Existing EL1 scheduler/context-switch core is retained.
- Deferred preemption remains unchanged:
  - timer IRQ updates tick/quantum and sets reschedule pending
  - no context switch in IRQ or SVC paths
  - switching occurs only at safe thread-context points
- Three persistent EL0 tasks:
  - `task_a` calls `SYS_write("A\n")` periodically, then yields
  - `task_b` runs `uart_server`: prints one-time `[uart] ready`, receives endpoint messages, writes payload, sends ACK reply, then crashes once (fault injection)
  - `task_c` runs supervisor: prints one-time `[sup] ready`, waits for task-death notification, restarts `task_b`, logs `[sup] restarted uart`
- Idle thread/path remains in EL1 and uses `WFI`.
- Task states include `TASK_RUNNABLE`, `TASK_RUNNING`, `TASK_BLOCKED`, `TASK_DEAD`.
- `TASK_BLOCKED` is active for blocking IPC paths and supervisor wait.
- In MONO, `task_b` blocks in `ipc_recv` after `[uart] ready`, and `task_c` blocks in `SYS_supervise_wait`; this is expected in M10.
- EL0 synchronous non-SVC faults kill the current EL0 task and log a compact one-line fault message.

## EL1 <-> EL0 return mechanics (current)
- Sync vectors restore `ELR_EL1` and `SPSR_EL1` from trap frame before `eret`.
- Yield/exit/fault redirection rewrites trap-frame `ELR/SPSR` to a shared EL1 continuation function.
- IPC blocking paths reuse the same redirection mechanism with `TASK_RETURN_IPC_BLOCK`.
- Supervisor wait blocking uses the same mechanism with `TASK_RETURN_NOTIFY_BLOCK`.
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
- EL0 stack layout in M10:
  - `task_c`: `0x40FA0000-0x40FC0000`
  - `task_a`: `0x40FC0000-0x40FE0000`
  - `task_b`: `0x40FE0000-0x41000000`
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

## IPC baseline (current M10)
- Model: synchronous call/reply with fixed-size kernel-copy messages (`IPC_MSG_SIZE=256`).
- Endpoint table is static in-kernel.
- Active endpoint:
  - `EP_UART = 1` owned by `task_b`
- Per-endpoint state is intentionally minimal:
  - one pending request slot
  - one blocked caller awaiting reply
  - one blocked receiver awaiting request
- Endpoint state includes `caller_result_override` to preserve return semantics for internally routed `SYS_write` calls in MICRO.
- Endpoint cleanup on task death clears in-flight blocked/pending state and can wake blocked callers with `-1`.
- No capabilities, dynamic registration, or deep queues in M10.

## MONO vs MICRO split (M9/M10)
- MONO:
  - `SYS_write` remains direct kernel UART path.
- MICRO:
  - `SYS_write` from normal EL0 tasks routes through `EP_UART` via IPC to `task_b` uart_server.
  - `SYS_write` from EL1, `task_b`, and `task_c` uses direct kernel UART path (preserves boot/log path and lets supervisor log during service recovery).
  - Routed write requests are limited to `len <= IPC_MSG_SIZE`.
  - `task_b` crash is contained to EL0 task state; supervisor restarts it without reboot.

## Supervisor model (M10)
- Single pending death notification slot and single blocked supervisor waiter slot.
- `SYS_supervise_wait`:
  - immediate return with dead slot if notification pending
  - otherwise blocks via redirect path
- `SYS_task_restart`:
  - supervisor-only
  - fixed policy for restarting `task_b` only in M10
- Restart reuses stored EL0 entry/SP template and does not alter endpoint ownership.

## IPC roadmap (future)
- Add owner/pid policy and capability model in later milestones.

## Fault policy roadmap
- EL1 faults: panic and print ESR/FAR/ELR/SPSR.
- EL0 faults: kill task, log one-line summary, cleanup IPC state, notify supervisor, optional targeted restart (M10 scope is `task_b` only).
