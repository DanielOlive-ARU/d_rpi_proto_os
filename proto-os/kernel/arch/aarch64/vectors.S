.section .text, "ax"
.align 11
.global vectors
.global vectors_init

vectors:
  b el1t_sync
  .balign 128
  b el1t_irq
  .balign 128
  b el1t_fiq
  .balign 128
  b el1t_serr
  .balign 128
  b el1h_sync
  .balign 128
  b el1h_irq
  .balign 128
  b el1h_fiq
  .balign 128
  b el1h_serr
  .balign 128
  b el0_sync
  .balign 128
  b el0_irq
  .balign 128
  b el0_fiq
  .balign 128
  b el0_serr
  .balign 128
  b el0_32_sync
  .balign 128
  b el0_32_irq
  .balign 128
  b el0_32_fiq
  .balign 128
  b el0_32_serr
  .balign 128

vectors_init:
  adr x0, vectors
  msr vbar_el1, x0
  isb
  ret

.equ FRAME_SIZE, (36 * 8)

.macro SAVE_CONTEXT
  sub sp, sp, #FRAME_SIZE
  stp x0, x1, [sp, #0]
  stp x2, x3, [sp, #16]
  stp x4, x5, [sp, #32]
  stp x6, x7, [sp, #48]
  stp x8, x9, [sp, #64]
  stp x10, x11, [sp, #80]
  stp x12, x13, [sp, #96]
  stp x14, x15, [sp, #112]
  stp x16, x17, [sp, #128]
  stp x18, x19, [sp, #144]
  stp x20, x21, [sp, #160]
  stp x22, x23, [sp, #176]
  stp x24, x25, [sp, #192]
  stp x26, x27, [sp, #208]
  stp x28, x29, [sp, #224]
  str x30, [sp, #240]
  add x1, sp, #FRAME_SIZE
  str x1, [sp, #248]
  mrs x1, elr_el1
  str x1, [sp, #256]
  mrs x1, spsr_el1
  str x1, [sp, #264]
  mrs x1, esr_el1
  str x1, [sp, #272]
  mrs x1, far_el1
  str x1, [sp, #280]
.endm

.macro RESTORE_CONTEXT
  ldp x0, x1, [sp, #0]
  ldp x2, x3, [sp, #16]
  ldp x4, x5, [sp, #32]
  ldp x6, x7, [sp, #48]
  ldp x8, x9, [sp, #64]
  ldp x10, x11, [sp, #80]
  ldp x12, x13, [sp, #96]
  ldp x14, x15, [sp, #112]
  ldp x16, x17, [sp, #128]
  ldp x18, x19, [sp, #144]
  ldp x20, x21, [sp, #160]
  ldp x22, x23, [sp, #176]
  ldp x24, x25, [sp, #192]
  ldp x26, x27, [sp, #208]
  ldp x28, x29, [sp, #224]
  ldr x30, [sp, #240]
  add sp, sp, #FRAME_SIZE
.endm

.macro RESTORE_SYNC_CONTEXT
  /* Restore edited ELR/SPSR before GPR restore; x0 is scratch here. */
  ldr x0, [sp, #256]
  msr elr_el1, x0
  ldr x0, [sp, #264]
  msr spsr_el1, x0
  RESTORE_CONTEXT
.endm

el1t_sync:
  b el1h_sync
el1t_irq:
  b el1h_irq
el1t_fiq:
  b el1h_sync
el1t_serr:
  b el1h_sync

el1h_sync:
  SAVE_CONTEXT
  mov x0, sp
  bl exception_sync_el1
  RESTORE_SYNC_CONTEXT
  eret

el1h_irq:
  SAVE_CONTEXT
  mov x0, sp
  bl exception_irq
  RESTORE_CONTEXT
  eret

el1h_fiq:
  b el1h_sync
el1h_serr:
  b el1h_sync

el0_sync:
  SAVE_CONTEXT
  mov x0, sp
  bl exception_sync_el0
  RESTORE_SYNC_CONTEXT
  eret

el0_irq:
  SAVE_CONTEXT
  mov x0, sp
  bl exception_irq
  RESTORE_CONTEXT
  eret

el0_fiq:
  b el0_sync
el0_serr:
  b el0_sync

el0_32_sync:
  b el0_sync
el0_32_irq:
  b el0_irq
el0_32_fiq:
  b el0_sync
el0_32_serr:
  b el0_sync
