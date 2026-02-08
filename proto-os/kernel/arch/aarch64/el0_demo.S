.section .text, "ax"
.align 4
.global el0_enter

el0_enter:
  msr sp_el0, x1
  msr elr_el1, x0
  msr spsr_el1, x2
  isb
  eret

.equ SYS_WRITE_NR, 2
.equ SYS_EXIT_NR, 3

.section .el0_sandbox.text, "ax"
.align 4
.global __el0_demo_entry

__el0_demo_entry:
  adr x0, __el0_demo_msg
  adr x1, __el0_demo_msg_end
  sub x1, x1, x0
  mov x8, #SYS_WRITE_NR
  svc #0

  mov x8, #SYS_EXIT_NR
  svc #0

1:
  b 1b

.section .el0_sandbox.rodata, "a"
.align 3
.global __el0_demo_msg
.global __el0_demo_msg_end

__el0_demo_msg:
  .ascii "hello from el0\n"
__el0_demo_msg_end:
