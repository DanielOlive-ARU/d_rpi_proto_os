.section .text.boot, "ax"
.global _start
.extern kernel_main

_start:
  mrs x0, CurrentEL
  lsr x0, x0, #2
  cmp x0, #2
  b.eq from_el2
  cmp x0, #1
  b.eq el1_entry
  b .

from_el2:
  mov x0, #(1 << 31)
  msr HCR_EL2, x0
  mrs x0, CNTHCTL_EL2
  orr x0, x0, #3
  msr CNTHCTL_EL2, x0
  msr CNTVOFF_EL2, xzr
  mov x0, #0x3C5
  msr SPSR_EL2, x0
  adr x0, el1_entry
  msr ELR_EL2, x0
  eret

el1_entry:
  ldr x0, =__stack_top
  mov sp, x0

  ldr x0, =__bss_start
  ldr x1, =__bss_end
  mov x2, #0
bss_loop:
  cmp x0, x1
  b.hs bss_done
  str x2, [x0], #8
  b bss_loop
bss_done:
  bl kernel_main

1:
  wfi
  b 1b
