# Design Notes (M0-M6)

## Scope
- Bring-up target is QEMU AArch64 `virt` for M0-M6.
- Implemented path: boot -> UART -> vectors -> timer IRQ heartbeat -> minimal SVC syscall dispatch -> kernel threads with deferred preemption -> MMU identity mapping -> caches on -> one-shot EL0 demo.
- Runtime scheduler execution remains EL1; M6 adds a controlled one-shot EL0 transition and return.

## Build environment policy
- Preferred development/build location on Windows hosts: WSL2 Linux filesystem (for example `~/src/proto-os`).
- Avoid `/mnt/c/...` for regular builds when possible due slower file IO and inconsistent executable-bit behavior.
- Raspberry Pi validation should be done with native Pi builds once hardware milestones are active.

## M3 syscall ABI (current)
- Trap source: `SVC #0`, handled by EL1 synchronous vector entry and C dispatch.
- Syscall number register: `x8`.
- Arguments: `x0`, `x1` (extended later as needed).
- Return value: `x0`.
- Implemented syscall numbers:
  - `SYS_yield = 0`
  - `SYS_time_ticks = 1` (returns `CNTVCT_EL0`)
  - `SYS_write = 2` (writes raw buffer bytes to UART)
  - `SYS_exit = 3` (M6 semantics: EL0 demo exit back to EL1 continuation)

## M6 EL0 smoke test (current)
- A one-shot EL0 demo runs before `gic_init()/timer_init()/thread_start()`.
- EL0 path:
  - kernel sets `SP_EL0`, `ELR_EL1`, `SPSR_EL1` and executes `eret`
  - EL0 stub prints `hello from el0` via `SYS_write`
  - EL0 stub issues `SYS_exit` and returns to EL1 continuation (`[el0] returned to el1`)
  - EL0 exit target uses an explicit function symbol (not compiler label-address extensions)
  - EL1 continuation path is `noreturn` and resumes boot directly (`gic_init -> timer_init -> arch_enable_irq -> thread_start`) to avoid LR/x30 coupling to EL0 state
- Exception return correctness:
  - sync vector restore path now reloads `ELR_EL1`/`SPSR_EL1` from trap frame before `eret`
  - IRQ path restore behavior is unchanged
- `SYS_write` enforces pointer bounds only for EL0-origin syscalls, including strict zero-length bounds.

## M4 scheduler model (current)
- Threads:
  - `thread_a`: emits `A` marker roughly every 200 scheduler ticks.
  - `thread_b`: emits `B` marker roughly every 200 scheduler ticks.
  - `idle`: executes `WFI` when no runnable work is selected.
- Time base:
  - Existing generic timer remains at 1ms period.
  - Scheduler quantum is 10 ticks.
- Deferred preemption:
  - Timer IRQ handler only updates quantum accounting and sets `resched_pending`.
  - No `context_switch()` is performed in IRQ context.
  - Actual switching occurs at explicit safe points in normal thread context.
- Context switch ABI (AArch64):
  - save/restore `x19-x29`, `sp`, `lr`.

## Memory mapping (current + future)
- M5 enables MMU with identity mapping only; no higher-half kernel mapping.
- Translation uses TTBR0-only in EL1 (`EPD1=1`), with 4KiB granule and 2MiB block mappings.
- Mapped regions in M5/M5.1:
  - Kernel RAM window starting at `0x40000000` (covers globals/stacks/page tables).
  - GIC MMIO block at `0x08000000`.
  - PL011 MMIO block at `0x09000000`.
- M6 adds an EL0-accessible sandbox at `0x40E00000-0x41000000` (last 2MiB block of the 16MiB identity map).
- EL0 sandbox normal-memory block is marked PXN; kernel normal-memory blocks are marked UXN.
- The rest of kernel normal memory remains EL1-only.
- Cache policy in current M5.1:
  - Normal memory is WB/WA cacheable (`MAIR_EL1` + `TCR_EL1.IRGN0/ORGN0`).
  - Data and instruction caches are enabled (`SCTLR_EL1.C=1`, `SCTLR_EL1.I=1`).
  - Device mappings/attributes remain unchanged.

## Fixed user VA plan (future)
- User virtual address window: `0x40000000-0x40200000` (2MiB).
- User stack top: `0x40020000` (grows downward).
- User apps will be flat, position-dependent images linked at `0x40000000` and copied into the mapped region later.

## IPC plan (future)
- Synchronous call/reply, fixed 256-byte messages, kernel copy.
- Static endpoints:
  - `EP_ECHO = 1`
  - `EP_UART = 2`
  - `EP_SUPERVISOR = 3`
- Owner/pid model to be defined later.

## Fault policy (future)
- EL1 faults: panic and print ESR/FAR/ELR/SPSR.
- EL0 faults: kill process, log one-line, optional supervisor notify placeholder.
