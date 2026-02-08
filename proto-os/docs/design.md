# Design Notes (M0-M3)

## Scope
- Bring-up target is QEMU AArch64 `virt` for M0-M3.
- Implemented path: boot -> UART -> vectors -> timer IRQ heartbeat -> minimal SVC syscall dispatch.
- All execution remains EL1 for now (no EL0 transition yet).

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

## Memory mapping (future)
- Identity mapping only is planned initially; no higher-half kernel mapping.
- Page size target: 4KiB.
- MMU is OFF for M0-M3.

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
