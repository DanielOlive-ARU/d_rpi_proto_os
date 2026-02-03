# Design Notes (M0-M2)

## Scope
- M0-M2 bring-up is QEMU-only (AArch64 `virt`), focused on boot -> UART -> vectors -> timer IRQ heartbeat.
- EL0/user space, syscalls, IPC, and MMU are placeholders only.

## Memory mapping (future)
- Identity mapping only is planned initially; no higher-half kernel mapping.
- Page size target: 4KiB.
- MMU is OFF for M0-M2.

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
